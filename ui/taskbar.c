/* The taskbar: Start button, quick launch, window buttons and the notification area. */

#include "ui.h"

#define START_W     104
#define QL_X        (START_W + 8)
#define QL_ICONS    3
#define TASKS_X     (QL_X + QL_ICONS * 24 + 12)
#define TRAY_W      118

static const struct {
    int icon;
    const char *cmd;
    const char *tip;
} quick[QL_ICONS] = {
    { ICON_SHOWDESKTOP, "::desktop", "Show Desktop" },
    { ICON_CMD, "cmd", "Command Prompt" },
    { ICON_NOTEPAD, "notepad", "Notepad" },
};

static int hover = -1; /* 0 start, 100+i quick launch, 200+i task button, 300 clock */
static int pressed = -1;
static int last_minute = -1;
static uint32_t hover_since;
static bool tip_shown;

static int bar_y(void) { return g_screen.h - TASKBAR_H; }

rect_t taskbar_start_button(void) { return rect_make(0, bar_y(), START_W, TASKBAR_H); }

static rect_t tray_rect(void) { return rect_make(g_screen.w - TRAY_W, bar_y(), TRAY_W, TASKBAR_H); }

static void invalidate_bar(void) { ui_invalidate(rect_make(0, bar_y(), g_screen.w, TASKBAR_H)); }

void taskbar_changed(void) { invalidate_bar(); }

/* task buttons in z-order of creation */
static int task_windows(window_t **out, int max)
{
    int n = 0;
    window_t *all[64];
    int count = 0;
    for (window_t *w = wm_first(); w && count < 64; w = w->next)
        if (!w->dead && !(w->flags & WF_NO_TASKBAR) && !w->owner)
            all[count++] = w;
    /* stable order: by id */
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++)
            if (all[j]->id < all[i]->id) {
                window_t *t = all[i];
                all[i] = all[j];
                all[j] = t;
            }
    for (int i = 0; i < count && n < max; i++)
        out[n++] = all[i];
    return n;
}

static int task_button_w(int n)
{
    int avail = g_screen.w - TRAY_W - TASKS_X - 6;
    if (n == 0)
        return 160;
    return MIN(160, avail / n - 3);
}

static rect_t task_button(int i, int n) { return rect_make(TASKS_X + i * (task_button_w(n) + 3), bar_y() + 3, task_button_w(n), 24); }

/* ---- painting ------------------------------------------------------------------------------ */

static const uint32_t bar_colors[] = { HEX(0x3168D5), HEX(0x4A92EC), HEX(0x3A81E8), HEX(0x2863DC),
                                       HEX(0x245EDB), HEX(0x215AD6), HEX(0x1D50C6), HEX(0x1941A5) };
static const int bar_stops[] = { 0, 9, 26, 52, 128, 205, 236, 255 };

static void paint_start(gfx_t *g, int y)
{
    bool open = startmenu_is_open();
    bool hot = hover == 0;
    static const int stops[] = { 0, 12, 40, 130, 215, 255 };
    uint32_t c[6] = { HEX(0x4E9E4E), HEX(0x8BD68B), HEX(0x4DAE4D), HEX(0x3C9C3C), HEX(0x2F8C2F), HEX(0x2A7A2A) };
    if (hot && !open)
        for (int i = 0; i < 6; i++)
            c[i] = color_shade(c[i], 116);
    if (open)
        for (int i = 0; i < 6; i++)
            c[i] = color_shade(c[i], 78);
    /* square on the left, pill-shaped on the right */
    gfx_t sg = gfx_sub(g, 0, y, START_W, TASKBAR_H);
    gfx_round_rect_stops(&sg, -20, 0, START_W + 20, TASKBAR_H, 13, c, stops, 6, CORNER_ALL);
    /* right edge shading */
    gfx_round_frame(&sg, -20, 0, START_W + 20, TASKBAR_H, 13, RGBA(0, 0, 0, 60), CORNER_ALL);
    gfx_hline(g, 0, y, START_W - 12, RGBA(255, 255, 255, 50));

    const image_t *logo = icon_get(ICON_KEPLER, 24);
    gfx_blit_alpha(g, 7, y + 3, logo, 255);
    int tx = 35, ty = y + (TASKBAR_H - font_start.height) / 2;
    gfx_text(g, &font_start, tx + 1, ty + 1, "start", RGBA(0, 0, 0, 120));
    gfx_text(g, &font_start, tx, ty, "start", HEX(0xFFFFFF));
}

