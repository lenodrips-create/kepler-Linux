/* Popup menus (context menus, menu bar menus, submenus) in the Big Sur style. */

#include "ui.h"

#define ITEM_H     22
#define SEP_H      11
#define PAD_Y      5
#define RADIUS     7
#define MAX_LEVELS 3

typedef struct {
    const menu_item_t *items;
    int n;
    rect_t r;
    int hover;
    bool icons; /* some item has an icon: text moves right */
} level_t;

static level_t levels[MAX_LEVELS];
static int depth;
static menu_cb callback;
static void *cb_ctx;
static rect_t bar_area; /* clicks here belong to the owning menu bar */
static void (*bar_hook)(int x, int y);

static int item_height(const menu_item_t *it) { return (it->flags & MI_SEP) ? SEP_H : ITEM_H; }

static bool has_icon(const menu_item_t *it) { return it->icon > 0 && !(it->flags & (MI_CHECK | MI_RADIO | MI_SEP)); }

static int text_x(const level_t *lv) { return lv->icons ? 32 : 21; }

static rect_t level_rect(level_t *lv, int x, int y)
{
    int w = 180;
    lv->icons = false;
    for (int i = 0; i < lv->n; i++)
        if (has_icon(&lv->items[i]))
            lv->icons = true;
    for (int i = 0; i < lv->n; i++) {
        const menu_item_t *it = &lv->items[i];
        if (it->flags & MI_SEP)
            continue;
        const font_t *f = (it->flags & MI_BOLD) ? &font_menu_bold : &font_menu;
        int iw = text_x(lv) + text_width(f, it->label) + 34;
        if (it->shortcut)
            iw += shortcut_width(it->shortcut) + 24;
        w = MAX(w, iw);
    }
    int h = 2 * PAD_Y;
    for (int i = 0; i < lv->n; i++)
        h += item_height(&lv->items[i]);
    if (x + w > g_screen.w - 4)
        x = g_screen.w - w - 4;
    if (y + h > g_screen.h - 4)
        y = MAX(TOPBAR_H, y - h);
    return rect_make(MAX(4, x), y, w, h);
}

/* the shadow reaches past the menu */
static rect_t paint_extent(rect_t r) { return rect_make(r.x - 14, r.y - 8, r.w + 28, r.h + 30); }

static void invalidate_level(int i) { ui_invalidate(paint_extent(levels[i].r)); }

static void open_level(int lvl, const menu_item_t *items, int n, int x, int y)
{
    for (int i = lvl; i < depth; i++)
        invalidate_level(i);
    depth = lvl + 1;
    levels[lvl].items = items;
    levels[lvl].n = n;
    levels[lvl].r = level_rect(&levels[lvl], x, y);
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

void menu_open_bar(const menu_item_t *items, int n, int x, int y, menu_cb cb, void *ctx, rect_t bar,
                   void (*slide)(int x, int y))
{
    menu_open(items, n, x, y, cb, ctx);
    bar_area = bar;
    bar_hook = slide;
}

void menu_close(void)
{
    for (int i = 0; i < depth; i++)
        invalidate_level(i);
    if (depth)
        topbar_changed();
    depth = 0;
}

bool menu_is_open(void) { return depth > 0; }

const menu_item_t *menu_current(void) { return depth ? levels[0].items : NULL; }

/* "Ctrl+S" reads as Command-S on this keyboard-less Mac */
static const char *after_ctrl(const char *s) { return !strncmp(s, "Ctrl+", 5) ? s + 5 : NULL; }

int shortcut_width(const char *s)
{
    const char *k = after_ctrl(s);
    return k ? 14 + text_width(&font_menu, k) : text_width(&font_menu, s);
}

void shortcut_paint(gfx_t *g, int x, int y, const char *s, uint32_t c)
{
    const char *k = after_ctrl(s);
    if (k) {
        gfx_blit_tint(g, x, y + (font_menu.height - 12) / 2 + 1, icon_get(GLYPH_CMD, 12), c);
        x += 14;
        s = k;
    }
    gfx_text(g, &font_menu, x, y, s, c);
}

int menu_rects(rect_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < depth && n < max; i++)
        out[n++] = levels[i].r;
    return n;
}

