/* The menu bar across the top of the screen: the Kepler menu, the frontmost
 * app's name and menus, status icons and the clock. */

#include "ui.h"

#define LOGO_X  10
#define LOGO_W  34
#define APP_X   (LOGO_X + LOGO_W)
#define ITEM_PAD 20

enum { H_NONE = -1, H_LOGO = 0, H_APP = 1, H_MENU0 = 2, H_WIFI = 100, H_SOUND, H_SEARCH, H_CONTROL, H_CLOCK };

static int open_idx = H_NONE;
static const menu_item_t *open_items;
static bool dark;
static int last_minute = -1;

bool topbar_dark(void) { return dark; }

void topbar_changed(void) { ui_invalidate(rect_make(0, 0, g_screen.w, TOPBAR_H)); }

void topbar_background_changed(void)
{
    const image_t *v = ui_vibrancy();
    dark = v && image_luma(v, 0, 0, g_screen.w, TOPBAR_H * 3) < 100;
    topbar_changed();
}

/* ---- what the bar shows ------------------------------------------------------------------- */

static window_t *owner(void) { return wm_menu_owner(); }

static const char *app_name(void)
{
    window_t *w = owner();
    return w ? w->app : "Finder";
}

static menubar_t *app_menus(void)
{
    window_t *w = owner();
    if (!w)
        return &desktop_menus;
    return w->menus;
}

static int menu_count(void)
{
    menubar_t *mb = app_menus();
    return mb ? mb->n : 0;
}

static rect_t item_rect(int h)
{
    if (h == H_LOGO)
        return rect_make(LOGO_X, 0, LOGO_W - 4, TOPBAR_H);
    int x = APP_X;
    int w = text_width(&font_menu_bold, app_name()) + ITEM_PAD;
    if (h == H_APP)
        return rect_make(x, 0, w, TOPBAR_H);
    x += w;
    menubar_t *mb = app_menus();
    for (int i = 0; mb && i < mb->n; i++) {
        int iw = text_width(&font_menu, mb->entries[i].title) + ITEM_PAD;
        if (h == H_MENU0 + i)
            return rect_make(x, 0, iw, TOPBAR_H);
        x += iw;
    }
    return rect_make(0, 0, 0, 0);
}

static void clock_text(char *out, int n)
{
    static const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    datetime_t dt;
    rtc_read(&dt);
    int h12 = dt.hour % 12 ? dt.hour % 12 : 12;
    ksnprintf(out, n, "%s %s %d  %d:%02d %s", days[dt.weekday % 7], months[(dt.month + 11) % 12], dt.day, h12, dt.minute,
              dt.hour < 12 ? "AM" : "PM");
}

static rect_t clock_rect(void)
{
    char t[40];
    clock_text(t, sizeof t);
    int w = text_width(&font_menu, t) + 16;
    return rect_make(g_screen.w - 6 - w, 0, w, TOPBAR_H);
}

/* status icons, right to left before the clock */
static const int status_glyph[4] = { GLYPH_CONTROL, GLYPH_SEARCH, GLYPH_SPEAKER, GLYPH_WIFI };
static const int status_hit[4] = { H_CONTROL, H_SEARCH, H_SOUND, H_WIFI };

static rect_t status_rect(int i)
{
    rect_t c = clock_rect();
    return rect_make(c.x - (i + 1) * 30, 0, 30, TOPBAR_H);
}

/* ---- painting ------------------------------------------------------------------------------ */