static void paint_grip(gfx_t *g, int x, int y)
{
    for (int i = 0; i < 6; i++) {
        gfx_fill(g, x + 1, y + 5 + i * 3 + 1, 2, 2, RGBA(0, 0, 0, 70));
        gfx_fill(g, x, y + 5 + i * 3, 2, 2, RGBA(255, 255, 255, 110));
    }
}

static void paint_task(gfx_t *g, window_t *w, rect_t r, bool hot, bool down)
{
    bool act = w == wm_active() && !(w->flags & WF_MINIMIZED);
    uint32_t top = HEX(0x5D9DF7), bot = HEX(0x3A7EEC), edge = HEX(0x2F67CF);
    if (hot) {
        top = HEX(0x7CB3FF);
        bot = HEX(0x4C90F5);
    }
    if (act || down) {
        top = HEX(0x1C4FB5);
        bot = HEX(0x2257C2);
        edge = HEX(0x173E92);
    }
    gfx_round_rect(g, r.x, r.y, r.w, r.h, 3, edge, CORNER_ALL);
    gfx_round_rect_vgrad(g, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 2, top, bot, CORNER_ALL);
    if (!(act || down))
        gfx_hline(g, r.x + 3, r.y + 1, r.w - 6, RGBA(255, 255, 255, 80));
    else
        gfx_hline(g, r.x + 3, r.y + 1, r.w - 6, RGBA(0, 0, 0, 60));
    gfx_blit_alpha(g, r.x + 6, r.y + 4, icon_get(w->icon, 16), 255);
    gfx_text_clipped(g, &font_ui, r.x + 27, r.y + 6, r.w - 33, w->title, HEX(0xFFFFFF));
}

static void paint_tray(gfx_t *g, int y)
{
    rect_t t = tray_rect();
    static const uint32_t c[] = { HEX(0x1B6FCE), HEX(0x2AA6F6), HEX(0x169AEC), HEX(0x0F87E0), HEX(0x0A72CF), HEX(0x0E5CB6) };
    static const int s[] = { 0, 10, 40, 140, 230, 255 };
    gfx_vgrad_stops(g, t.x, y, t.w, TASKBAR_H, c, s, 6);
    gfx_vline(g, t.x, y, TASKBAR_H, HEX(0x0F4CAD));
    gfx_vline(g, t.x + 1, y + 1, TASKBAR_H - 1, RGBA(255, 255, 255, 90));

    gfx_blit_alpha(g, t.x + 10, y + 7, icon_get(ICON_NETWORK, 16), 255);
    gfx_blit_alpha(g, t.x + 32, y + 7, icon_get(ICON_SPEAKER, 16), 255);

    datetime_t dt;
    rtc_read(&dt);
    char clock[16];
    int h12 = dt.hour % 12 ? dt.hour % 12 : 12;
    ksnprintf(clock, sizeof clock, "%d:%02d %s", h12, dt.minute, dt.hour < 12 ? "AM" : "PM");
    int cw = text_width(&font_ui, clock);
    gfx_text(g, &font_ui, g_screen.w - cw - 12, y + (TASKBAR_H - font_ui.height) / 2 + 1, clock, HEX(0xFFFFFF));
}

void taskbar_paint(gfx_t *g, rect_t dirty)
{
    int y = bar_y();
    if (rect_empty(rect_intersect(dirty, rect_make(0, y, g_screen.w, TASKBAR_H))))
        return;
    gfx_vgrad_stops(g, 0, y, g_screen.w, TASKBAR_H, bar_colors, bar_stops, 8);
    paint_start(g, y);

    paint_grip(g, START_W + 2, y);
    for (int i = 0; i < QL_ICONS; i++) {
        int x = QL_X + i * 24;
        if (hover == 100 + i) {
            gfx_round_rect(g, x - 2, y + 4, 22, 22, 3, RGBA(255, 255, 255, pressed == 100 + i ? 40 : 70), CORNER_ALL);
            gfx_round_frame(g, x - 2, y + 4, 22, 22, 3, RGBA(255, 255, 255, 120), CORNER_ALL);
        }
        gfx_blit_alpha(g, x + 1, y + 7, icon_get(quick[i].icon, 16), 255);
    }
    paint_grip(g, TASKS_X - 8, y);

    window_t *ws[32];
    int n = task_windows(ws, 32);
    for (int i = 0; i < n; i++)
        paint_task(g, ws[i], task_button(i, n), hover == 200 + i, pressed == 200 + i);

    paint_tray(g, y);
}

