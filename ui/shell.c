/*
 * The shell: owns the screen, the mouse pointer and the main loop.
 * Everything is redrawn through a single dirty rectangle: anything that
 * changes calls ui_invalidate(), and once the input queue is drained the
 * dirty area is recomposed back to front and copied to the framebuffer.
 */

#include "apps.h"
#include "jpeg.h"
#include "ramfs.h"
#include "ui.h"

char g_user_name[32] = "Kepler";

enum { MODE_BOOT, MODE_WELCOME, MODE_DESKTOP, MODE_STANDBY };

static int mode = MODE_BOOT;
static rect_t dirty;
static int mx, my, buttons;
static int cursor = CURSOR_ARROW;
static rect_t cursor_rect;

static uint32_t last_click_ms;
static int last_click_x, last_click_y;
static bool in_taskbar;
static bool win_held, win_combo;

static char tip_text[96];
static rect_t tip_rect;

static uint32_t beep_until;

extern volatile int cpu_idle;

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
    int w = text_width(&font_ui, tip_text) + 10, h = 19;
    x = CLAMP(x, 2, g_screen.w - w - 2);
    y = CLAMP(y, 2, g_screen.h - h - 2);
    tip_rect = rect_make(x, y, w + 2, h + 2);
    ui_invalidate(tip_rect);
}

static void tooltip_paint(gfx_t *g)
{
    if (!tip_text[0])
        return;
    rect_t r = tip_rect;
    gfx_fill(g, r.x + 2, r.y + 2, r.w - 2, r.h - 2, RGBA(0, 0, 0, 50));
    gfx_fill(g, r.x, r.y, r.w - 2, r.h - 2, HEX(0xFFFFE1));
    gfx_rect(g, r.x, r.y, r.w - 2, r.h - 2, HEX(0x000000));
    gfx_text(g, &font_ui, r.x + 5, r.y + 3, tip_text, HEX(0x000000));
}

static void compose(void)
{
    if (rect_empty(dirty))
        return;
    gfx_t g = video_gfx();
    g.clip = dirty;
    switch (mode) {
    case MODE_WELCOME:
        screens_welcome_paint(&g, dirty);
        overlay_paint(&g, dirty);
        break;
    case MODE_DESKTOP:
        desktop_paint(&g, dirty);
        wm_paint(&g, dirty);
        taskbar_paint(&g, dirty);
        startmenu_paint(&g);
        menu_paint(&g);
        tooltip_paint(&g);
        overlay_paint(&g, dirty);
        break;
    case MODE_STANDBY:
        gfx_fill(&g, 0, 0, g_screen.w, g_screen.h, HEX(0x000000));
        break;
    }
    if (mode != MODE_STANDBY)
        cursor_draw(&g, cursor, mx, my);
    cursor_rect = cursor_bounds(cursor, mx, my);
    video_flush(dirty);
    dirty = rect_make(0, 0, 0, 0);
}

/* ---- PC speaker --------------------------------------------------------------------- */

void ui_beep(int hz, int ms)
{
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
    if (beep_until && (int32_t)(timer_ms() - beep_until) >= 0) {
        outb(0x61, inb(0x61) & 0xFC);
        beep_until = 0;
    }
}

