/* The dock: pinned apps, running apps, minimised windows and the Trash, on a
 * frosted glass shelf at the bottom of the screen. */

#include "ramfs.h"
#include "ui.h"

#define ICON    48
#define SLOT    58
#define PAD     7
#define SEP_W   15
#define DOCK_H  (ICON + 2 * PAD + 4)
#define BOTTOM  6
#define RADIUS  17

enum { S_APP, S_RUNNING, S_SEP, S_MIN, S_TRASH, S_KEPLER, S_CLOCK };

#define CLOCK_W 92

typedef struct {
    int kind;
    const app_info_t *app; /* S_APP, and S_RUNNING when the app is known */
    window_t *win;         /* S_RUNNING, S_MIN */
    int x, w;              /* screen x and width */
} slot_t;

static slot_t slots[40];
static int n_slots;
static rect_t area;
static bool stale = true;
static int hover = -1, pressed = -1;
static int bounce = -1;
static uint32_t bounce_start;
static rect_t label_rect;

static bool app_running(const char *name)
{
    if (!strcmp(name, "Finder"))
        return true; /* Finder never quits */
    for (window_t *w = wm_first(); w; w = w->next)
        if (!w->dead && !strcmp(w->app, name))
            return true;
    return false;
}

static bool trash_full(void)
{
    fnode_t *bin = fs_lookup("/Recycled");
    return bin && bin->child;
}

static void build(void)
{
    n_slots = 0;
    slots[n_slots++] = (slot_t){ S_KEPLER, NULL, NULL, 0, SLOT };
    slots[n_slots++] = (slot_t){ S_SEP, NULL, NULL, 0, SEP_W };
    for (int i = 0; i < g_app_count; i++)
        if (g_apps[i].in_dock)
            slots[n_slots++] = (slot_t){ S_APP, &g_apps[i], NULL, 0, SLOT };
    /* running apps that are not pinned */
    for (window_t *w = wm_first(); w && n_slots < 30; w = w->next) {
        if (w->dead || (w->flags & WF_NO_TASKBAR) || w->owner)
            continue;
        const app_info_t *a = app_find(w->app);
        if (a && a->in_dock)
            continue;
        bool dup = false;
        for (int k = 0; k < n_slots; k++)
            if (slots[k].kind == S_RUNNING && !strcmp(slots[k].win->app, w->app))
                dup = true;
        if (!dup)
            slots[n_slots++] = (slot_t){ S_RUNNING, a, w, 0, SLOT };
    }
    slots[n_slots++] = (slot_t){ S_SEP, NULL, NULL, 0, SEP_W };
    for (window_t *w = wm_first(); w && n_slots < 38; w = w->next)
        if (!w->dead && (w->flags & WF_MINIMIZED) && !(w->flags & WF_NO_TASKBAR))
            slots[n_slots++] = (slot_t){ S_MIN, NULL, w, 0, SLOT };
    slots[n_slots++] = (slot_t){ S_TRASH, NULL, NULL, 0, SLOT };
    slots[n_slots++] = (slot_t){ S_SEP, NULL, NULL, 0, SEP_W };
    slots[n_slots++] = (slot_t){ S_CLOCK, NULL, NULL, 0, CLOCK_W };

    int total = 2 * PAD - (SLOT - ICON);
    for (int i = 0; i < n_slots; i++)
        total += slots[i].w;
    area = rect_make((g_screen.w - total) / 2, g_screen.h - DOCK_H - BOTTOM, total, DOCK_H);
    int x = area.x + PAD - (SLOT - ICON) / 2;
    for (int i = 0; i < n_slots; i++) {
        slots[i].x = x;
        x += slots[i].w;
    }
    stale = false;
}

rect_t dock_rect(void)
{
    if (stale)
        build();
    return area;
}

/* the dock plus its shadow and the room above it that bounces and labels use */
static rect_t extent(void) { return rect_make(area.x - 20, area.y - 40, area.w + 40, area.h + 40 + BOTTOM); }

void dock_changed(void)
{
    ui_invalidate(extent());
    stale = true;
    build();
    ui_invalidate(extent());
    hover = -1;
}

static void set_label(rect_t r)
{
    ui_invalidate(label_rect);
    label_rect = r;
    ui_invalidate(label_rect);
}

static const char *slot_name(const slot_t *s)
{
    switch (s->kind) {
    case S_APP: return s->app->name;
    case S_RUNNING: return s->win->app;
    case S_MIN: return s->win->title;
    case S_TRASH: return "Trash";
    case S_KEPLER: return "KeplerOS";
    case S_CLOCK: return "";
    }
    return "";
}

/* ---- painting -------------------------------------------------------------------------------- */

