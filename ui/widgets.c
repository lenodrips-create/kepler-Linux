/* Common controls: buttons, text boxes, scroll bars, tabs, progress bars. */

#include "ui.h"

void draw_button_face(gfx_t *g, int x, int y, int w, int h, int state)
{
    bool dis = state & BTN_DISABLED;
    bool down = (state & BTN_PRESSED) && !dis;
    uint32_t edge = dis ? HEX(0xC9C7BA) : HEX(0x003C74);
    gfx_round_rect(g, x, y, w, h, 3, edge, CORNER_ALL);
    uint32_t top = HEX(0xFFFFFF), bot = HEX(0xE3E1D6);
    if (down) {
        top = HEX(0xE0DFD8);
        bot = HEX(0xF2F1EA);
    }
    if (dis) {
        top = bot = HEX(0xF5F4EA);
    }
    gfx_round_rect_vgrad(g, x + 1, y + 1, w - 2, h - 2, 2, top, bot, CORNER_ALL);
    if (!dis && !down) {
        gfx_hline(g, x + 2, y + h - 3, w - 4, HEX(0xD6D0C5));
        gfx_hline(g, x + 2, y + h - 2, w - 4, HEX(0xC9C3B6));
        gfx_vline(g, x + w - 2, y + 2, h - 4, HEX(0xD6D0C5));
    }
    if ((state & BTN_HOVER) && !dis && !down) {
        /* the orange glow */
        gfx_round_frame(g, x + 1, y + 1, w - 2, h - 2, 2, HEX(0xF8B330), CORNER_ALL);
        gfx_round_frame(g, x + 2, y + 2, w - 4, h - 4, 2, HEX(0xFDD889), CORNER_ALL);
    } else if ((state & (BTN_DEFAULT | BTN_FOCUS)) && !dis && !down) {
        gfx_round_frame(g, x + 1, y + 1, w - 2, h - 2, 2, HEX(0x6982EE), CORNER_ALL);
        gfx_round_frame(g, x + 2, y + 2, w - 4, h - 4, 2, HEX(0xB7CBF6), CORNER_ALL);
    }
}

void draw_button(gfx_t *g, int x, int y, int w, int h, const char *label, int state)
{
    draw_button_face(g, x, y, w, h, state);
    bool dis = state & BTN_DISABLED;
    int off = (state & BTN_PRESSED) ? 1 : 0;
    int tw = text_width(&font_ui, label);
    gfx_text(g, &font_ui, x + (w - tw) / 2 + off, y + (h - font_ui.height) / 2 + 1 + off, label,
             dis ? HEX(0xACA899) : HEX(0x000000));
}

void draw_textbox(gfx_t *g, int x, int y, int w, int h)
{
    gfx_fill(g, x, y, w, h, HEX(0xFFFFFF));
    gfx_rect(g, x, y, w, h, COL_EDIT_EDGE);
}

void draw_sunken(gfx_t *g, int x, int y, int w, int h)
{
    gfx_hline(g, x, y, w, HEX(0x808080));
    gfx_vline(g, x, y, h, HEX(0x808080));
    gfx_hline(g, x, y + h - 1, w, HEX(0xFFFFFF));
    gfx_vline(g, x + w - 1, y, h, HEX(0xFFFFFF));
}

void draw_raised(gfx_t *g, int x, int y, int w, int h)
{
    gfx_hline(g, x, y, w, HEX(0xFFFFFF));
    gfx_vline(g, x, y, h, HEX(0xFFFFFF));
    gfx_hline(g, x, y + h - 1, w, HEX(0x808080));
    gfx_vline(g, x + w - 1, y, h, HEX(0x808080));
}

void draw_progress(gfx_t *g, int x, int y, int w, int h, int pct)
{
    gfx_round_rect(g, x, y, w, h, 3, HEX(0x686868), CORNER_ALL);
    gfx_round_rect(g, x + 1, y + 1, w - 2, h - 2, 2, HEX(0xFFFFFF), CORNER_ALL);
    int inner = w - 6;
    int fill = inner * CLAMP(pct, 0, 100) / 100;
    for (int bx = 0; bx + 8 <= fill + 8 && bx < fill; bx += 10) {
        int bw = MIN(8, fill - bx);
        gfx_vgrad(g, x + 3 + bx, y + 3, bw, h - 6, HEX(0xADEDA8), HEX(0x26B21E));
    }
}

