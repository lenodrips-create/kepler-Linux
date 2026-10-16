/*
 * The shell: owns the screen, the mouse pointer and the main loop.
 * Everything is redrawn through a single dirty rectangle: anything that
 * changes calls ui_invalidate(), and once the input queue is drained the
 * dirty area is recomposed back to front and copied to the framebuffer.
 */

#include "apps.h"
#include "jpeg.h"
#include "net.h"
#include "ramfs.h"
#include "ui.h"

char g_user_name[32] = "Kepler";

enum { MODE_BOOT, MODE_LOGIN, MODE_DESKTOP, MODE_STANDBY };

static int mode = MODE_BOOT;
static bool signed_in; /* a user's session exists (possibly locked) */
static rect_t dirty;
static int mx, my, buttons;
static int cursor = CURSOR_ARROW;
static rect_t cursor_rect;

static uint32_t last_click_ms;
static int last_click_x, last_click_y;
static bool in_topbar, in_dock;
static bool win_held, win_combo;

static char tip_text[96];
static rect_t tip_rect;

static uint32_t beep_until;

#ifndef KEPLER_HOSTED
extern volatile int cpu_idle;
#endif

/* ---- invalidation & composition ------------------------------------------------------ */

void ui_invalidate(rect_t r)
{
    r = rect_intersect(r, rect_make(0, 0, g_screen.w, g_screen.h));
    if (!rect_empty(r))
        dirty = rect_union(dirty, r);
}

void ui_invalidate_all(void) { dirty = rect_make(0, 0, g_screen.w, g_screen.h); }

int ui_mouse_x(void) { return mx; }
int ui_mouse_y(void) { return my; }

rect_t ui_work_area(void) { return rect_make(0, TOPBAR_H, g_screen.w, g_screen.h - TOPBAR_H - DOCK_RESERVE); }

void shell_windows_changed(void)
{
    topbar_changed();
    dock_changed();
}

void ui_set_cursor(int c)
{
    if (c == cursor)
        return;
    ui_invalidate(cursor_rect);
    cursor = c;
    ui_invalidate(cursor_bounds(cursor, mx, my));
}

void ui_tooltip(const char *text, int x, int y)
{
    if (tip_text[0])
        ui_invalidate(tip_rect);
    if (!text) {
        tip_text[0] = 0;
        return;
    }
    strlcpy(tip_text, text, sizeof tip_text);
    int w = text_width(&font_ui, tip_text) + 14, h = 21;
    x = CLAMP(x, 2, g_screen.w - w - 2);
    y = CLAMP(y, 2, g_screen.h - h - 2);
    tip_rect = rect_make(x, y, w + 2, h + 4);
    ui_invalidate(tip_rect);
}

static void tooltip_paint(gfx_t *g)
{
    if (!tip_text[0])
        return;
    rect_t r = tip_rect;
    int w = r.w - 2, h = r.h - 4;
    gfx_round_rect(g, r.x, r.y + 2, w, h, 4, RGBA(0, 0, 0, 30), CORNER_ALL);
    gfx_round_rect(g, r.x, r.y, w, h, 4, HEX(0xF3F3F3), CORNER_ALL);
    gfx_round_frame(g, r.x, r.y, w, h, 4, RGBA(0, 0, 0, 60), CORNER_ALL);
    gfx_text(g, &font_ui, r.x + 7, r.y + (h - font_ui.height) / 2, tip_text, COL_TEXT);
}

/* Translucent surfaces blur whatever is already drawn under them, so when any
 * part of one needs repainting all of it does, from the bottom up, and the
 * pointer (drawn last, straight into the back buffer) must not be lying in
 * what it samples. */
static void expand_dirty(void)
{
    if (mode != MODE_DESKTOP || rect_empty(dirty))
        return;
    rect_t surf[8];
    int n = 0;
    surf[n++] = dock_rect();
    n += menu_rects(surf + n, 4);
    if (spotlight_is_open())
        surf[n++] = spotlight_rect();
    if (overlay_is_open())
        surf[n++] = overlay_rect();
    for (int pass = 0; pass < 4; pass++) {
        bool grew = false;
        for (int i = 0; i < n; i++) {
            if (rect_empty(rect_intersect(dirty, surf[i])))
                continue;
            rect_t u = rect_intersect(rect_union(dirty, surf[i]), rect_make(0, 0, g_screen.w, g_screen.h));
            if (u.x != dirty.x || u.y != dirty.y || u.w != dirty.w || u.h != dirty.h) {
                dirty = u;
                grew = true;
            }
        }
        if (!grew)
            break;
    }
    for (int i = 0; i < n; i++) {
        rect_t m = rect_make(surf[i].x - 24, surf[i].y - 24, surf[i].w + 48, surf[i].h + 48);
        if (!rect_empty(rect_intersect(dirty, m)) && !rect_empty(rect_intersect(cursor_rect, m)))
            dirty = rect_union(dirty, cursor_rect);
    }
}