/* ---- input ------------------------------------------------------------------------------------ */

static int hit(int x, int y)
{
    if (y < bar_y())
        return -1;
    if (x < START_W)
        return 0;
    for (int i = 0; i < QL_ICONS; i++)
        if (x >= QL_X - 2 + i * 24 && x < QL_X + 20 + i * 24)
            return 100 + i;
    window_t *ws[32];
    int n = task_windows(ws, 32);
    for (int i = 0; i < n; i++)
        if (rect_contains(task_button(i, n), x, y))
            return 200 + i;
    rect_t t = tray_rect();
    if (x >= g_screen.w - 70 && rect_contains(t, x, y))
        return 300;
    return -1;
}

bool taskbar_mouse(int kind, int x, int y, int button)
{
    int h = kind == MS_LEAVE ? -1 : hit(x, y);
    if (h != hover) {
        hover = h;
        hover_since = timer_ms();
        if (tip_shown) {
            ui_tooltip(NULL, 0, 0);
            tip_shown = false;
        }
        invalidate_bar();
    }
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        pressed = h;
        invalidate_bar();
        if (h == 0) {
            startmenu_toggle();
            pressed = -1;
        }
        return true;
    }
    if (kind == MS_UP && button == MOUSE_LEFT) {
        int p = pressed;
        pressed = -1;
        invalidate_bar();
        if (p != h)
            return true;
        if (h >= 100 && h < 100 + QL_ICONS) {
            const char *cmd = quick[h - 100].cmd;
            if (!strcmp(cmd, "::desktop"))
                wm_minimize_all();
            else
                ui_launch(cmd);
        } else if (h >= 200 && h < 300) {
            window_t *ws[32];
            int n = task_windows(ws, 32);
            if (h - 200 < n) {
                window_t *w = ws[h - 200];
                if (w == wm_active() && !(w->flags & WF_MINIMIZED))
                    wm_minimize(w);
                else
                    wm_focus(w);
            }
        }
        return true;
    }
    if (kind == MS_DOWN && button == MOUSE_RIGHT && h >= 200 && h < 300) {
        window_t *ws[32];
        int n = task_windows(ws, 32);
        if (h - 200 < n)
            wm_focus(ws[h - 200]);
    }
    return true;
}

void taskbar_tick(void)
{
    datetime_t dt;
    rtc_read(&dt);
    if (dt.minute != last_minute) {
        last_minute = dt.minute;
        ui_invalidate(tray_rect());
    }
    if (!tip_shown && hover >= 0 && timer_ms() - hover_since > 600 && !startmenu_is_open()) {
        char tip[96];
        tip[0] = 0;
        if (hover == 300) {
            static const char *days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
            static const char *months[] = { "January", "February", "March", "April", "May", "June", "July",
                                             "August", "September", "October", "November", "December" };
            ksnprintf(tip, sizeof tip, "%s, %s %d, %d", days[dt.weekday], months[(dt.month + 11) % 12], dt.day, dt.year);
        } else if (hover >= 100 && hover < 100 + QL_ICONS) {
            strlcpy(tip, quick[hover - 100].tip, sizeof tip);
        } else if (hover == 0) {
            strlcpy(tip, "Click here to begin", sizeof tip);
        } else if (hover >= 200 && hover < 300) {
            window_t *ws[32];
            int n = task_windows(ws, 32);
            if (hover - 200 < n)
                strlcpy(tip, ws[hover - 200]->title, sizeof tip);
        }
        if (tip[0]) {
            int x = ui_mouse_x(), w = text_width(&font_ui, tip) + 12;
            if (hover == 300)
                x = g_screen.w - w - 4;
            ui_tooltip(tip, x, bar_y() - 24);
            tip_shown = true;
        }
    }
}
