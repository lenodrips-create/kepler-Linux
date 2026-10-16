/* Task Manager: applications, processes and live CPU / memory graphs. */

#include "apps.h"

#define HIST 128

static const char *const tm_tabs[] = { "Applications", "Processes", "Performance" };

typedef struct {
    int tab;
    uint8_t cpu[HIST], mem[HIST];
    int n;
    int sel;
    int scroll_phase;
    button_t items[3];
    buttons_t btns;
    menubar_t mb;
} tm_t;

static int page_x(void) { return 7; }
static int page_y(void) { return MENUBAR_H + 30; }

/* ---- data --------------------------------------------------------------------------------------------- */

static int list_windows(window_t **out, int max)
{
    int n = 0;
    for (window_t *w = wm_first(); w && n < max; w = w->next)
        if (!w->dead && !(w->flags & WF_NO_TASKBAR))
            out[n++] = w;
    return n;
}

static const char *image_name(window_t *w)
{
    switch (w->icon) {
    case ICON_NOTEPAD: return "notepad.exe";
    case ICON_CALC: return "calc.exe";
    case ICON_CMD: return "cmd.exe";
    case ICON_PAINT: return "mspaint.exe";
    case ICON_MINES: return "winmine.exe";
    case ICON_TASKMGR: return "taskmgr.exe";
    case ICON_PICTURES: return "viewer.exe";
    case ICON_DISPLAY: return "desk.cpl";
    case ICON_KEPLER: return "winver.exe";
    case ICON_SYSINFO: return "sysdm.cpl";
    default: return "explorer.exe";
    }
}

static void sample(tm_t *t)
{
    if (t->n == HIST) {
        memmove(t->cpu, t->cpu + 1, HIST - 1);
        memmove(t->mem, t->mem + 1, HIST - 1);
        t->n--;
    }
    t->cpu[t->n] = (uint8_t)MIN(100, cpu_busy_percent());
    t->mem[t->n] = (uint8_t)(heap_used() * 100 / heap_total());
    t->n++;
    t->scroll_phase = (t->scroll_phase + 3) % 12;
}

/* ---- painting ----------------------------------------------------------------------------------------- */

static void list_header(gfx_t *g, int x, int y, int w, const char **cols, const int *xs, int n)
{
    gfx_vgrad(g, x, y, w, 20, HEX(0xFFFFFF), HEX(0xEBEADB));
    gfx_hline(g, x, y + 19, w, HEX(0xD6D2C2));
    gfx_hline(g, x, y + 18, w, HEX(0xE2DECD));
    for (int i = 0; i < n; i++) {
        gfx_text(g, &font_ui, x + xs[i] + 6, y + 4, cols[i], HEX(0x000000));
        if (i)
            gfx_vline(g, x + xs[i], y + 3, 14, HEX(0xC7C5B2));
    }
}

static void paint_apps(tm_t *t, gfx_t *g, int x, int y, int w, int h)
{
    draw_textbox(g, x, y, w, h);
    static const char *cols[] = { "Task", "Status" };
    int xs[] = { 0, w - 110 };
    list_header(g, x + 1, y + 1, w - 2, cols, xs, 2);
    window_t *ws[32];
    int n = list_windows(ws, 32);
    int ry = y + 24;
    for (int i = 0; i < n; i++) {
        bool sel = i == t->sel;
        if (sel)
            gfx_fill(g, x + 2, ry - 1, w - 4, 18, COL_SELECT);
        uint32_t tc = sel ? HEX(0xFFFFFF) : HEX(0x000000);
        gfx_blit_alpha(g, x + 6, ry, icon_get(ws[i]->icon, 16), 255);
        gfx_text_clipped(g, &font_ui, x + 28, ry + 2, w - 150, ws[i]->title, tc);
        gfx_text(g, &font_ui, x + w - 104, ry + 2, "Running", tc);
        ry += 18;
    }
}