static void compose(void)
{
    if (rect_empty(dirty))
        return;
    expand_dirty();
    gfx_t g = video_gfx();
    g.clip = dirty;
    switch (mode) {
    case MODE_LOGIN:
        screens_login_paint(&g, dirty);
        overlay_paint(&g, dirty);
        break;
    case MODE_DESKTOP:
        desktop_paint(&g, dirty);
        wm_paint(&g, dirty);
        if (launchpad_is_open())
            launchpad_paint(&g, dirty);
        else
            topbar_paint(&g, dirty);
        if (!wm_fullscreen())
            dock_paint(&g, dirty);
        spotlight_paint(&g);
        menu_paint(&g);
#ifdef KEPLER_HOSTED
        host_paint_popups(&g, dirty);
#endif
        tooltip_paint(&g);
        overlay_paint(&g, dirty);
        break;
    case MODE_STANDBY:
        gfx_fill(&g, 0, 0, g_screen.w, g_screen.h, HEX(0x000000));
        break;
    }
#ifdef KEPLER_HOSTED
    /* X draws the pointer */
    host_set_cursor(mode == MODE_STANDBY ? -1 : cursor);
    cursor_rect = rect_make(0, 0, 0, 0);
#else
    if (mode != MODE_STANDBY)
        cursor_draw(&g, cursor, mx, my);
    cursor_rect = cursor_bounds(cursor, mx, my);
#endif
    video_flush(dirty);
    dirty = rect_make(0, 0, 0, 0);
}

/* ---- PC speaker --------------------------------------------------------------------- */

void ui_beep(int hz, int ms)
{
#ifdef KEPLER_HOSTED
    (void)hz, (void)ms;
    host_beep();
    return;
#endif
    uint32_t div = 1193180 / hz;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, div >> 8);
    uint8_t t = inb(0x61);
    if ((t & 3) != 3)
        outb(0x61, t | 3);
    beep_until = timer_ms() + ms;
}

static void beep_update(void)
{
#ifdef KEPLER_HOSTED
    return;
#endif
    if (beep_until && (int32_t)(timer_ms() - beep_until) >= 0) {
        outb(0x61, inb(0x61) & 0xFC);
        beep_until = 0;
    }
}

/* ---- launching -------------------------------------------------------------------------- */

static void close_launchers(void)
{
    launchpad_close();
    spotlight_close();
}