static void paint_level(gfx_t *g, level_t *lv)
{
    rect_t r = lv->r;
    gfx_shadow(g, r.x, r.y, r.w, r.h, RADIUS, 14, 6, 60);
    gfx_blur_behind(g, r.x, r.y, r.w, r.h, RADIUS, 10, RGBA(246, 246, 246, 208), CORNER_ALL);
    gfx_round_frame(g, r.x, r.y, r.w, r.h, RADIUS, RGBA(0, 0, 0, 52), CORNER_ALL);
    gfx_round_frame(g, r.x + 1, r.y + 1, r.w - 2, r.h - 2, RADIUS - 1, RGBA(255, 255, 255, 110), CORNER_ALL);

    int y = r.y + PAD_Y;
    int tx = r.x + text_x(lv);
    for (int i = 0; i < lv->n; i++) {
        const menu_item_t *it = &lv->items[i];
        if (it->flags & MI_SEP) {
            gfx_hline(g, r.x + 10, y + SEP_H / 2, r.w - 20, RGBA(0, 0, 0, 34));
            y += SEP_H;
            continue;
        }
        bool hot = lv->hover == i;
        bool dis = it->flags & MI_DISABLED;
        uint32_t tc = dis ? HEX(0xADADAD) : HEX(0x1D1D1F);
        uint32_t sc = dis ? HEX(0xC4C4C4) : HEX(0x8A8A8E);
        if (hot && !dis) {
            gfx_round_rect(g, r.x + 5, y, r.w - 10, ITEM_H, 4, COL_MENU_HI, CORNER_ALL);
            tc = sc = HEX(0xFFFFFF);
        }
        if (has_icon(it))
            gfx_blit_alpha(g, r.x + 10, y + 3, icon_get(it->icon, 16), dis ? 110 : 255);
        if (it->flags & MI_CHECK)
            gfx_blit_tint(g, r.x + 7, y + 5, icon_get(GLYPH_CHECK, 12), tc);
        if (it->flags & MI_RADIO)
            gfx_circle(g, r.x + 12, y + ITEM_H / 2, 3, tc);
        const font_t *f = (it->flags & MI_BOLD) ? &font_menu_bold : &font_menu;
        gfx_text(g, f, tx, y + (ITEM_H - f->height) / 2, it->label, tc);
        if (it->shortcut)
            shortcut_paint(g, r.x + r.w - 16 - shortcut_width(it->shortcut), y + (ITEM_H - font_menu.height) / 2, it->shortcut, sc);
        if (it->sub)
            gfx_blit_tint(g, r.x + r.w - 20, y + 5, icon_get(GLYPH_CHEVRON_R, 12), hot ? HEX(0xFFFFFF) : HEX(0x6E6E73));
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
    int yy = lv->r.y + PAD_Y;
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
        open_level(lvl + 1, it->sub, it->sub_n, levels[lvl].r.x + levels[lvl].r.w - 4, top - PAD_Y);
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
                /* a click on the owning bar just closes; anything else is swallowed too */
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
            int top = lv->r.y + PAD_Y;
            for (int i = 0; i < lv->hover; i++)
                top += item_height(&lv->items[i]);
            if (ev->key == KEY_ENTER || lv->items[lv->hover].sub)
                activate(depth - 1, lv->hover, top);
        }
        return true;
    }
    return true;
}

/* ---- in-window menu bars ------------------------------------------------------------------- */

/* Windows register their menus with wm_set_app and the menu bar at the top of
 * the screen shows them; the in-window bar is gone. */
void menubar_paint(menubar_t *mb, gfx_t *g, int w) { (void)mb, (void)g, (void)w; }

bool menubar_mouse(menubar_t *mb, window_t *win, int kind, int x, int y, menu_cb cb, void *ctx)
{
    (void)mb, (void)win, (void)kind, (void)x, (void)y, (void)cb, (void)ctx;
    return false;
}
