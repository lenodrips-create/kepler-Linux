/* Popup menus (context menus, menu bars, submenus) in the XP style. */

#include "ui.h"

#define ITEM_H   22
#define SEP_H    9
#define MAX_LEVELS 3

typedef struct {
    const menu_item_t *items;
    int n;
    rect_t r;
    int hover;
} level_t;

static level_t levels[MAX_LEVELS];
static int depth;
static menu_cb callback;
static void *cb_ctx;
static rect_t bar_area; /* clicks here belong to the owning menu bar */
static void (*bar_hook)(int x, int y);

static int item_height(const menu_item_t *it) { return (it->flags & MI_SEP) ? SEP_H : ITEM_H; }

static rect_t level_rect(const menu_item_t *items, int n, int x, int y)
{
    int w = 150;
    for (int i = 0; i < n; i++) {
        if (items[i].flags & MI_SEP)
            continue;
        const font_t *f = (items[i].flags & MI_BOLD) ? &font_ui_bold : &font_ui;
        int iw = 30 + text_width(f, items[i].label) + 40;
        if (items[i].shortcut)
            iw += text_width(&font_ui, items[i].shortcut) + 20;
        w = MAX(w, iw);
    }
    int h = 6;
    for (int i = 0; i < n; i++)
        h += item_height(&items[i]);
    if (x + w > g_screen.w)
        x = g_screen.w - w - 2;
    if (y + h > g_screen.h - TASKBAR_H)
        y = MAX(0, y - h);
    return rect_make(x, y, w, h);
}

static void invalidate_level(int i)
{
    rect_t r = levels[i].r;
    ui_invalidate(rect_make(r.x, r.y, r.w + 4, r.h + 4));
}

static void open_level(int lvl, const menu_item_t *items, int n, int x, int y)
{
    for (int i = lvl; i < depth; i++)
        invalidate_level(i);
    depth = lvl + 1;
    levels[lvl].items = items;
    levels[lvl].n = n;
    levels[lvl].r = level_rect(items, n, x, y);
    levels[lvl].hover = -1;
    invalidate_level(lvl);
}

void menu_open(const menu_item_t *items, int n, int x, int y, menu_cb cb, void *ctx)
{
    menu_close();
    callback = cb;
    cb_ctx = ctx;
    bar_area = rect_make(0, 0, 0, 0);
    bar_hook = NULL;
    open_level(0, items, n, x, y);
}

void menu_close(void)
{
    for (int i = 0; i < depth; i++)
        invalidate_level(i);
    depth = 0;
}

bool menu_is_open(void) { return depth > 0; }

static void paint_level(gfx_t *g, level_t *lv)
{
    rect_t r = lv->r;
    /* soft drop shadow */
    gfx_fill(g, r.x + 4, r.y + r.h, r.w, 2, RGBA(0, 0, 0, 40));
    gfx_fill(g, r.x + r.w, r.y + 4, 2, r.h - 2, RGBA(0, 0, 0, 40));
    gfx_fill(g, r.x + 3, r.y + r.h, r.w, 1, RGBA(0, 0, 0, 30));
    gfx_fill(g, r.x, r.y, r.w, r.h, HEX(0xFFFFFF));
    gfx_rect(g, r.x, r.y, r.w, r.h, HEX(0xACA899));

    int y = r.y + 3;
    for (int i = 0; i < lv->n; i++) {
        const menu_item_t *it = &lv->items[i];
        if (it->flags & MI_SEP) {
            gfx_hline(g, r.x + 2, y + SEP_H / 2, r.w - 4, HEX(0xACA899));
            y += SEP_H;
            continue;
        }
        bool hot = lv->hover == i;
        bool dis = it->flags & MI_DISABLED;
        uint32_t tc = dis ? COL_GRAYTEXT : HEX(0x000000);
        if (hot && !dis) {
            gfx_fill(g, r.x + 2, y, r.w - 4, ITEM_H, COL_MENU_HI);
            tc = HEX(0xFFFFFF);
        } else if (hot) {
            gfx_fill(g, r.x + 2, y, r.w - 4, ITEM_H, HEX(0xE8E8E8));
        }
        if (it->icon >= 0 && it->icon != 0 && !(it->flags & (MI_CHECK | MI_RADIO)))
            gfx_blit_alpha(g, r.x + 5, y + 3, icon_get(it->icon, 16), dis ? 110 : 255);
        if (it->flags & MI_CHECK) {
            int cx = r.x + 9, cy = y + 7;
            for (int k = 0; k < 3; k++) {
                gfx_fill(g, cx + k, cy + 2 + k, 1, 3, tc);
                gfx_fill(g, cx + 3 + k, cy + 4 - k, 1, 3, tc);
            }
            gfx_fill(g, cx + 6, cy + 1, 1, 3, tc);
        }
        if (it->flags & MI_RADIO)
            gfx_circle(g, r.x + 12, y + 11, 3, tc);
        const font_t *f = (it->flags & MI_BOLD) ? &font_ui_bold : &font_ui;
        gfx_text(g, f, r.x + 28, y + (ITEM_H - f->height) / 2 + 1, it->label, tc);
        if (it->shortcut)
            gfx_text(g, &font_ui, r.x + r.w - 16 - text_width(&font_ui, it->shortcut), y + (ITEM_H - font_ui.height) / 2 + 1,
                     it->shortcut, tc);
        if (it->sub) {
            int ax = r.x + r.w - 12, ay = y + ITEM_H / 2;
            for (int k = 0; k < 4; k++)
                gfx_vline(g, ax + k, ay - 3 + k, 7 - 2 * k, tc);
        }
        y += ITEM_H;
    }
}