void ui_launch(const char *cmd)
{
    char name[32];
    int n = 0;
    while (*cmd == ' ')
        cmd++;
    while (cmd[n] && cmd[n] != ' ' && n < 31) {
        name[n] = ktolower(cmd[n]);
        n++;
    }
    name[n] = 0;
    const char *arg = cmd + n;
    while (*arg == ' ')
        arg++;

    close_launchers();
    if (!strcmp(name, "notepad") || !strcmp(name, "edit") || !strcmp(name, "textedit"))
        notepad_open(*arg ? arg : NULL);
    else if (!strcmp(name, "kepler") || !strcmp(name, "browser") || !strcmp(name, "safari") || !strcmp(name, "web"))
#ifdef KEPLER_HOSTED
        host_launch("Kepler", *arg == '?' ? arg + 1 : arg);
#else
        browser_open(*arg ? arg : NULL);
#endif
    else if (!strcmp(name, "welcome"))
        welcome_open();
    else if (!strcmp(name, "brady") || !strcmp(name, "bradybuddy"))
        brady_open();
    else if (!strcmp(name, "calc") || !strcmp(name, "calculator"))
        calc_open();
    else if (!strcmp(name, "cmd") || !strcmp(name, "terminal") || !strcmp(name, "bash"))
#ifdef KEPLER_HOSTED
        host_launch("kepler-terminal", NULL); /* a real Linux shell, with the KeplerOS commands added */
#else
        terminal_open();
#endif
    else if (!strcmp(name, "explorer"))
        explorer_open(*arg ? arg : "::computer");
    else if (!strcmp(name, "finder") || !strcmp(name, "mydocs") || !strcmp(name, "documents"))
        explorer_open("/Documents");
    else if (!strcmp(name, "mycomputer") || !strcmp(name, "computer"))
        explorer_open("::computer");
    else if (!strcmp(name, "mypics"))
        explorer_open("/Documents/My Pictures");
    else if (!strcmp(name, "recycle") || !strcmp(name, "trash"))
        explorer_open("/Recycled");
    else if (!strcmp(name, "paint") || !strcmp(name, "mspaint"))
        paint_open();
    else if (!strcmp(name, "mines") || !strcmp(name, "winmine") || !strcmp(name, "minesweeper"))
        mines_open();
    else if (!strcmp(name, "taskmgr") || !strcmp(name, "activity"))
        taskmgr_open(*arg ? katoi(arg) : 0);
    else if (!strcmp(name, "about") || !strcmp(name, "winver"))
        about_open();
    else if (!strcmp(name, "display") || !strcmp(name, "control") || !strcmp(name, "desk.cpl") ||
             !strcmp(name, "sysprefs") || !strcmp(name, "preferences"))
        display_open();
    else if (!strcmp(name, "viewer") || !strcmp(name, "pictures") || !strcmp(name, "preview"))
        viewer_open(*arg ? arg : "/Documents/My Pictures/Kepler Earth.jpg");
    else if (!strcmp(name, "sysprops") || !strcmp(name, "sysdm.cpl") || !strcmp(name, "sysinfo"))
        sysprops_open();
    else if (!strcmp(name, "run") || !strcmp(name, "spotlight"))
        spotlight_open();
    else if (!strcmp(name, "launchpad"))
        launchpad_toggle();
    else if (!strcmp(name, "lock"))
        ui_lock();
    else if (!strcmp(name, "sleep"))
        ui_power(0);
    else if (!strcmp(name, "shutdown"))
        overlay_open(0);
    else if (!strcmp(name, "restart") || !strcmp(name, "reboot"))
        overlay_open(2);
    else if (!strcmp(name, "logoff") || !strcmp(name, "logout"))
        overlay_open(1);
    else {
        char msg[160];
        ksnprintf(msg, sizeof msg, "KeplerOS can't find \"%s\". Check the spelling and try again.", name);
        msgbox(NULL, name, msg, ICON_ERROR);
    }
}

void ui_login(void)
{
    kprintf("desktop: %s signed in\n", g_user_name);
    ui_set_cursor(CURSOR_ARROW);
    mode = MODE_DESKTOP;
    signed_in = true;
    ui_invalidate_all();
    shell_windows_changed();
    /* the Welcome screen, once per session, unless it was switched off */
    static bool welcomed;
    if (!welcomed && welcome_at_startup())
        welcome_open();
    welcomed = true;
}

void ui_lock(void)
{
    menu_close();
    close_launchers();
    ui_tooltip(NULL, 0, 0);
    screens_login_reset(true);
    mode = MODE_LOGIN;
    ui_invalidate_all();
}

void ui_logoff(void)
{
    menu_close();
    close_launchers();
    for (window_t *w = wm_first(); w; w = w->next)
        wm_close(w);
    signed_in = false;
    screens_login_reset(false);
    mode = MODE_LOGIN;
    ui_invalidate_all();
}

void ui_power(int action)
{
    if (action == 0) {
        menu_close();
        close_launchers();
        mode = MODE_STANDBY;
        ui_invalidate_all();
        return;
    }
    screens_goodbye(action);
}

/* waking from sleep always asks for the password again */
static void wake(void)
{
    if (signed_in)
        ui_lock();
    else {
        mode = MODE_LOGIN;
        ui_invalidate_all();
    }
}

bool ui_overlay_active(void) { return overlay_is_open(); }

bool ui_desktop_shown(void) { return mode == MODE_DESKTOP && !launchpad_is_open() && !overlay_is_open(); }

bool ui_wants_keyboard(void)
{
    return mode != MODE_DESKTOP || overlay_is_open() || menu_is_open() || spotlight_is_open() || launchpad_is_open();
}

/* ---- input routing ---------------------------------------------------------------------- */