void draw_groupbox(gfx_t *g, int x, int y, int w, int h, const char *title)
{
    int ty = y + font_ui.height / 2;
    gfx_round_frame(g, x, ty, w, h - (ty - y), 3, HEX(0xD0D0BF), CORNER_ALL);
    int tw = text_width(&font_ui, title);
    gfx_fill(g, x + 6, y, tw + 6, font_ui.height, COL_FACE);
    gfx_text(g, &font_ui, x + 9, y + 1, title, HEX(0x0046D5));
}

void draw_checkbox(gfx_t *g, int x, int y, bool checked, const char *label)
{
    gfx_rect(g, x, y, 13, 13, HEX(0x1C5180));
    gfx_vgrad(g, x + 1, y + 1, 11, 11, HEX(0xDCDCD7), HEX(0xFFFFFF));
    if (checked)
        for (int k = 0; k < 3; k++) {
            gfx_fill(g, x + 3 + k, y + 5 + k, 1, 3, HEX(0x21A121));
            gfx_fill(g, x + 6 + k, y + 7 - k, 1, 3, HEX(0x21A121));
            gfx_fill(g, x + 9, y + 3, 1, 3, HEX(0x21A121));
        }
    if (label)
        gfx_text(g, &font_ui, x + 19, y, label, HEX(0x000000));
}

void draw_focus_rect(gfx_t *g, int x, int y, int w, int h)
{
    for (int i = 0; i < w; i += 2) {
        gfx_pixel(g, x + i, y, HEX(0x000000));
        gfx_pixel(g, x + i, y + h - 1, HEX(0x000000));
    }
    for (int j = 0; j < h; j += 2) {
        gfx_pixel(g, x, y + j, HEX(0x000000));
        gfx_pixel(g, x + w - 1, y + j, HEX(0x000000));
    }
}

static int tab_w(const char *t) { return text_width(&font_ui, t) + 16; }

void draw_tabs(gfx_t *g, int x, int y, int w, const char *const *tabs, int n, int active)
{
    int tx = x + 2;
    gfx_hline(g, x, y + 21, w, HEX(0x919B9C));
    for (int i = 0; i < n; i++) {
        int tw = tab_w(tabs[i]);
        bool act = i == active;
        int top = act ? y : y + 2;
        int h = act ? 22 : 19;
        int xx = act ? tx - 2 : tx, ww = act ? tw + 4 : tw;
        gfx_fill(g, xx, top, ww, h, act ? HEX(0xFCFCFE) : HEX(0xF4F3EE));
        gfx_vgrad(g, xx, top, ww, h, act ? HEX(0xFFFFFF) : HEX(0xFEFEFE), act ? HEX(0xFCFCFE) : HEX(0xEAE8E1));
        gfx_vline(g, xx, top + 1, h - 1, HEX(0x919B9C));
        gfx_vline(g, xx + ww - 1, top + 1, h - 1, HEX(0x919B9C));
        gfx_hline(g, xx + 1, top, ww - 2, HEX(0x919B9C));
        if (act) {
            gfx_hline(g, xx + 1, top, ww - 2, HEX(0xE68B2C));
            gfx_hline(g, xx, top + 1, ww, HEX(0xFFC73C));
            gfx_hline(g, xx, top + 2, ww, HEX(0xFFC73C));
        }
        gfx_text(g, &font_ui, tx + 8, top + 4, tabs[i], HEX(0x000000));
        tx += tw;
    }
}

int tabs_hit(int x, int y, const char *const *tabs, int n, int mx, int my)
{
    if (my < y || my >= y + 22)
        return -1;
    int tx = x + 2;
    for (int i = 0; i < n; i++) {
        int tw = tab_w(tabs[i]);
        if (mx >= tx && mx < tx + tw)
            return i;
        tx += tw;
    }
    return -1;
}

void draw_statusbar(gfx_t *g, int y, int w, const char *text)
{
    gfx_fill(g, 0, y, w, 22, COL_FACE);
    gfx_hline(g, 0, y, w, HEX(0xD8D2BD));
    gfx_hline(g, 0, y + 1, w, HEX(0xFFFFFF));
    gfx_text(g, &font_ui, 6, y + 5, text, HEX(0x000000));
    for (int i = 0; i < 3; i++)
        for (int j = 0; j <= i; j++)
            gfx_fill(g, w - 4 - (i - j) * 4, y + 10 + i * 4, 2, 2, HEX(0xB8B4A6));
}