void menu_paint(gfx_t *g)
{
    for (int i = 0; i < depth; i++)
        paint_level(g, &levels[i]);
}

static int item_at(level_t *lv, int x, int y, int *top)
{
    if (!rect_contains(lv->r, x, y))
        return -1;
    int yy = lv->r.y + 3;
    for (int i = 0; i < lv->n; i++) {
        int h = item_height(&lv->items[i]);
        if (y >= yy && y < yy + h) {
            if (top)
                *top = yy;
            return (lv->items[i].flags & MI_SEP) ? -1 : i;
        }
        yy += h;
    }
    return -1;
}

static void activate(int lvl, int idx, int top)
{
    const menu_item_t *it = &levels[lvl].items[idx];
    if (it->flags & MI_DISABLED)
        return;
    if (it->sub) {
        open_level(lvl + 1, it->sub, it->sub_n, levels[lvl].r.x + levels[lvl].r.w - 3, top - 3);
        return;
    }
    menu_cb cb = callback;
    void *ctx = cb_ctx;
    int id = it->id;
    menu_close();
    if (cb)
        cb(id, ctx);
}

bool menu_mouse(int kind, int x, int y, int button)
{
    if (!depth)
        return false;
    int lvl = -1, idx = -1, top = 0;
    for (int i = depth - 1; i >= 0; i--) {
        if (rect_contains(levels[i].r, x, y)) {
            lvl = i;
            idx = item_at(&levels[i], x, y, &top);
            break;
        }
    }
    if (kind == MS_MOVE) {
        if (lvl >= 0) {
            if (levels[lvl].hover != idx) {
                levels[lvl].hover = idx;
                invalidate_level(lvl);
                /* hovering an item with a submenu opens it, otherwise closes deeper levels */
                if (idx >= 0 && levels[lvl].items[idx].sub && !(levels[lvl].items[idx].flags & MI_DISABLED))
                    activate(lvl, idx, top);
                else if (depth > lvl + 1) {
                    for (int i = lvl + 1; i < depth; i++)
                        invalidate_level(i);
                    depth = lvl + 1;
                }
            }
        } else if (bar_hook && rect_contains(bar_area, x, y)) {
            bar_hook(x, y);
        }
        ui_set_cursor(CURSOR_ARROW);
        return true;
    }
    if (kind == MS_DOWN || kind == MS_UP) {
        if (lvl < 0) {
            if (kind == MS_DOWN) {
                bool in_bar = rect_contains(bar_area, x, y);
                menu_close();
                /* a click on the owning menu bar just closes the menu */
                return in_bar || true;
            }
            return true;
        }
        if (idx >= 0 && (kind == MS_UP || levels[lvl].items[idx].sub))
            activate(lvl, idx, top);
        return true;
    }
    return true;
}