static void paint_procs(tm_t *t, gfx_t *g, int x, int y, int w, int h)
{
    draw_textbox(g, x, y, w, h);
    static const char *cols[] = { "Image Name", "User Name", "CPU", "Mem Usage" };
    int xs[] = { 0, 130, 220, 270 };
    list_header(g, x + 1, y + 1, w - 2, cols, xs, 4);
    window_t *ws[32];
    int n = list_windows(ws, 32);
    int ry = y + 24;
    char buf[32];
    int busy = t->n ? t->cpu[t->n - 1] : 0;
    for (int i = -3; i < n; i++) {
        const char *name, *user;
        int cpu, mem_k;
        if (i == -3) {
            name = "System Idle Process";
            user = "SYSTEM";
            cpu = 100 - busy;
            mem_k = 16;
        } else if (i == -2) {
            name = "kernel";
            user = "SYSTEM";
            cpu = busy / 3;
            mem_k = (int)(heap_used() / 1024 / 3);
        } else if (i == -1) {
            name = "shell.exe";
            user = g_user_name;
            cpu = busy - busy / 3;
            mem_k = 9 * 1024 + 412;
        } else {
            name = image_name(ws[i]);
            user = g_user_name;
            cpu = 0;
            mem_k = 1200 + (ws[i]->w * ws[i]->h) * 4 / 1024 / 4 + ws[i]->id * 37;
        }
        bool sel = i == t->sel;
        if (sel)
            gfx_fill(g, x + 2, ry - 1, w - 4, 17, COL_SELECT);
        uint32_t tc = sel ? HEX(0xFFFFFF) : HEX(0x000000);
        gfx_text(g, &font_ui, x + 7, ry + 1, name, tc);
        gfx_text(g, &font_ui, x + 137, ry + 1, user, tc);
        ksnprintf(buf, sizeof buf, "%02d", CLAMP(cpu, 0, 99));
        gfx_text(g, &font_ui, x + 250 - text_width(&font_ui, buf), ry + 1, buf, tc);
        ksnprintf(buf, sizeof buf, "%d,%03d K", mem_k / 1000, mem_k % 1000);
        if (mem_k < 1000)
            ksnprintf(buf, sizeof buf, "%d K", mem_k);
        gfx_text(g, &font_ui, x + w - 12 - text_width(&font_ui, buf), ry + 1, buf, tc);
        ry += 17;
    }
}

static void meter(gfx_t *g, int x, int y, int w, int h, int pct, const char *label, uint32_t on)
{
    gfx_fill(g, x, y, w, h, HEX(0x000000));
    int bars = (h - 26) / 3;
    int lit = bars * pct / 100;
    for (int i = 0; i < bars; i++) {
        int by = y + h - 22 - i * 3;
        uint32_t c = i < lit ? on : HEX(0x00480B);
        gfx_hline(g, x + w / 2 - 15, by, 13, c);
        gfx_hline(g, x + w / 2 + 2, by, 13, c);
    }
    gfx_text_centered(g, &font_ui, x, y + h - 17, w, label, on);
}

static void graph(gfx_t *g, int x, int y, int w, int h, const uint8_t *data, int n, int phase, uint32_t c)
{
    gfx_fill(g, x, y, w, h, HEX(0x000000));
    gfx_t gg = gfx_sub(g, x, y, w, h);
    for (int gx = w - 1 - phase; gx >= 0; gx -= 12)
        gfx_vline(&gg, gx, 0, h, HEX(0x008040));
    for (int gy = h - 1; gy >= 0; gy -= 12)
        gfx_hline(&gg, 0, gy, w, HEX(0x008040));
    int step = 3;
    for (int i = n - 1; i > 0; i--) {
        int x1 = w - 1 - (n - 1 - i) * step, x0 = x1 - step;
        if (x1 < 0)
            break;
        int y1 = h - 1 - data[i] * (h - 2) / 100, y0 = h - 1 - data[i - 1] * (h - 2) / 100;
        gfx_line(&gg, x0, y0, x1, y1, c);
    }
}