static void route_mouse(int kind, int button)
{
    int x = mx, y = my;
    if (mode == MODE_LOGIN) {
        if (overlay_is_open())
            overlay_mouse(kind, x, y, button);
        else
            screens_login_mouse(kind, x, y, button);
        return;
    }
    if (tip_text[0] && (kind != MS_MOVE || !rect_contains(rect_make(tip_rect.x - 40, tip_rect.y - 40, tip_rect.w + 80, tip_rect.h + 80), x, y)))
        ui_tooltip(NULL, 0, 0);
    if (overlay_is_open()) {
        ui_set_cursor(CURSOR_ARROW);
        overlay_mouse(kind, x, y, button);
        return;
    }
    if (menu_is_open()) {
        /* a menu opened from inside a window swallows the release; let the window drop its capture */
        if (kind == MS_UP && wm_dragging())
            wm_mouse(kind, x, y, buttons, button);
        if (menu_mouse(kind, x, y, button))
            return;
    }
    if (spotlight_is_open() && spotlight_mouse(kind, x, y, button))
        return;
    if (wm_dragging()) {
        wm_mouse(kind, x, y, buttons, button);
        return;
    }
    bool over_bar = y < TOPBAR_H && !launchpad_is_open();
    if (over_bar || in_topbar) {
        if (!over_bar) {
            topbar_mouse(MS_LEAVE, x, y, 0);
            in_topbar = false;
        } else {
            in_topbar = true;
            ui_set_cursor(CURSOR_ARROW);
            topbar_mouse(kind, x, y, button);
            return;
        }
    }
    bool over_dock = rect_contains(dock_rect(), x, y);
    if (over_dock || in_dock) {
        if (!over_dock) {
            dock_mouse(MS_LEAVE, x, y, 0);
            in_dock = false;
        } else {
            in_dock = true;
            ui_set_cursor(CURSOR_ARROW);
            dock_mouse(kind, x, y, button);
            return;
        }
    }
    if (launchpad_is_open()) {
        ui_set_cursor(CURSOR_ARROW);
        launchpad_mouse(kind, x, y, button);
        return;
    }
    if (wm_mouse(kind, x, y, buttons, button))
        return;
    ui_set_cursor(CURSOR_ARROW);
    desktop_mouse(kind, x, y, button);
}

static void handle_mouse(const input_event_t *ev)
{
    entropy_add(ev, sizeof *ev);
    if (mode == MODE_STANDBY) {
        if (ev->buttons)
            wake();
        return;
    }
    if (ev->dx || ev->dy) {
        int nx = CLAMP(mx + ev->dx, 0, g_screen.w - 1);
        int ny = CLAMP(my + ev->dy, 0, g_screen.h - 1);
        if (nx != mx || ny != my) {
            ui_invalidate(cursor_rect);
            mx = nx;
            my = ny;
            ui_invalidate(cursor_bounds(cursor, mx, my));
            route_mouse(MS_MOVE, 0);
        }
    }
    if (ev->wheel)
        route_mouse(MS_WHEEL, ev->wheel);
    static const int order[3] = { MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE };
    for (int i = 0; i < 3; i++) {
        int b = order[i];
        bool was = buttons & b, now = ev->buttons & b;
        if (was == now)
            continue;
        if (now) {
            buttons |= b;
            route_mouse(MS_DOWN, b);
            if (b == MOUSE_LEFT) {
                uint32_t t = timer_ms();
                if (t - last_click_ms < 450 && ABS(mx - last_click_x) < 5 && ABS(my - last_click_y) < 5) {
                    route_mouse(MS_DBLCLICK, b);
                    last_click_ms = 0;
                } else {
                    last_click_ms = t;
                    last_click_x = mx;
                    last_click_y = my;
                }
            }
        } else {
            buttons &= ~b;
            route_mouse(MS_UP, b);
        }
    }
}