bool menu_key(const input_event_t *ev)
{
    if (!depth)
        return false;
    level_t *lv = &levels[depth - 1];
    switch (ev->key) {
    case KEY_ESC:
        if (depth > 1) {
            invalidate_level(depth - 1);
            depth--;
        } else {
            menu_close();
        }
        return true;
    case KEY_DOWN:
    case KEY_UP: {
        int dir = ev->key == KEY_DOWN ? 1 : -1;
        int i = lv->hover;
        for (int k = 0; k < lv->n; k++) {
            i = (i + dir + lv->n) % lv->n;
            if (!(lv->items[i].flags & MI_SEP))
                break;
        }
        lv->hover = i;
        invalidate_level(depth - 1);
        return true;
    }
    case KEY_LEFT:
        if (depth > 1) {
            invalidate_level(depth - 1);
            depth--;
        }
        return true;
    case KEY_RIGHT:
    case KEY_ENTER:
        if (lv->hover >= 0) {
            int top = lv->r.y + 3;
            for (int i = 0; i < lv->hover; i++)
                top += item_height(&lv->items[i]);
            if (ev->key == KEY_ENTER || lv->items[lv->hover].sub)
                activate(depth - 1, lv->hover, top);
        }
        return true;
    }
    return true;
}

/* ---- menu bars ------------------------------------------------------------------------------- */

static window_t *bar_window;
static menubar_t *bar_owner;
static menu_cb bar_cb;
static void *bar_ctx;

static rect_t entry_rect(menubar_t *mb, int i)
{
    int x = 2;
    for (int k = 0; k < i; k++)
        x += text_width(&font_ui, mb->entries[k].title) + 14;
    return rect_make(x, 1, text_width(&font_ui, mb->entries[i].title) + 14, MENUBAR_H - 2);
}

void menubar_paint(menubar_t *mb, gfx_t *g, int w)
{
    gfx_fill(g, 0, 0, w, MENUBAR_H, COL_FACE);
    gfx_hline(g, 0, MENUBAR_H - 1, w, HEX(0xFFFFFF));
    gfx_hline(g, 0, MENUBAR_H - 2, w, HEX(0xD8D2BD));
    bool open = menu_is_open() && bar_owner == mb;
    for (int i = 0; i < mb->n; i++) {
        rect_t r = entry_rect(mb, i);
        bool hot = mb->hover == i || (open && mb->open == i);
        uint32_t tc = HEX(0x000000);
        if (hot) {
            gfx_fill(g, r.x, r.y, r.w, r.h, COL_MENU_HI);
            tc = HEX(0xFFFFFF);
        }
        gfx_text(g, &font_ui, r.x + 7, r.y + (r.h - font_ui.height) / 2 + 1, mb->entries[i].title, tc);
    }
}

static void bar_open(menubar_t *mb, window_t *win, int i)
{
    rect_t c = wm_client_rect(win);
    rect_t r = entry_rect(mb, i);
    mb->open = i;
    menu_open(mb->entries[i].items, mb->entries[i].n, c.x + r.x, c.y + r.y + r.h, bar_cb, bar_ctx);
    bar_area = rect_make(c.x, c.y, c.w, MENUBAR_H);
    bar_hook = NULL;
    bar_owner = mb;
    bar_window = win;
    wm_invalidate_client_rect(win, 0, 0, c.w, MENUBAR_H);
}

/* while a bar menu is open, sliding along the bar switches menus */
static void bar_slide(int x, int y)
{
    if (!bar_owner || !bar_window || !wm_alive(bar_window))
        return;
    rect_t c = wm_client_rect(bar_window);
    for (int i = 0; i < bar_owner->n; i++) {
        rect_t r = entry_rect(bar_owner, i);
        if (rect_contains(r, x - c.x, y - c.y) && i != bar_owner->open) {
            bar_open(bar_owner, bar_window, i);
            bar_hook = bar_slide;
            return;
        }
    }
}

bool menubar_mouse(menubar_t *mb, window_t *win, int kind, int x, int y, menu_cb cb, void *ctx)
{
    if (kind == MS_LEAVE || y >= MENUBAR_H || y < 0) {
        if (mb->hover != -1) {
            mb->hover = -1;
            wm_invalidate_client_rect(win, 0, 0, wm_client_rect(win).w, MENUBAR_H);
        }
        return kind != MS_LEAVE && y >= 0 && y < MENUBAR_H;
    }
    int idx = -1;
    for (int i = 0; i < mb->n; i++)
        if (rect_contains(entry_rect(mb, i), x, y))
            idx = i;
    if (idx != mb->hover) {
        mb->hover = idx;
        wm_invalidate_client_rect(win, 0, 0, wm_client_rect(win).w, MENUBAR_H);
    }
    if (kind == MS_DOWN && idx >= 0) {
        bar_cb = cb;
        bar_ctx = ctx;
        bar_open(mb, win, idx);
        bar_hook = bar_slide;
    }
    return true;
}