/* ---- scroll bars (Luna blue) -------------------------------------------------------------------- */

void scrollbar_clamp(scrollbar_t *sb)
{
    int max = MAX(0, sb->total - sb->visible);
    sb->pos = CLAMP(sb->pos, 0, max);
}

static void thumb_geom(scrollbar_t *sb, int h, int *ty, int *th)
{
    int track = h - 2 * SCROLLBAR_W;
    if (sb->total <= sb->visible || track <= 8) {
        *ty = SCROLLBAR_W;
        *th = track;
        return;
    }
    *th = MAX(16, track * sb->visible / sb->total);
    int max = sb->total - sb->visible;
    *ty = SCROLLBAR_W + (track - *th) * sb->pos / max;
}

static void sb_button(gfx_t *g, int x, int y, int dir, bool hot, bool enabled)
{
    uint32_t top = hot ? HEX(0xDCE9FF) : HEX(0xC8D9FC), bot = hot ? HEX(0xB3CBFA) : HEX(0xA6C0F5);
    gfx_round_rect(g, x, y, SCROLLBAR_W, SCROLLBAR_W, 2, HEX(0xFFFFFF), CORNER_ALL);
    gfx_round_rect_vgrad(g, x + 1, y + 1, SCROLLBAR_W - 2, SCROLLBAR_W - 2, 2, top, bot, CORNER_ALL);
    gfx_round_frame(g, x + 1, y + 1, SCROLLBAR_W - 2, SCROLLBAR_W - 2, 2, HEX(0x95AEE9), CORNER_ALL);
    uint32_t c = enabled ? HEX(0x4D6185) : HEX(0xA9B6D2);
    int cx = x + SCROLLBAR_W / 2, cy = y + SCROLLBAR_W / 2;
    for (int k = 0; k < 4; k++) {
        int yy = dir < 0 ? cy - 2 + k : cy + 1 - k;
        gfx_fill(g, cx - k - 1, yy, 2 * k + 3, 1, c);
    }
}

void scrollbar_paint(scrollbar_t *sb, gfx_t *g, int x, int y, int h)
{
    gfx_fill(g, x, y, SCROLLBAR_W, h, HEX(0xF4F3EE));
    gfx_vline(g, x, y, h, HEX(0xEEEDE5));
    bool en = sb->total > sb->visible;
    sb_button(g, x, y, -1, sb->hover == 1, en);
    sb_button(g, x, y + h - SCROLLBAR_W, 1, sb->hover == 2, en);
    if (!en)
        return;
    int ty, th;
    thumb_geom(sb, h, &ty, &th);
    bool hot = sb->hover == 3 || sb->drag_off >= 0;
    gfx_round_rect(g, x, y + ty, SCROLLBAR_W, th, 2, HEX(0xFFFFFF), CORNER_ALL);
    gfx_round_rect(g, x + 1, y + ty + 1, SCROLLBAR_W - 2, th - 2, 2, HEX(0x98B1E6), CORNER_ALL);
    gfx_hgrad(g, x + 2, y + ty + 2, SCROLLBAR_W - 4, th - 4, hot ? HEX(0xD9E7FF) : HEX(0xCBDAFC), hot ? HEX(0xB9CEFA) : HEX(0xAFC6F7));
    if (th > 16)
        for (int i = 0; i < 4; i++) {
            int gy = y + ty + th / 2 - 4 + i * 2;
            gfx_hline(g, x + 5, gy, 7, HEX(0xEEF4FE));
            gfx_hline(g, x + 6, gy + 1, 7, HEX(0x8CA6DE));
        }
}

