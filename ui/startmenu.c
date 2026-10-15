/* The two-column Start menu. */

#include "ui.h"

#define SM_W      410
#define SM_H      506
#define HEAD_H    62
#define FOOT_H    44
#define LEFT_W    212
#define SEP_H     9

enum { F_BOLD = 1, F_SEP = 2, F_PINNED = 4 };

typedef struct {
    const char *title, *subtitle;
    int icon;
    const char *cmd;
    int flags;
} sm_item_t;

static const sm_item_t left_items[] = {
    { "Command Prompt", "Kepler terminal", ICON_CMD, "cmd", F_BOLD | F_PINNED },
    { "Notepad", "Text editor", ICON_NOTEPAD, "notepad", F_BOLD | F_PINNED },
    { NULL, NULL, 0, NULL, F_SEP },
    { "Calculator", NULL, ICON_CALC, "calc", 0 },
    { "Paint", NULL, ICON_PAINT, "paint", 0 },
    { "Minesweeper", NULL, ICON_MINES, "mines", 0 },
    { "Picture Viewer", NULL, ICON_PICTURES, "viewer", 0 },
    { "Task Manager", NULL, ICON_TASKMGR, "taskmgr", 0 },
};

static const sm_item_t right_items[] = {
    { "My Documents", NULL, ICON_DOCUMENTS, "mydocs", F_BOLD },
    { "My Pictures", NULL, ICON_PICTURES, "mypics", F_BOLD },
    { "My Computer", NULL, ICON_COMPUTER, "mycomputer", F_BOLD },
    { NULL, NULL, 0, NULL, F_SEP },
    { "Control Panel", NULL, ICON_DISPLAY, "display", 0 },
    { "System Information", NULL, ICON_SYSINFO, "sysprops", 0 },
    { NULL, NULL, 0, NULL, F_SEP },
    { "Help and Support", NULL, ICON_HELP, "about", 0 },
    { "Run...", NULL, ICON_RUN, "run", 0 },
};

enum {
    AP_CALC = 1,
    AP_CMD,
    AP_NOTEPAD,
    AP_PAINT,
    AP_VIEWER,
    AP_MINES,
    AP_TASKMGR,
    AP_DISPLAY,
    AP_ABOUT,
    AP_EXPLORER,
};

static const menu_item_t games_menu[] = {
    { "Minesweeper", AP_MINES, 0, ICON_MINES, NULL, 0, NULL },
};

static const menu_item_t accessories_menu[] = {
    { "Calculator", AP_CALC, 0, ICON_CALC, NULL, 0, NULL },
    { "Command Prompt", AP_CMD, 0, ICON_CMD, NULL, 0, NULL },
    { "Notepad", AP_NOTEPAD, 0, ICON_NOTEPAD, NULL, 0, NULL },
    { "Paint", AP_PAINT, 0, ICON_PAINT, NULL, 0, NULL },
    { "Picture Viewer", AP_VIEWER, 0, ICON_PICTURES, NULL, 0, NULL },
    { "Kepler Explorer", AP_EXPLORER, 0, ICON_FOLDER, NULL, 0, NULL },
};