void topbar_paint(gfx_t *g, rect_t dirty)
{
    int W = g_screen.w;
    if (rect_empty(rect_intersect(dirty, rect_make(0, 0, W, TOPBAR_H))))
        return;
    gfx_backdrop(g, 0, 0, W, TOPBAR_H, 0, ui_vibrancy(), dark ? RGBA(24, 24, 28, 120) : RGBA(250, 250, 252, 150), 0);
    gfx_hline(g, 0, TOPBAR_H - 1, W, dark ? RGBA(0, 0, 0, 60) : RGBA(0, 0, 0, 22));
    uint32_t tc = dark ? HEX(0xFFFFFF) : HEX(0x101012);
    uint32_t hi = dark ? RGBA(255, 255, 255, 60) : RGBA(0, 0, 0, 36);
    bool open = open_items && menu_current() == open_items;
    int ty = (TOPBAR_H - font_menu.height) / 2;

    rect_t lr = item_rect(H_LOGO);
    if (open && open_idx == H_LOGO)
        gfx_round_rect(g, lr.x, 2, lr.w, TOPBAR_H - 4, 4, hi, CORNER_ALL);
    gfx_blit_tint(g, lr.x + (lr.w - 16) / 2, (TOPBAR_H - 16) / 2, icon_get(GLYPH_KEPLER, 16), tc);

    rect_t ar = item_rect(H_APP);
    if (open && open_idx == H_APP)
        gfx_round_rect(g, ar.x, 2, ar.w, TOPBAR_H - 4, 4, hi, CORNER_ALL);
    gfx_text(g, &font_menu_bold, ar.x + ITEM_PAD / 2, ty, app_name(), tc);

    menubar_t *mb = app_menus();
    for (int i = 0; mb && i < mb->n; i++) {
        rect_t r = item_rect(H_MENU0 + i);
        if (open && open_idx == H_MENU0 + i)
            gfx_round_rect(g, r.x, 2, r.w, TOPBAR_H - 4, 4, hi, CORNER_ALL);
        gfx_text(g, &font_menu, r.x + ITEM_PAD / 2, ty, mb->entries[i].title, tc);
    }

    for (int i = 0; i < 4; i++) {
        rect_t r = status_rect(i);
        if (open && open_idx == status_hit[i])
            gfx_round_rect(g, r.x + 2, 2, r.w - 4, TOPBAR_H - 4, 4, hi, CORNER_ALL);
        int sz = status_glyph[i] == GLYPH_SEARCH ? 14 : 16;
        gfx_blit_tint(g, r.x + (r.w - sz) / 2, (TOPBAR_H - sz) / 2, icon_get(status_glyph[i], sz), tc);
    }
    char t[40];
    clock_text(t, sizeof t);
    rect_t cr = clock_rect();
    gfx_text(g, &font_menu, cr.x + 8, ty, t, tc);
}

/* ---- menus ----------------------------------------------------------------------------------- */

enum {
    K_ABOUT = 1,
    K_PREFS,
    K_FORCE,
    K_SLEEP,
    K_RESTART,
    K_SHUTDOWN,
    K_LOCK,
    K_LOGOUT,
    K_APP_ABOUT,
    K_APP_HIDE,
    K_APP_HIDE_OTHERS,
    K_APP_QUIT,
    K_PREFS_DISPLAY,
    K_ACTIVITY,
};