/* The Windows key plays the Command key: tapped alone it opens Launchpad. */
static bool global_key(const input_event_t *ev)
{
    uint8_t m = ev->mods;
    if (ev->key == KEY_LWIN) {
        if (ev->pressed) {
            win_held = true;
            win_combo = false;
        } else {
            if (win_held && !win_combo && !overlay_is_open()) {
                menu_close();
                spotlight_close();
                launchpad_toggle();
            }
            win_held = false;
        }
        return true;
    }
    if (!ev->pressed)
        return false;
    if (win_held) {
        win_combo = true;
        window_t *a = wm_active();
        switch (ktolower(ev->key)) {
        case ' ':
        case 'r': ui_launch("spotlight"); return true;
        case 'd': wm_minimize_all(); return true;
        case 'e':
        case 'n': ui_launch("finder"); return true;
        case 'm':
            if (a)
                wm_minimize(a);
            return true;
        case 'h':
            if (a)
                wm_hide_app(a->app);
            return true;
        case 'w':
        case 'q':
            if (a)
                wm_close(a);
            return true;
        case 'l': ui_lock(); return true;
        case KEY_TAB: wm_cycle(); return true;
        }
    }
    if ((m & MOD_CTRL) && (m & MOD_ALT) && (ev->key == KEY_DELETE || ev->key == '.')) {
        ui_launch("taskmgr");
        return true;
    }
    if ((m & MOD_CTRL) && ev->key == KEY_ESC) {
        launchpad_toggle();
        return true;
    }
    if ((m & MOD_ALT) && ev->key == KEY_F4) {
        window_t *w = wm_active();
        if (w)
            wm_close(w);
        else
            overlay_open(0);
        return true;
    }
    if ((m & MOD_ALT) && ev->key == KEY_TAB) {
        wm_cycle();
        return true;
    }
    return false;
}

static void handle_key(const input_event_t *ev)
{
    entropy_add(ev, sizeof *ev);
    if (mode == MODE_STANDBY) {
        if (ev->pressed)
            wake();
        return;
    }
    if (mode == MODE_LOGIN) {
        if (overlay_is_open())
            overlay_key(ev);
        else
            screens_login_key(ev);
        return;
    }
    if (overlay_is_open()) {
        overlay_key(ev);
        return;
    }
    if (global_key(ev))
        return;
    if (!ev->pressed)
        return;
    if (menu_is_open()) {
        menu_key(ev);
        return;
    }
    if (spotlight_is_open()) {
        spotlight_key(ev);
        return;
    }
    if (launchpad_is_open()) {
        launchpad_key(ev);
        return;
    }
    if (wm_active())
        wm_key(ev);
    else
        desktop_key(ev);
}

/* ---- boot and main loop ------------------------------------------------------------------- */

extern const uint8_t wallpaper_jpg[], wallpaper_jpg_end[];
extern const uint8_t kepler_icon_jpg[], kepler_icon_jpg_end[];
extern const uint32_t finder_icon_argb[], terminal_icon_argb[], notepad_icon_argb[], calc_icon_argb[],
    activity_icon_argb[], settings_icon_argb[], launchpad_icon_argb[], trash_icon_argb[],
    brady_icon_argb[];

static void boot_work(void (*progress)(int pct))
{
    progress(5);
    fs_init();
    net_init();
    image_t *src;
    if (!jpeg_decode(wallpaper_jpg, wallpaper_jpg_end - wallpaper_jpg, &src))
        panic("could not decode the wallpaper");
    progress(25);
    image_t *wall = image_cover(src, g_screen.w, g_screen.h);
    progress(55);
    /* only a picture that had to shrink a lot needs crisping up; at full size it is left alone */
    if (src->w > g_screen.w * 5 / 4 && src->h > g_screen.h * 5 / 4)
        image_sharpen(wall, 40);
    progress(65);
    image_free(src);
    icons_set_user_picture(wall);
    desktop_init(wall);
    progress(70);
    image_t *kepler;
    if (jpeg_decode(kepler_icon_jpg, kepler_icon_jpg_end - kepler_icon_jpg, &kepler))
        icons_set_image(ICON_BROWSER, kepler);
    /* pictures supplied for the dock: Finder, Terminal, Notepad, Calculator, Activity Monitor, Settings,
     * Launchpad and the Trash (empty or full) */
    static image_t pics[] = {
        { (uint32_t *)finder_icon_argb, 256, 256 },   { (uint32_t *)terminal_icon_argb, 256, 256 },
        { (uint32_t *)notepad_icon_argb, 256, 256 },  { (uint32_t *)calc_icon_argb, 150, 150 },
        { (uint32_t *)activity_icon_argb, 256, 256 }, { (uint32_t *)settings_icon_argb, 96, 96 },
        { (uint32_t *)launchpad_icon_argb, 256, 256 }, { (uint32_t *)trash_icon_argb, 256, 256 },
        { (uint32_t *)trash_icon_argb, 256, 256 },   { (uint32_t *)brady_icon_argb, 256, 256 },
    };
    static const int ids[] = { ICON_FINDER,  ICON_CMD,      ICON_NOTEPAD, ICON_CALC,      ICON_TASKMGR,
                               ICON_DISPLAY, ICON_ALLPROGS, ICON_RECYCLE, ICON_TRASH_FULL, ICON_BRADY };
    for (int i = 0; i < ARRAY_LEN(ids); i++)
        icons_set_image_plain(ids[i], &pics[i]);
    icons_prerender(NULL);
    progress(100);
}