static void paint_perf(tm_t *t, gfx_t *g, int x, int y, int w, int h)
{
    int cpu = t->n ? t->cpu[t->n - 1] : 0;
    int mem = t->n ? t->mem[t->n - 1] : 0;
    char buf[64];
    int lw = 84, gh = 100;
    draw_groupbox(g, x, y, lw, gh + 22, "CPU Usage");
    ksnprintf(buf, sizeof buf, "%d %%", cpu);
    meter(g, x + 8, y + 18, lw - 16, gh - 2, cpu, buf, HEX(0x00FF00));
    draw_groupbox(g, x + lw + 8, y, w - lw - 8, gh + 22, "CPU Usage History");
    graph(g, x + lw + 16, y + 18, w - lw - 24, gh - 2, t->cpu, t->n, t->scroll_phase, HEX(0x00FF00));

    int y2 = y + gh + 30;
    draw_groupbox(g, x, y2, lw, gh + 22, "Mem Usage");
    ksnprintf(buf, sizeof buf, "%u MB", (unsigned)(heap_used() >> 20));
    meter(g, x + 8, y2 + 18, lw - 16, gh - 2, mem, buf, HEX(0x00FF00));
    draw_groupbox(g, x + lw + 8, y2, w - lw - 8, gh + 22, "Memory Usage History");
    graph(g, x + lw + 16, y2 + 18, w - lw - 24, gh - 2, t->mem, t->n, t->scroll_phase, HEX(0xFFFF00));

    int y3 = y2 + gh + 30;
    int bw = (w - 8) / 2;
    draw_groupbox(g, x, y3, bw, 76, "Totals");
    window_t *ws[32];
    int procs = list_windows(ws, 32) + 3;
    ksnprintf(buf, sizeof buf, "%d", procs * 37 + 211);
    gfx_text(g, &font_ui, x + 10, y3 + 18, "Handles", HEX(0x000000));
    gfx_text(g, &font_ui, x + bw - 10 - text_width(&font_ui, buf), y3 + 18, buf, HEX(0x000000));
    ksnprintf(buf, sizeof buf, "%d", procs + 4);
    gfx_text(g, &font_ui, x + 10, y3 + 34, "Threads", HEX(0x000000));
    gfx_text(g, &font_ui, x + bw - 10 - text_width(&font_ui, buf), y3 + 34, buf, HEX(0x000000));
    ksnprintf(buf, sizeof buf, "%d", procs);
    gfx_text(g, &font_ui, x + 10, y3 + 50, "Processes", HEX(0x000000));
    gfx_text(g, &font_ui, x + bw - 10 - text_width(&font_ui, buf), y3 + 50, buf, HEX(0x000000));

    int x4 = x + bw + 8;
    draw_groupbox(g, x4, y3, bw, 76, "Physical Memory (K)");
    const char *labels[3] = { "Total", "Available", "Kernel heap" };
    uint32_t vals[3] = { g_sys.mem_kb, (uint32_t)((heap_total() - heap_used()) / 1024), (uint32_t)(heap_used() / 1024) };
    for (int i = 0; i < 3; i++) {
        ksnprintf(buf, sizeof buf, "%u", vals[i]);
        gfx_text(g, &font_ui, x4 + 10, y3 + 18 + i * 16, labels[i], HEX(0x000000));
        gfx_text(g, &font_ui, x4 + bw - 10 - text_width(&font_ui, buf), y3 + 18 + i * 16, buf, HEX(0x000000));
    }
}

static void tm_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    tm_t *t = w->data;
    menubar_paint(&t->mb, g, cw);
    int px = page_x(), py = page_y() - 22, pw = cw - 14, ph = ch - py - 26;
    draw_tab_panel(g, px, py + TABS_H / 2, pw, ph - TABS_H / 2);
    draw_tabs(g, px, py, pw, tm_tabs, 3, t->tab);
    int ix = px + 10, iy = py + 34, iw = pw - 20, ih = ph - 34 - 10;
    switch (t->tab) {
    case 0:
        paint_apps(t, g, ix, iy, iw, ih - 34);
        break;
    case 1:
        paint_procs(t, g, ix, iy, iw, ih - 34);
        break;
    case 2:
        paint_perf(t, g, ix, iy - 6, iw, ih);
        break;
    }
    if (t->tab != 2)
        buttons_paint(&t->btns, g);

    window_t *ws[32];
    int procs = list_windows(ws, 32) + 3;
    char status[128];
    ksnprintf(status, sizeof status, "Processes: %d      CPU Usage: %d%%      Commit Charge: %uK / %uK", procs,
              t->n ? t->cpu[t->n - 1] : 0, (unsigned)(heap_used() / 1024), (unsigned)(heap_total() / 1024));
    draw_statusbar(g, ch - 22, cw, status);
}

/* ---- input ----------------------------------------------------------------------------------------------- */

enum { TM_NEWTASK = 1, TM_EXIT, TM_ABOUT, TM_ONTOP };