bool scrollbar_mouse(scrollbar_t *sb, int kind, int x, int y, int sx, int sy, int h)
{
    int old = sb->pos;
    int lx = x - sx, ly = y - sy;
    if (kind == MS_WHEEL) {
        sb->pos += x; /* x carries the delta for wheel events */
        scrollbar_clamp(sb);
        return sb->pos != old;
    }
    int ty, th;
    thumb_geom(sb, h, &ty, &th);
    if (sb->drag_off >= 0) {
        if (kind == MS_MOVE) {
            int track = h - 2 * SCROLLBAR_W - th;
            int max = sb->total - sb->visible;
            if (track > 0)
                sb->pos = (ly - sb->drag_off - SCROLLBAR_W) * max / track;
            scrollbar_clamp(sb);
        }
        if (kind == MS_UP)
            sb->drag_off = -1;
        return sb->pos != old || kind == MS_UP;
    }
    bool inside = lx >= 0 && lx < SCROLLBAR_W && ly >= 0 && ly < h;
    int hv = !inside ? 0 : ly < SCROLLBAR_W ? 1 : ly >= h - SCROLLBAR_W ? 2 : (ly >= ty && ly < ty + th) ? 3 : 4;
    bool changed = hv != sb->hover;
    sb->hover = hv;
    if (kind == MS_DOWN && inside) {
        int step = MAX(1, sb->visible / 10);
        if (hv == 1)
            sb->pos -= step;
        else if (hv == 2)
            sb->pos += step;
        else if (hv == 3)
            sb->drag_off = ly - ty;
        else
            sb->pos += ly < ty ? -sb->visible : sb->visible;
        scrollbar_clamp(sb);
        return true;
    }
    return changed || sb->pos != old;
}

/* ---- button groups ------------------------------------------------------------------------------ */

void buttons_paint(buttons_t *b, gfx_t *g)
{
    for (int i = 0; i < b->n; i++) {
        button_t *it = &b->items[i];
        int st = it->flags;
        if (b->hover == i)
            st |= BTN_HOVER;
        if (b->pressed == i && b->hover == i)
            st |= BTN_PRESSED;
        draw_button(g, it->x, it->y, it->w, it->h, it->label, st);
    }
}

int buttons_mouse(buttons_t *b, window_t *w, int kind, int x, int y)
{
    int idx = -1;
    for (int i = 0; i < b->n; i++) {
        button_t *it = &b->items[i];
        if (!(it->flags & BTN_DISABLED) && x >= it->x && y >= it->y && x < it->x + it->w && y < it->y + it->h)
            idx = i;
    }
    if (kind == MS_LEAVE)
        idx = -1;
    int result = -1;
    if (idx != b->hover) {
        b->hover = idx;
        wm_invalidate_client(w);
    }
    if (kind == MS_DOWN) {
        b->pressed = idx;
        wm_invalidate_client(w);
    } else if (kind == MS_UP) {
        if (b->pressed >= 0 && b->pressed == idx)
            result = b->items[idx].id;
        b->pressed = -1;
        wm_invalidate_client(w);
    }
    return result;
}

/* ---- text fields ---------------------------------------------------------------------------------- */

void textfield_set(textfield_t *t, const char *s)
{
    strlcpy(t->text, s, sizeof t->text);
    t->cursor = strlen(t->text);
}

bool textfield_key(textfield_t *t, const input_event_t *ev)
{
    int len = strlen(t->text);
    switch (ev->key) {
    case KEY_LEFT:
        if (t->cursor > 0)
            t->cursor--;
        return true;
    case KEY_RIGHT:
        if (t->cursor < len)
            t->cursor++;
        return true;
    case KEY_HOME:
        t->cursor = 0;
        return true;
    case KEY_END:
        t->cursor = len;
        return true;
    case KEY_BACKSPACE:
        if (t->cursor > 0) {
            memmove(&t->text[t->cursor - 1], &t->text[t->cursor], len - t->cursor + 1);
            t->cursor--;
        }
        return true;
    case KEY_DELETE:
        if (t->cursor < len)
            memmove(&t->text[t->cursor], &t->text[t->cursor + 1], len - t->cursor);
        return true;
    }
    if (ev->key >= 32 && ev->key < 127 && !(ev->mods & (MOD_CTRL | MOD_ALT)) && len < (int)sizeof t->text - 1) {
        memmove(&t->text[t->cursor + 1], &t->text[t->cursor], len - t->cursor + 1);
        t->text[t->cursor++] = (char)ev->key;
        return true;
    }
    return false;
}

void textfield_paint(textfield_t *t, gfx_t *g, int x, int y, int w, bool focused)
{
    int h = 21;
    draw_textbox(g, x, y, w, h);
    gfx_t inner = gfx_sub(g, x + 2, y + 2, w - 4, h - 4);
    int cx = text_width_n(&font_ui, t->text, t->cursor);
    int scroll = MAX(0, cx - (w - 10));
    gfx_text(&inner, &font_ui, 2 - scroll, 2, t->text, HEX(0x000000));
    if (focused && (timer_ms() / 530) % 2 == 0)
        gfx_vline(&inner, 2 - scroll + cx, 1, font_ui.height + 1, HEX(0x000000));
}