static void tick_all(void)
{
    if (mode == MODE_DESKTOP) {
        topbar_tick();
        dock_tick();
        spotlight_tick();
        overlay_tick();
    } else if (mode == MODE_LOGIN) {
        screens_login_tick();
        overlay_tick();
    }
    wm_tick();
    beep_update();
}

static void start_session(void)
{
    mx = g_screen.w / 2;
    my = g_screen.h / 2;
    screens_boot(boot_work);
    g_sys.boot_ms = timer_ms();
    kprintf("shell: boot finished in %u ms\n", g_sys.boot_ms);
    mode = MODE_LOGIN;
    screens_login_reset(false);
    ui_invalidate_all();
    kprintf("login: shown\n");
}

#ifdef KEPLER_HOSTED
void shell_main(void)
{
    kprintf("video: %dx%d, %s\n", g_screen.w, g_screen.h, g_sys.video);
    start_session();
    uint32_t last_tick = 0;
    for (;;) {
        host_pump();
        input_event_t ev;
        while (input_poll(&ev)) {
            if (ev.type == EV_MOUSE)
                handle_mouse(&ev);
            else if (ev.type == EV_KEY)
                handle_key(&ev);
        }
        uint32_t now = timer_ms();
        if (now - last_tick >= 15) {
            last_tick = now;
            tick_all();
        }
        compose();
        host_sync();
        host_wait(15 - (int)MIN(15u, timer_ms() - last_tick));
    }
}
#else
void desktop_main(multiboot_info_t *mbi)
{
    if (!video_init(mbi))
        panic("no usable linear framebuffer");
    kprintf("video: %dx%d, %s\n", g_screen.w, g_screen.h, g_sys.video);
    start_session();

    uint32_t last_tick = 0;
    for (;;) {
        input_event_t ev;
        while (input_poll(&ev)) {
            if (ev.type == EV_MOUSE)
                handle_mouse(&ev);
            else if (ev.type == EV_KEY)
                handle_key(&ev);
        }
        net_poll();
        uint32_t now = timer_ms();
        if (now - last_tick >= 15) {
            last_tick = now;
            tick_all();
        }
        compose();

        cpu_cli();
        if (!input_pending()) {
            cpu_idle = 1;
            __asm__ volatile("sti; hlt");
            cpu_idle = 0;
        } else {
            cpu_sti();
        }
    }
}
#endif

void gui_panic(const char *msg)
{
    if (!g_screen.back) {
#ifndef KEPLER_HOSTED
        vga_text_init();
        vga_text_color(0x4F);
        vga_text_write("KeplerOS has stopped.\n\n");
        vga_text_write(msg);
#endif
        return;
    }
    gfx_t g = video_gfx();
    /* dim whatever was on screen and put the message on a dark panel */
    gfx_desaturate(&g, 0, 0, g_screen.w, g_screen.h, 200, 190);
    int pw = 760, ph = 300, px = (g_screen.w - pw) / 2, py = (g_screen.h - ph) / 2;
    gfx_round_rect(&g, px, py, pw, ph, 14, RGBA(28, 28, 30, 235), CORNER_ALL);
    gfx_blit_tint(&g, px + (pw - 48) / 2, py + 26, icon_get(GLYPH_POWER, 48), HEX(0xFFFFFF));
    gfx_text_centered(&g, &font_heading, px, py + 92, pw, "Your computer needs to restart because of a problem.", HEX(0xFFFFFF));
    gfx_text_centered(&g, &font_ui, px, py + 118, pw, "Press the power button, or restart the virtual machine.", HEX(0xB8B8BE));
    int x = px + 40, y = py + 156;
    const char *p = msg;
    while (*p && y < py + ph - 24) {
        const char *nl = strchr(p, '\n');
        int n = nl ? nl - p : (int)strlen(p);
        gfx_text_n(&g, &font_mono, x, y, p, n, HEX(0xE5E5EA));
        y += 19;
        p += n;
        if (*p == '\n')
            p++;
    }
    video_flush(rect_make(0, 0, g_screen.w, g_screen.h));
}