/* ---- launching -------------------------------------------------------------------------- */

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

    startmenu_close();
    if (!strcmp(name, "notepad") || !strcmp(name, "edit"))
        notepad_open(*arg ? arg : NULL);
    else if (!strcmp(name, "calc"))
        calc_open();
    else if (!strcmp(name, "cmd") || !strcmp(name, "terminal"))
        terminal_open();
    else if (!strcmp(name, "explorer"))
        explorer_open(*arg ? arg : "::computer");
    else if (!strcmp(name, "mycomputer"))
        explorer_open("::computer");
    else if (!strcmp(name, "mydocs"))
        explorer_open("/Documents");
    else if (!strcmp(name, "mypics"))
        explorer_open("/Documents/My Pictures");
    else if (!strcmp(name, "recycle"))
        explorer_open("/Recycled");
    else if (!strcmp(name, "paint") || !strcmp(name, "mspaint"))
        paint_open();
    else if (!strcmp(name, "mines") || !strcmp(name, "winmine") || !strcmp(name, "minesweeper"))
        mines_open();
    else if (!strcmp(name, "taskmgr"))
        taskmgr_open(*arg ? katoi(arg) : 0);
    else if (!strcmp(name, "about") || !strcmp(name, "winver"))
        about_open();
    else if (!strcmp(name, "display") || !strcmp(name, "control") || !strcmp(name, "desk.cpl"))
        display_open();
    else if (!strcmp(name, "viewer") || !strcmp(name, "pictures"))
        viewer_open(*arg ? arg : "/Documents/My Pictures/Kepler Earth.jpg");
    else if (!strcmp(name, "sysprops") || !strcmp(name, "sysdm.cpl"))
        sysprops_open();
    else if (!strcmp(name, "run"))
        run_dialog();
    else if (!strcmp(name, "shutdown"))
        overlay_open(0);
    else if (!strcmp(name, "logoff"))
        overlay_open(1);
    else {
        char msg[160];
        ksnprintf(msg, sizeof msg, "KeplerOS cannot find '%s'. Make sure you typed the name correctly, and then try again.",
                  name);
        msgbox(NULL, name, msg, ICON_ERROR);
    }
}

void ui_login(void)
{
    ui_set_cursor(CURSOR_ARROW);
    mode = MODE_DESKTOP;
    ui_invalidate_all();
    taskbar_changed();
}

void ui_logoff(void)
{
    startmenu_close();
    menu_close();
    for (window_t *w = wm_first(); w; w = w->next)
        wm_close(w);
    screens_welcome_reset();
    mode = MODE_WELCOME;
    ui_invalidate_all();
}

void ui_power(int action)
{
    if (action == 0) {
        startmenu_close();
        mode = MODE_STANDBY;
        ui_invalidate_all();
        return;
    }
    screens_goodbye(action);
}

bool ui_overlay_active(void) { return overlay_is_open(); }

/* ---- input routing ---------------------------------------------------------------------- */