void dock_paint(gfx_t *g, rect_t dirty)
{
    if (stale)
        build();
    if (rect_empty(rect_intersect(dirty, extent())))
        return;
    rect_t a = area;
    gfx_shadow(g, a.x, a.y, a.w, a.h, RADIUS, 16, 6, 46);
    gfx_blur_behind(g, a.x, a.y, a.w, a.h, RADIUS, 12, RGBA(246, 246, 250, 92), CORNER_ALL);
    gfx_round_frame(g, a.x, a.y, a.w, a.h, RADIUS, RGBA(0, 0, 0, 40), CORNER_ALL);
    gfx_round_frame(g, a.x + 1, a.y + 1, a.w - 2, a.h - 2, RADIUS - 1, RGBA(255, 255, 255, 120), CORNER_ALL);

    int iy = a.y + PAD;
    for (int i = 0; i < n_slots; i++) {
        slot_t *s = &slots[i];
        int ix = s->x + (s->w - ICON) / 2;
        if (s->kind == S_SEP) {
            gfx_vline(g, s->x + s->w / 2, a.y + 10, a.h - 20, RGBA(0, 0, 0, 50));
            continue;
        }
        if (s->kind == S_KEPLER) {
            bool hot = hover == i || pressed == i;
            gfx_round_rect(g, ix + 4, iy + 4, ICON - 8, ICON - 8, 12, hot ? RGBA(255, 255, 255, 150) : RGBA(255, 255, 255, 90),
                           CORNER_ALL);
            gfx_blit_tint(g, ix + (ICON - 26) / 2, iy + (ICON - 26) / 2, icon_get(GLYPH_KEPLER, 26), HEX(0x1D1D1F));
            continue;
        }
        if (s->kind == S_CLOCK) {
            static const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
            static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
            datetime_t dt;
            rtc_read(&dt);
            char t[16], d[24];
            ksnprintf(t, sizeof t, "%d:%02d %s", dt.hour % 12 ? dt.hour % 12 : 12, dt.minute, dt.hour < 12 ? "AM" : "PM");
            ksnprintf(d, sizeof d, "%s %s %d", days[dt.weekday % 7], months[(dt.month + 11) % 12], dt.day);
            int cx = s->x, cw = s->w - 8;
            gfx_text_centered(g, &font_menu_bold, cx, iy + 6, cw, t, COL_TEXT);
            gfx_text_centered(g, &font_ui, cx, iy + 27, cw, d, HEX(0x3A3A3C));
            continue;
        }
        int dy = 0;
        if (bounce == i) {
            uint32_t t = timer_ms() - bounce_start;
            int phase = (int)(t % 360);
            int amp = t < 720 ? 14 : 0;
            dy = -(amp * (phase < 180 ? phase : 360 - phase) / 180);
        }
        const image_t *img = NULL;
        if (s->kind == S_APP)
            img = icon_get(s->app->icon, ICON);
        else if (s->kind == S_RUNNING)
            img = icon_get(s->app ? s->app->icon : s->win->icon, ICON);
        else if (s->kind == S_TRASH)
            img = icon_get(trash_full() ? ICON_TRASH_FULL : ICON_RECYCLE, ICON);
        if (s->kind == S_MIN) {
            const image_t *th = s->win->thumb;
            if (th) {
                gfx_shadow(g, ix + (ICON - th->w) / 2, iy + (ICON - th->h) / 2, th->w, th->h, 3, 4, 2, 50);
                gfx_blit_alpha(g, ix + (ICON - th->w) / 2, iy + (ICON - th->h) / 2, th, 255);
            }
            const app_info_t *ap = app_find(s->win->app);
            gfx_blit_alpha(g, ix + ICON - 20, iy + ICON - 20, icon_get(ap ? ap->icon : s->win->icon, 20), 255);
        } else if (img) {
            gfx_blit_alpha(g, ix, iy + dy, img, 255);
            if (pressed == i && hover == i)
                gfx_blit_tint(g, ix, iy + dy, img, RGBA(0, 0, 0, 90));
        }
        bool running = (s->kind == S_APP && app_running(s->app->name)) || s->kind == S_RUNNING;
        if (running)
            gfx_circle(g, s->x + s->w / 2, a.y + a.h - 3, 2, RGBA(0, 0, 0, 170));
    }

    /* the name of the hovered item, floating above it */
    if (hover >= 0 && hover < n_slots && slots[hover].kind != S_SEP && slots[hover].kind != S_CLOCK) {
        const char *name = slot_name(&slots[hover]);
        int tw = MIN(text_width(&font_menu, name), 300);
        int w = tw + 22, h = 24;
        int x = CLAMP(slots[hover].x + slots[hover].w / 2 - w / 2, 4, g_screen.w - w - 4), y = a.y - h - 10;
        gfx_round_rect(g, x, y + 1, w, h, 6, RGBA(0, 0, 0, 30), CORNER_ALL);
        gfx_round_rect(g, x, y, w, h, 6, RGBA(238, 238, 240, 245), CORNER_ALL);
        gfx_round_frame(g, x, y, w, h, 6, RGBA(0, 0, 0, 50), CORNER_ALL);
        gfx_text_clipped(g, &font_menu, x + 11, y + (h - font_menu.height) / 2, 300, name, COL_TEXT);
    }
}