static const menu_item_t all_programs[] = {
    { "Accessories", 0, 0, ICON_FOLDER, accessories_menu, ARRAY_LEN(accessories_menu), NULL },
    { "Games", 0, 0, ICON_FOLDER, games_menu, ARRAY_LEN(games_menu), NULL },
    { "System Tools", 0, 0, ICON_FOLDER, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Task Manager", AP_TASKMGR, 0, ICON_TASKMGR, NULL, 0, NULL },
    { "Display Properties", AP_DISPLAY, 0, ICON_DISPLAY, NULL, 0, NULL },
    { "About KeplerOS", AP_ABOUT, 0, ICON_KEPLER, NULL, 0, NULL },
};

static bool open;
static int hover = -1; /* 0..: left items, 100..: right, 200 all programs, 300 log off, 301 turn off */

static int sm_x(void) { return 0; }
static int sm_y(void) { return g_screen.h - TASKBAR_H - SM_H; }
static rect_t sm_rect(void) { return rect_make(sm_x(), sm_y(), SM_W + 4, SM_H + 4); }

bool startmenu_is_open(void) { return open; }

void startmenu_close(void)
{
    if (!open)
        return;
    open = false;
    hover = -1;
    menu_close();
    ui_invalidate(sm_rect());
    taskbar_changed();
}

void startmenu_toggle(void)
{
    if (open) {
        startmenu_close();
        return;
    }
    open = true;
    hover = -1;
    menu_close();
    ui_tooltip(NULL, 0, 0);
    ui_invalidate(sm_rect());
    taskbar_changed();
}

/* ---- layout ------------------------------------------------------------------------------- */

static int left_h(const sm_item_t *it) { return (it->flags & F_SEP) ? SEP_H : (it->flags & F_PINNED) ? 46 : 40; }
static int right_h(const sm_item_t *it) { return (it->flags & F_SEP) ? SEP_H : 32; }

static int body_top(void) { return sm_y() + HEAD_H + 2; }
static int body_bottom(void) { return sm_y() + SM_H - FOOT_H; }

static rect_t left_item_rect(int i)
{
    int y = body_top() + 6;
    for (int k = 0; k < i; k++)
        y += left_h(&left_items[k]);
    return rect_make(sm_x() + 2, y, LEFT_W - 4, left_h(&left_items[i]));
}

static rect_t right_item_rect(int i)
{
    int y = body_top() + 8;
    for (int k = 0; k < i; k++)
        y += right_h(&right_items[k]);
    return rect_make(sm_x() + LEFT_W + 3, y, SM_W - LEFT_W - 6, right_h(&right_items[i]));
}

static rect_t all_programs_rect(void) { return rect_make(sm_x() + 2, body_bottom() - 38, LEFT_W - 4, 32); }

static rect_t footer_button(int which)
{
    int y = sm_y() + SM_H - FOOT_H + 6;
    if (which == 0)
        return rect_make(sm_x() + SM_W - 238, y, 84, 32);
    return rect_make(sm_x() + SM_W - 150, y, 144, 32);
}

/* ---- painting ------------------------------------------------------------------------------- */

static void paint_hot(gfx_t *g, rect_t r) { gfx_fill(g, r.x, r.y, r.w, r.h, COL_MENU_HI); }

void startmenu_paint(gfx_t *g)
{
    if (!open)
        return;
    int x0 = sm_x(), y0 = sm_y();

    /* shadow on the right */
    gfx_fill(g, x0 + SM_W, y0 + 6, 3, SM_H - 6, RGBA(0, 0, 0, 60));
    gfx_fill(g, x0 + SM_W + 3, y0 + 8, 1, SM_H - 8, RGBA(0, 0, 0, 25));

    /* frame + header */
    static const uint32_t head_c[] = { HEX(0x5E9AF2), HEX(0x3C82EC), HEX(0x1D63D9), HEX(0x1A55CB), HEX(0x1349B9) };
    static const int head_s[] = { 0, 20, 90, 200, 255 };
    gfx_round_rect(g, x0, y0, SM_W, SM_H, 7, HEX(0x1C4FB6), CORNER_TOP);
    gfx_round_rect_stops(g, x0 + 1, y0 + 1, SM_W - 2, HEAD_H, 6, head_c, head_s, 5, CORNER_TOP);
    gfx_hline(g, x0 + 6, y0 + 1, SM_W - 12, RGBA(255, 255, 255, 110));

    const image_t *pic = icon_user_picture(48);
    gfx_round_rect(g, x0 + 9, y0 + 6, 52, 52, 5, HEX(0xFFFFFF), CORNER_ALL);
    if (pic)
        gfx_blit_alpha(g, x0 + 11, y0 + 8, pic, 255);
    gfx_text(g, &font_big_bold, x0 + 72, y0 + 18, g_user_name, RGBA(0, 0, 0, 110));
    gfx_text(g, &font_big_bold, x0 + 71, y0 + 17, g_user_name, HEX(0xFFFFFF));

    /* orange accent line */
    int ly = y0 + HEAD_H + 1;
    for (int i = 0; i < SM_W - 2; i++) {
        int edge = MIN(i, SM_W - 3 - i);
        int a = MIN(255, edge * 6);
        gfx_pixel(g, x0 + 1 + i, ly, color_lerp(HEX(0x2A62D2), HEX(0xE88E32), a));
        gfx_pixel(g, x0 + 1 + i, ly + 1, color_lerp(HEX(0x2A62D2), HEX(0xF6C27A), a));
    }

    /* columns */
    int by = body_top(), bh = body_bottom() - by;
    gfx_fill(g, x0 + 1, by, LEFT_W, bh, HEX(0xFFFFFF));
    gfx_fill(g, x0 + LEFT_W + 1, by, SM_W - LEFT_W - 2, bh, HEX(0xD3E5FA));
    gfx_vline(g, x0 + LEFT_W + 1, by, bh, HEX(0x95BDEE));

    for (int i = 0; i < ARRAY_LEN(left_items); i++) {
        const sm_item_t *it = &left_items[i];
        rect_t r = left_item_rect(i);
        if (it->flags & F_SEP) {
            gfx_hline(g, r.x + 8, r.y + SEP_H / 2, r.w - 16, HEX(0xDCDCDC));
            continue;
        }
        bool hot = hover == i;
        if (hot)
            paint_hot(g, r);
        gfx_blit_alpha(g, r.x + 6, r.y + (r.h - 32) / 2, icon_get(it->icon, 32), 255);
        uint32_t tc = hot ? HEX(0xFFFFFF) : HEX(0x000000);
        if (it->flags & F_PINNED) {
            gfx_text(g, &font_ui_bold, r.x + 46, r.y + 8, it->title, tc);
            gfx_text(g, &font_ui, r.x + 46, r.y + 23, it->subtitle, hot ? HEX(0xDDE8FF) : HEX(0x6D6D6D));
        } else {
            gfx_text(g, &font_ui, r.x + 46, r.y + (r.h - font_ui.height) / 2 + 1, it->title, tc);
        }
    }

    /* All Programs */
    rect_t ap = all_programs_rect();
    gfx_hline(g, ap.x + 8, ap.y - 6, ap.w - 16, HEX(0xDCDCDC));
    bool ap_hot = hover == 200;
    if (ap_hot)
        paint_hot(g, ap);
    int apw = text_width(&font_ui_bold, "All Programs");
    int apx = ap.x + (ap.w - apw - 30) / 2;
    gfx_text(g, &font_ui_bold, apx, ap.y + (ap.h - font_ui_bold.height) / 2 + 1, "All Programs",
             ap_hot ? HEX(0xFFFFFF) : HEX(0x000000));
    gfx_blit_alpha(g, apx + apw + 10, ap.y + 4, icon_get(ICON_ALLPROGS, 24), 255);

    for (int i = 0; i < ARRAY_LEN(right_items); i++) {
        const sm_item_t *it = &right_items[i];
        rect_t r = right_item_rect(i);
        if (it->flags & F_SEP) {
            gfx_hline(g, r.x + 6, r.y + SEP_H / 2, r.w - 12, HEX(0xA9C7EF));
            continue;
        }
        bool hot = hover == 100 + i;
        if (hot)
            paint_hot(g, r);
        gfx_blit_alpha(g, r.x + 6, r.y + (r.h - 24) / 2, icon_get(it->icon, 24), 255);
        const font_t *f = (it->flags & F_BOLD) ? &font_ui_bold : &font_ui;
        gfx_text(g, f, r.x + 38, r.y + (r.h - f->height) / 2 + 1, it->title, hot ? HEX(0xFFFFFF) : HEX(0x0A246A));
    }

    /* footer */
    static const uint32_t foot_c[] = { HEX(0x4F8EEB), HEX(0x2F6FE0), HEX(0x1D58CC), HEX(0x1747B2) };
    static const int foot_s[] = { 0, 25, 160, 255 };
    int fy = y0 + SM_H - FOOT_H;
    gfx_vgrad_stops(g, x0 + 1, fy, SM_W - 2, FOOT_H, foot_c, foot_s, 4);
    gfx_hline(g, x0 + 1, fy, SM_W - 2, RGBA(255, 255, 255, 90));
    static const char *labels[2] = { "Log Off", "Turn Off Computer" };
    static const int icons[2] = { ICON_LOGOFF, ICON_POWER };
    for (int i = 0; i < 2; i++) {
        rect_t r = footer_button(i);
        if (hover == 300 + i) {
            gfx_round_rect(g, r.x, r.y, r.w, r.h, 4, RGBA(255, 255, 255, 50), CORNER_ALL);
            gfx_round_frame(g, r.x, r.y, r.w, r.h, 4, RGBA(255, 255, 255, 80), CORNER_ALL);
        }
        gfx_blit_alpha(g, r.x + 4, r.y + 4, icon_get(icons[i], 24), 255);
        gfx_text(g, &font_ui, r.x + 33, r.y + 10, labels[i], RGBA(0, 0, 0, 90));
        gfx_text(g, &font_ui, r.x + 32, r.y + 9, labels[i], HEX(0xFFFFFF));
    }
}

/* ---- input ---------------------------------------------------------------------------------- */

static int hit(int x, int y)
{
    for (int i = 0; i < ARRAY_LEN(left_items); i++)
        if (!(left_items[i].flags & F_SEP) && rect_contains(left_item_rect(i), x, y))
            return i;
    for (int i = 0; i < ARRAY_LEN(right_items); i++)
        if (!(right_items[i].flags & F_SEP) && rect_contains(right_item_rect(i), x, y))
            return 100 + i;
    if (rect_contains(all_programs_rect(), x, y))
        return 200;
    for (int i = 0; i < 2; i++)
        if (rect_contains(footer_button(i), x, y))
            return 300 + i;
    return -1;
}

static void all_programs_action(int id, void *ctx)
{
    static const char *cmds[] = { [AP_CALC] = "calc", [AP_CMD] = "cmd", [AP_NOTEPAD] = "notepad",
                                  [AP_PAINT] = "paint", [AP_VIEWER] = "viewer", [AP_MINES] = "mines",
                                  [AP_TASKMGR] = "taskmgr", [AP_DISPLAY] = "display", [AP_ABOUT] = "about",
                                  [AP_EXPLORER] = "mydocs" };
    if (id > 0 && id < ARRAY_LEN(cmds) && cmds[id])
        ui_launch(cmds[id]);
    startmenu_close();
}

static void activate(int h)
{
    if (h < 0)
        return;
    if (h < 100) {
        ui_launch(left_items[h].cmd);
    } else if (h < 200) {
        ui_launch(right_items[h - 100].cmd);
    } else if (h == 200) {
        rect_t r = all_programs_rect();
        menu_open(all_programs, ARRAY_LEN(all_programs), r.x + r.w + 4, r.y - 120, all_programs_action, NULL);
        return;
    } else if (h == 300) {
        startmenu_close();
        overlay_open(1);
    } else if (h == 301) {
        startmenu_close();
        overlay_open(0);
    }
    startmenu_close();
}

bool startmenu_mouse(int kind, int x, int y, int button)
{
    rect_t r = rect_make(sm_x(), sm_y(), SM_W, SM_H);
    bool inside = rect_contains(r, x, y);
    if (kind == MS_MOVE) {
        int h = inside ? hit(x, y) : -1;
        if (h != hover) {
            hover = h;
            ui_invalidate(sm_rect());
            if (h == 200)
                activate(200);
        }
        if (inside)
            ui_set_cursor(CURSOR_ARROW);
        return inside;
    }
    if (kind == MS_DOWN) {
        if (!inside) {
            /* the Start button toggles; anything else just closes the menu */
            if (rect_contains(taskbar_start_button(), x, y)) {
                startmenu_close();
                return true;
            }
            startmenu_close();
            return true;
        }
        return true;
    }
    if (kind == MS_UP && inside) {
        activate(hit(x, y));
        return true;
    }
    return inside;
}

bool startmenu_key(const input_event_t *ev)
{
    if (ev->key == KEY_ESC) {
        startmenu_close();
        return true;
    }
    if (ev->key == KEY_DOWN || ev->key == KEY_UP) {
        int dir = ev->key == KEY_DOWN ? 1 : -1;
        int n = ARRAY_LEN(left_items);
        int h = hover >= 0 && hover < n ? hover : (dir > 0 ? -1 : n);
        do {
            h += dir;
        } while (h >= 0 && h < n && (left_items[h].flags & F_SEP));
        hover = h < 0 || h >= n ? -1 : h;
        ui_invalidate(sm_rect());
        return true;
    }
    if (ev->key == KEY_ENTER && hover >= 0) {
        activate(hover);
        return true;
    }
    return true;
}