static void route_mouse(int kind, int button)
{
    int x = mx, y = my;
    if (mode == MODE_WELCOME) {
        if (overlay_is_open())
            overlay_mouse(kind, x, y, button);
        else
            screens_welcome_mouse(kind, x, y, button);
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
    if (startmenu_is_open() && startmenu_mouse(kind, x, y, button))
        return;
    if (wm_dragging()) {
        wm_mouse(kind, x, y, buttons, button);
        return;
    }
    bool over_bar = y >= g_screen.h - TASKBAR_H;
    if (over_bar || in_taskbar) {
        if (!over_bar) {
            taskbar_mouse(MS_LEAVE, x, y, 0);
            in_taskbar = false;
        } else {
            in_taskbar = true;
            ui_set_cursor(CURSOR_ARROW);
            taskbar_mouse(kind, x, y, button);
            return;
        }
    }
    if (wm_mouse(kind, x, y, buttons, button))
        return;
    ui_set_cursor(CURSOR_ARROW);
    desktop_mouse(kind, x, y, button);
}

static void handle_mouse(const input_event_t *ev)
{
    if (mode == MODE_STANDBY) {
        if (ev->buttons)
            ui_login();
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

static bool global_key(const input_event_t *ev)
{
    uint8_t m = ev->mods;
    if (ev->key == KEY_LWIN) {
        if (ev->pressed) {
            win_held = true;
            win_combo = false;
        } else {
            if (win_held && !win_combo && !overlay_is_open())
                startmenu_toggle();
            win_held = false;
        }
        return true;
    }
    if (!ev->pressed)
        return false;
    if (win_held) {
        win_combo = true;
        switch (ktolower(ev->key)) {
        case 'd': wm_minimize_all(); return true;
        case 'e': ui_launch("mycomputer"); return true;
        case 'r': ui_launch("run"); return true;
        case 'm': wm_minimize_all(); return true;
        }
    }
    if ((m & MOD_CTRL) && (m & MOD_ALT) && (ev->key == KEY_DELETE || ev->key == '.')) {
        ui_launch("taskmgr");
        return true;
    }
    if ((m & MOD_CTRL) && ev->key == KEY_ESC) {
        startmenu_toggle();
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
    if (mode == MODE_STANDBY) {
        if (ev->pressed)
            ui_login();
        return;
    }
    if (mode == MODE_WELCOME) {
        if (overlay_is_open())
            overlay_key(ev);
        else
            screens_welcome_key(ev);
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
    if (startmenu_is_open()) {
        startmenu_key(ev);
        return;
    }
    if (wm_active())
        wm_key(ev);
    else
        desktop_key(ev);
}

/* ---- boot and main loop ------------------------------------------------------------------- */

extern const uint8_t wallpaper_jpg[], wallpaper_jpg_end[];

static void boot_work(void (*progress)(int pct))
{
    progress(5);
    fs_init();
    image_t *src;
    if (!jpeg_decode(wallpaper_jpg, wallpaper_jpg_end - wallpaper_jpg, &src))
        panic("could not decode the wallpaper");
    progress(25);
    image_t *wall = image_cover(src, g_screen.w, g_screen.h - 0);
    progress(55);
    image_sharpen(wall, 90);
    progress(65);
    image_free(src);
    icons_set_user_picture(wall);
    desktop_init(wall);
    progress(70);
    icons_prerender(NULL);
    progress(100);
}

void desktop_main(multiboot_info_t *mbi)
{
    if (!video_init(mbi))
        panic("no usable linear framebuffer");
    kprintf("video: %dx%d, %s\n", g_screen.w, g_screen.h, g_sys.video);
    mx = g_screen.w / 2;
    my = g_screen.h / 2;

    screens_boot(boot_work);
    g_sys.boot_ms = timer_ms();
    kprintf("shell: boot finished in %u ms\n", g_sys.boot_ms);

    mode = MODE_WELCOME;
    screens_welcome_reset();
    ui_invalidate_all();

    uint32_t last_tick = 0;
    for (;;) {
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
            if (mode == MODE_DESKTOP) {
                taskbar_tick();
                overlay_tick();
            } else if (mode == MODE_WELCOME) {
                screens_welcome_tick();
                overlay_tick();
            }
            wm_tick();
            beep_update();
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

void gui_panic(const char *msg)
{
    if (!g_screen.back) {
        vga_text_init();
        vga_text_color(0x4F);
        vga_text_write("KeplerOS has stopped.\n\n");
        vga_text_write(msg);
        return;
    }
    gfx_t g = video_gfx();
    gfx_fill(&g, 0, 0, g_screen.w, g_screen.h, HEX(0x0B3D91));
    int x = 120, y = 140;
    gfx_text(&g, &font_logo_small, x, y, "KeplerOS", HEX(0xFFFFFF));
    y += 70;
    gfx_text(&g, &font_big, x, y, "A problem has been detected and KeplerOS has been shut down", HEX(0xFFFFFF));
    y += 30;
    gfx_text(&g, &font_big, x, y, "to prevent damage to your computer.", HEX(0xFFFFFF));
    y += 60;
    const char *p = msg;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int n = nl ? nl - p : (int)strlen(p);
        gfx_text_n(&g, &font_mono, x, y, p, n, HEX(0xFFFFFF));
        y += 20;
        p += n;
        if (*p == '\n')
            p++;
    }
    y += 30;
    gfx_text(&g, &font_ui, x, y, "Restart your computer. If this screen appears again, check the serial log for details.", HEX(0xC8D8FF));
    video_flush(rect_make(0, 0, g_screen.w, g_screen.h));
}