static char logout_label[48];
static menu_item_t kepler_menu[] = {
    { "About This Kepler", K_ABOUT, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "System Preferences...", K_PREFS, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Force Quit...", K_FORCE, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Sleep", K_SLEEP, 0, 0, NULL, 0, NULL },
    { "Restart...", K_RESTART, 0, 0, NULL, 0, NULL },
    { "Shut Down...", K_SHUTDOWN, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Lock Screen", K_LOCK, 0, 0, NULL, 0, "Ctrl+L" },
    { logout_label, K_LOGOUT, 0, 0, NULL, 0, NULL },
};

static char about_label[64], hide_label[64], quit_label[64];
static menu_item_t app_menu[] = {
    { about_label, K_APP_ABOUT, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Preferences...", K_PREFS_DISPLAY, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { hide_label, K_APP_HIDE, 0, 0, NULL, 0, "Ctrl+H" },
    { "Hide Others", K_APP_HIDE_OTHERS, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { quit_label, K_APP_QUIT, 0, 0, NULL, 0, "Ctrl+Q" },
};

static const menu_item_t wifi_menu[] = {
    { "Wi-Fi: On", 0, MI_DISABLED, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "KeplerNet", 0, MI_CHECK, 0, NULL, 0, NULL },
};
static const menu_item_t sound_menu[] = {
    { "Sound", 0, MI_DISABLED, 0, NULL, 0, NULL },
    { "PC Speaker", 0, MI_CHECK, 0, NULL, 0, NULL },
};
static const menu_item_t control_menu[] = {
    { "Desktop & Screen Saver...", K_PREFS_DISPLAY, 0, 0, NULL, 0, NULL },
    { "Activity Monitor", K_ACTIVITY, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Lock Screen", K_LOCK, 0, 0, NULL, 0, NULL },
};

static void quit_app(const char *app)
{
    for (window_t *w = wm_first(); w; w = w->next)
        if (!w->dead && !strcmp(w->app, app))
            wm_close(w);
}

static void command(int id, void *ctx)
{
    (void)ctx;
    char app[32];
    strlcpy(app, app_name(), sizeof app);
    switch (id) {
    case K_ABOUT: ui_launch("about"); break;
    case K_PREFS:
    case K_PREFS_DISPLAY: ui_launch("display"); break;
    case K_FORCE:
    case K_ACTIVITY: ui_launch("taskmgr"); break;
    case K_SLEEP: ui_power(0); break;
    case K_RESTART: overlay_open(2); break;
    case K_SHUTDOWN: overlay_open(0); break;
    case K_LOCK: ui_lock(); break;
    case K_LOGOUT: overlay_open(1); break;
    case K_APP_ABOUT: ui_launch("about"); break;
    case K_APP_HIDE: wm_hide_app(app); break;
    case K_APP_HIDE_OTHERS:
        for (window_t *w = wm_first(); w; w = w->next)
            if (!w->dead && strcmp(w->app, app))
                wm_minimize(w);
        break;
    case K_APP_QUIT:
        if (strcmp(app, "Finder"))
            quit_app(app);
        break;
    }
}

static void open_menu(int h);

/* dragging along the bar with a menu open switches menus */
static void slide(int x, int y)
{
    (void)y;
    for (int h = H_LOGO; h < H_MENU0 + menu_count(); h++) {
        rect_t r = item_rect(h);
        if (x >= r.x && x < r.x + r.w) {
            if (h != open_idx)
                open_menu(h);
            return;
        }
    }
}

static void open_menu(int h)
{
    rect_t bar = rect_make(0, 0, g_screen.w, TOPBAR_H);
    rect_t r = h >= H_WIFI ? status_rect(h == H_CONTROL ? 0 : h == H_SEARCH ? 1 : h == H_SOUND ? 2 : 3) : item_rect(h);
    const menu_item_t *items = NULL;
    int n = 0;
    menu_cb cb = command;
    void *ctx = NULL;
    if (h == H_LOGO) {
        ksnprintf(logout_label, sizeof logout_label, "Log Out %s...", g_user_name);
        items = kepler_menu;
        n = ARRAY_LEN(kepler_menu);
    } else if (h == H_APP) {
        const char *app = app_name();
        ksnprintf(about_label, sizeof about_label, "About %s", app);
        ksnprintf(hide_label, sizeof hide_label, "Hide %s", app);
        ksnprintf(quit_label, sizeof quit_label, "Quit %s", app);
        app_menu[ARRAY_LEN(app_menu) - 1].flags = strcmp(app, "Finder") ? 0 : MI_DISABLED;
        items = app_menu;
        n = ARRAY_LEN(app_menu);
    } else if (h >= H_MENU0 && h < H_MENU0 + menu_count()) {
        menubar_t *mb = app_menus();
        window_t *w = owner();
        items = mb->entries[h - H_MENU0].items;
        n = mb->entries[h - H_MENU0].n;
        cb = w ? w->menu_fn : desktop_menu_command;
        ctx = w ? w->menu_ctx : NULL;
    } else if (h == H_WIFI) {
        items = wifi_menu;
        n = ARRAY_LEN(wifi_menu);
    } else if (h == H_SOUND) {
        items = sound_menu;
        n = ARRAY_LEN(sound_menu);
    } else if (h == H_CONTROL) {
        items = control_menu;
        n = ARRAY_LEN(control_menu);
    }
    if (!items)
        return;
    menu_open_bar(items, n, r.x, TOPBAR_H, cb, ctx, bar, slide);
    open_idx = h;
    open_items = items;
    topbar_changed();
}

/* ---- input ------------------------------------------------------------------------------------ */

static int hit(int x, int y)
{
    if (y < 0 || y >= TOPBAR_H)
        return H_NONE;
    for (int h = H_LOGO; h < H_MENU0 + menu_count(); h++) {
        rect_t r = item_rect(h);
        if (x >= r.x && x < r.x + r.w)
            return h;
    }
    for (int i = 0; i < 4; i++) {
        rect_t r = status_rect(i);
        if (x >= r.x && x < r.x + r.w)
            return status_hit[i];
    }
    rect_t c = clock_rect();
    if (x >= c.x && x < c.x + c.w)
        return H_CLOCK;
    return H_NONE;
}

bool topbar_mouse(int kind, int x, int y, int button)
{
    int h = kind == MS_LEAVE ? H_NONE : hit(x, y);
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        if (h == H_SEARCH) {
            spotlight_open();
        } else if (h == H_CLOCK) {
            datetime_t dt;
            rtc_read(&dt);
            static const char *days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
            static const char *months[] = { "January", "February", "March", "April", "May", "June", "July",
                                             "August", "September", "October", "November", "December" };
            char tip[64];
            ksnprintf(tip, sizeof tip, "%s, %s %d, %d", days[dt.weekday % 7], months[(dt.month + 11) % 12], dt.day, dt.year);
            ui_tooltip(tip, g_screen.w - text_width(&font_ui, tip) - 24, TOPBAR_H + 4);
        } else if (h != H_NONE) {
            launchpad_close();
            open_menu(h);
        }
    }
    return true;
}

void topbar_tick(void)
{
    datetime_t dt;
    rtc_read(&dt);
    if (dt.minute != last_minute) {
        last_minute = dt.minute;
        topbar_changed();
    }
}