/* ---- input ----------------------------------------------------------------------------------- */

static int slot_at(int x, int y)
{
    if (!rect_contains(area, x, y))
        return -1;
    for (int i = 0; i < n_slots; i++)
        if (x >= slots[i].x && x < slots[i].x + slots[i].w)
            return slots[i].kind == S_SEP ? -1 : i;
    return -1;
}

enum { DM_OPEN = 1, DM_QUIT, DM_EMPTY, DM_NEW_WINDOW };

static const menu_item_t app_open_menu[] = {
    { "Open", DM_OPEN, 0, 0, NULL, 0, NULL },
};
static const menu_item_t app_running_menu[] = {
    { "New Window", DM_NEW_WINDOW, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Quit", DM_QUIT, 0, 0, NULL, 0, NULL },
};
static const menu_item_t trash_menu[] = {
    { "Open", DM_OPEN, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Empty Trash", DM_EMPTY, 0, 0, NULL, 0, NULL },
};

static int menu_slot;
static char menu_app[32];

static void activate(int i)
{
    slot_t *s = &slots[i];
    switch (s->kind) {
    case S_APP: {
        if (!strcmp(s->app->cmd, "launchpad")) {
            launchpad_toggle();
            return;
        }
        launchpad_close();
        window_t *w = wm_app_window(s->app->name);
        if (w) {
            wm_focus(w);
        } else {
            bounce = i;
            bounce_start = timer_ms();
            ui_launch(s->app->cmd);
        }
        break;
    }
    case S_RUNNING:
    case S_MIN:
        launchpad_close();
        wm_focus(s->win);
        break;
    case S_TRASH:
        launchpad_close();
        ui_launch("trash");
        break;
    case S_KEPLER:
        launchpad_close();
        kepler_menu_open(s->x + 4, area.y - 8);
        break;
    case S_CLOCK:
        break;
    }
}

static void menu_cmd(int id, void *ctx)
{
    (void)ctx;
    switch (id) {
    case DM_OPEN:
        if (menu_slot >= 0 && menu_slot < n_slots)
            activate(menu_slot);
        break;
    case DM_NEW_WINDOW: {
        const app_info_t *a = app_find(menu_app);
        if (a)
            ui_launch(a->cmd);
        break;
    }
    case DM_QUIT:
        for (window_t *w = wm_first(); w; w = w->next)
            if (!w->dead && !strcmp(w->app, menu_app))
                wm_close(w);
        break;
    case DM_EMPTY: {
        fnode_t *bin = fs_lookup("/Recycled");
        while (bin && bin->child)
            fs_remove(bin->child);
        dock_changed();
        break;
    }
    }
}

bool dock_mouse(int kind, int x, int y, int button)
{
    if (stale)
        build();
    int h = kind == MS_LEAVE ? -1 : slot_at(x, y);
    if (h != hover) {
        hover = h;
        ui_invalidate(extent());
        set_label(h >= 0 ? rect_make(area.x - 160, area.y - 40, area.w + 320, 36) : rect_make(0, 0, 0, 0));
    }
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        pressed = h;
        ui_invalidate(area);
    } else if (kind == MS_UP && button == MOUSE_LEFT) {
        int p = pressed;
        pressed = -1;
        ui_invalidate(area);
        if (p >= 0 && p == h)
            activate(h);
    } else if (kind == MS_DOWN && button == MOUSE_RIGHT && h >= 0 && slots[h].kind != S_CLOCK && slots[h].kind != S_KEPLER) {
        slot_t *s = &slots[h];
        menu_slot = h;
        strlcpy(menu_app, slot_name(s), sizeof menu_app);
        const menu_item_t *items = app_open_menu;
        int n = ARRAY_LEN(app_open_menu);
        if (s->kind == S_TRASH) {
            items = trash_menu;
            n = ARRAY_LEN(trash_menu);
        } else if ((s->kind == S_APP && app_running(s->app->name) && strcmp(s->app->cmd, "launchpad")) ||
                   s->kind == S_RUNNING) {
            items = app_running_menu;
            n = ARRAY_LEN(app_running_menu);
            if (s->kind == S_RUNNING)
                strlcpy(menu_app, s->win->app, sizeof menu_app);
        }
        menu_open(items, n, s->x + 4, area.y - 8, menu_cmd, NULL);
        /* menus open upwards from the dock */
    }
    return true;
}

void dock_tick(void)
{
    if (bounce >= 0) {
        ui_invalidate(extent());
        if (timer_ms() - bounce_start > 760)
            bounce = -1;
    }
}