static const menu_item_t file_menu[] = {
    { "New Task (Run...)", TM_NEWTASK, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Exit Task Manager", TM_EXIT, 0, 0, NULL, 0, NULL },
};
static const menu_item_t options_menu[] = {
    { "Always On Top", 0, MI_DISABLED, 0, NULL, 0, NULL },
};
static const menu_item_t view_menu[] = {
    { "Refresh Now", 0, 0, 0, NULL, 0, "F5" },
    { "Update Speed", 0, MI_DISABLED, 0, NULL, 0, NULL },
};
static const menu_item_t help_menu[] = {
    { "About Task Manager", TM_ABOUT, 0, 0, NULL, 0, NULL },
};
static const menubar_entry_t bar[] = {
    { "File", file_menu, ARRAY_LEN(file_menu) },
    { "Options", options_menu, ARRAY_LEN(options_menu) },
    { "View", view_menu, ARRAY_LEN(view_menu) },
    { "Help", help_menu, ARRAY_LEN(help_menu) },
};

static void set_buttons(window_t *w)
{
    tm_t *t = w->data;
    rect_t c = wm_client_rect(w);
    int by = c.h - 22 - 26 - 30;
    if (t->tab == 0) {
        t->items[0] = (button_t){ c.w - 268, by, 80, 23, "End Task", 1, 0 };
        t->items[1] = (button_t){ c.w - 182, by, 80, 23, "Switch To", 2, 0 };
        t->items[2] = (button_t){ c.w - 96, by, 80, 23, "New Task...", 3, 0 };
        t->btns.n = 3;
    } else {
        t->items[0] = (button_t){ c.w - 116, by, 96, 23, "End Process", 4, 0 };
        t->btns.n = 1;
    }
}

static void command(int id, void *ctx)
{
    window_t *w = ctx;
    if (!wm_alive(w))
        return;
    switch (id) {
    case TM_NEWTASK: ui_launch("run"); break;
    case TM_EXIT: wm_close(w); break;
    case TM_ABOUT: msgbox(w, "About Task Manager", "KeplerOS Task Manager\nVersion " KEPLER_VERSION, ICON_TASKMGR); break;
    }
}

static void tm_mouse(window_t *w, int kind, int x, int y, int button)
{
    tm_t *t = w->data;
    if (menubar_mouse(&t->mb, w, kind, x, y, command, w))
        return;
    if (kind == MS_DOWN) {
        int tab = tabs_hit(page_x(), page_y() - 22, wm_client_rect(w).w - 14, tm_tabs, 3, x, y);
        if (tab >= 0) {
            t->tab = tab;
            t->sel = -1;
            set_buttons(w);
            wm_invalidate_client(w);
            return;
        }
        if (t->tab < 2) {
            int row_y = page_y() - 22 + 34 + 24;
            int row_h = t->tab == 0 ? 18 : 17;
            int idx = (y - row_y) / row_h;
            if (y >= row_y)
                t->sel = t->tab == 1 ? idx - 3 : idx;
            wm_invalidate_client(w);
        }
    }
    if (t->tab == 2)
        return;
    int id = buttons_mouse(&t->btns, w, kind, x, y);
    window_t *ws[32];
    int n = list_windows(ws, 32);
    window_t *target = t->sel >= 0 && t->sel < n ? ws[t->sel] : NULL;
    if (id == 1 || id == 4) {
        if (target && target != w)
            wm_close(target);
        t->sel = -1;
    } else if (id == 2 && target) {
        wm_focus(target);
    } else if (id == 3) {
        ui_launch("run");
    }
}

static void tm_tick(window_t *w)
{
    sample(w->data);
    wm_invalidate_client(w);
}

static void tm_close(window_t *w) { kfree(w->data); }

static const window_ops_t tm_ops = { tm_paint, NULL, tm_mouse, tm_tick, tm_close, NULL };

window_t *taskmgr_open(int tab)
{
    window_t *w = wm_find(&tm_ops);
    if (w) {
        wm_focus(w);
        return w;
    }
    tm_t *t = kzalloc(sizeof *t);
    t->tab = CLAMP(tab, 0, 2);
    t->sel = -1;
    t->btns = (buttons_t){ t->items, 0, -1, -1 };
    t->mb.entries = bar;
    t->mb.n = ARRAY_LEN(bar);
    t->mb.open = t->mb.hover = -1;
    w = wm_create("Activity Monitor", ICON_TASKMGR, 420, 500, &tm_ops, t, WF_NO_MAX);
    wm_set_app(w, "Activity Monitor", &t->mb, command, w);
    set_buttons(w);
    sample(t);
    wm_set_tick(w, 500);
    return w;
}
