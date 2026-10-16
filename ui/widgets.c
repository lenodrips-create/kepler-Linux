/* Common controls in the Big Sur style: push buttons, text fields, scroll bars,
 * segmented tabs, check boxes, progress bars and boxes. */

#include "ui.h"

void draw_button_face(gfx_t *g, int x, int y, int w, int h, int state)
{
    bool dis = state & BTN_DISABLED;
    bool down = (state & BTN_PRESSED) && !dis;
    bool blue = (state & BTN_DEFAULT) && !dis;
    /* a hairline shadow under the button, then the face */
    gfx_round_rect(g, x, y + 1, w, h, 5, RGBA(0, 0, 0, dis ? 14 : 34), CORNER_ALL);
    if (blue) {
        uint32_t top = down ? HEX(0x1458C8) : HEX(0x3C8DF8), bot = down ? HEX(0x0C4AB4) : HEX(0x176BEA);
        gfx_round_rect(g, x, y, w, h, 5, HEX(0x1260D6), CORNER_ALL);
        gfx_round_rect_vgrad(g, x + 1, y + 1, w - 2, h - 2, 4, top, bot, CORNER_ALL);
        gfx_hline(g, x + 4, y + 1, w - 8, RGBA(255, 255, 255, 60));
        return;
    }
    gfx_round_rect(g, x, y, w, h, 5, RGBA(0, 0, 0, dis ? 26 : 44), CORNER_ALL);
    uint32_t face = dis ? HEX(0xF7F7F7) : down ? HEX(0xDCDCDC) : HEX(0xFFFFFF);
    gfx_round_rect(g, x + 1, y + 1, w - 2, h - 2, 4, face, CORNER_ALL);
    if (state & BTN_FOCUS && !dis) {
        gfx_round_frame(g, x - 2, y - 2, w + 4, h + 4, 7, RGBA(10, 100, 222, 110), CORNER_ALL);
        gfx_round_frame(g, x - 1, y - 1, w + 2, h + 2, 6, RGBA(10, 100, 222, 170), CORNER_ALL);
    }
}

void draw_button(gfx_t *g, int x, int y, int w, int h, const char *label, int state)
{
    draw_button_face(g, x, y, w, h, state);
    bool dis = state & BTN_DISABLED;
    bool blue = (state & BTN_DEFAULT) && !dis;
    int tw = text_width(&font_ui, label);
    gfx_text(g, &font_ui, x + (w - tw) / 2, y + (h - font_ui.height) / 2, label,
             dis ? HEX(0xB4B4B4) : blue ? HEX(0xFFFFFF) : COL_TEXT);
}

void draw_textbox(gfx_t *g, int x, int y, int w, int h)
{
    gfx_fill(g, x, y, w, h, HEX(0xFFFFFF));
    gfx_rect(g, x, y, w, h, COL_EDIT_EDGE);
    gfx_hline(g, x + 1, y, w - 2, HEX(0xB2B2B2));
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
    /* a thin rounded bar, centred in the space given */
    int bh = MIN(h, 6);
    int by = y + (h - bh) / 2;
    gfx_round_rect(g, x, by, w, bh, bh / 2, HEX(0xDCDCDC), CORNER_ALL);
    int fill = w * CLAMP(pct, 0, 100) / 100;
    if (fill > 0)
        gfx_round_rect(g, x, by, MAX(bh, fill), bh, bh / 2, COL_ACCENT, CORNER_ALL);
}

void draw_groupbox(gfx_t *g, int x, int y, int w, int h, const char *title)
{
    /* the title sits above a softly tinted rounded box */
    int top = y + font_ui.height + 1;
    gfx_round_rect(g, x, top, w, h - (top - y), 6, RGBA(0, 0, 0, 12), CORNER_ALL);
    gfx_round_frame(g, x, top, w, h - (top - y), 6, RGBA(0, 0, 0, 28), CORNER_ALL);
    gfx_text(g, &font_ui, x + 2, y, title, HEX(0x4A4A4A));
}

void draw_checkbox(gfx_t *g, int x, int y, bool checked, const char *label)
{
    if (checked) {
        gfx_round_rect_vgrad(g, x, y, 14, 14, 3, HEX(0x3C8DF8), HEX(0x176BEA), CORNER_ALL);
        gfx_blit_tint(g, x + 1, y + 1, icon_get(GLYPH_CHECK, 12), HEX(0xFFFFFF));
    } else {
        gfx_round_rect(g, x, y, 14, 14, 3, RGBA(0, 0, 0, 60), CORNER_ALL);
        gfx_round_rect(g, x + 1, y + 1, 12, 12, 2, HEX(0xFFFFFF), CORNER_ALL);
    }
    if (label)
        gfx_text(g, &font_ui, x + 20, y, label, COL_TEXT);
}

void draw_focus_rect(gfx_t *g, int x, int y, int w, int h)
{
    gfx_round_frame(g, x, y, w, h, 3, RGBA(10, 100, 222, 150), CORNER_ALL);
}

/* ---- segmented tabs ------------------------------------------------------------------------------- */

static int seg_w(const char *t) { return text_width(&font_ui, t) + 20; }

static int seg_x0(int x, int w, const char *const *tabs, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++)
        total += seg_w(tabs[i]);
    return x + (w - total) / 2;
}

void draw_tabs(gfx_t *g, int x, int y, int w, const char *const *tabs, int n, int active)
{
    int sx = seg_x0(x, w, tabs, n), total = 0;
    for (int i = 0; i < n; i++)
        total += seg_w(tabs[i]);
    gfx_round_rect(g, sx, y + 1, total, TABS_H, 6, RGBA(0, 0, 0, 24), CORNER_ALL);
    gfx_round_rect(g, sx, y, total, TABS_H, 6, HEX(0xE1E1E1), CORNER_ALL);
    gfx_round_frame(g, sx, y, total, TABS_H, 6, RGBA(0, 0, 0, 30), CORNER_ALL);
    int tx = sx;
    for (int i = 0; i < n; i++) {
        int tw = seg_w(tabs[i]);
        if (i == active) {
            gfx_round_rect(g, tx + 1, y + 2, tw - 2, TABS_H - 2, 5, RGBA(0, 0, 0, 30), CORNER_ALL);
            gfx_round_rect(g, tx + 1, y + 1, tw - 2, TABS_H - 2, 5, HEX(0xFFFFFF), CORNER_ALL);
        } else if (i + 1 < n && i + 1 != active) {
            gfx_vline(g, tx + tw, y + 5, TABS_H - 10, HEX(0xC4C4C4));
        }
        gfx_text_centered(g, &font_ui, tx, y + (TABS_H - font_ui.height) / 2, tw, tabs[i], COL_TEXT);
        tx += tw;
    }
}

int tabs_hit(int x, int y, int w, const char *const *tabs, int n, int mx, int my)
{
    if (my < y || my >= y + TABS_H)
        return -1;
    int tx = seg_x0(x, w, tabs, n);
    for (int i = 0; i < n; i++) {
        int tw = seg_w(tabs[i]);
        if (mx >= tx && mx < tx + tw)
            return i;
        tx += tw;
    }
    return -1;
}

void draw_tab_panel(gfx_t *g, int x, int y, int w, int h)
{
    gfx_round_rect(g, x, y, w, h, 7, HEX(0xE4E4E4), CORNER_ALL);
    gfx_round_frame(g, x, y, w, h, 7, RGBA(0, 0, 0, 30), CORNER_ALL);
}

void draw_statusbar(gfx_t *g, int y, int w, const char *text)
{
    gfx_fill(g, 0, y, w, 22, HEX(0xEFEFEF));
    gfx_hline(g, 0, y, w, HEX(0xD6D6D6));
    gfx_text(g, &font_ui, 10, y + (22 - font_ui.height) / 2 + 1, text, HEX(0x6E6E73));
}

/* ---- scroll bars: a slim rounded thumb on a light track ----------------------------------------- */

#define SB_PAD 3

void scrollbar_clamp(scrollbar_t *sb)
{
    int max = MAX(0, sb->total - sb->visible);
    sb->pos = CLAMP(sb->pos, 0, max);
}

static void thumb_geom(scrollbar_t *sb, int h, int *ty, int *th)
{
    int track = h - 2 * SB_PAD;
    if (sb->total <= sb->visible || track <= 8) {
        *ty = SB_PAD;
        *th = track;
        return;
    }
    *th = MAX(24, track * sb->visible / sb->total);
    int max = sb->total - sb->visible;
    *ty = SB_PAD + (track - *th) * sb->pos / max;
}

void scrollbar_paint(scrollbar_t *sb, gfx_t *g, int x, int y, int h)
{
    gfx_fill(g, x, y, SCROLLBAR_W, h, HEX(0xFAFAFA));
    gfx_vline(g, x, y, h, HEX(0xE8E8E8));
    if (sb->total <= sb->visible)
        return;
    int ty, th;
    thumb_geom(sb, h, &ty, &th);
    bool hot = sb->hover == 3 || sb->drag_off >= 0;
    gfx_round_rect(g, x + 4, y + ty, 8, th, 4, hot ? HEX(0x8A8A8A) : HEX(0xC1C1C1), CORNER_ALL);
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
            int track = h - 2 * SB_PAD - th;
            int max = sb->total - sb->visible;
            if (track > 0)
                sb->pos = (ly - sb->drag_off - SB_PAD) * max / track;
            scrollbar_clamp(sb);
        }
        if (kind == MS_UP)
            sb->drag_off = -1;
        return sb->pos != old || kind == MS_UP;
    }
    bool inside = lx >= 0 && lx < SCROLLBAR_W && ly >= 0 && ly < h;
    int hv = !inside ? 0 : (ly >= ty && ly < ty + th) ? 3 : 4;
    bool changed = hv != sb->hover;
    sb->hover = hv;
    if (kind == MS_DOWN && inside && sb->total > sb->visible) {
        if (hv == 3)
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
    if (focused) {
        gfx_round_frame(g, x - 3, y - 3, w + 6, h + 6, 5, RGBA(10, 100, 222, 90), CORNER_ALL);
        gfx_round_frame(g, x - 2, y - 2, w + 4, h + 4, 4, RGBA(10, 100, 222, 150), CORNER_ALL);
        gfx_round_frame(g, x - 1, y - 1, w + 2, h + 2, 3, RGBA(10, 100, 222, 120), CORNER_ALL);
    }
    draw_textbox(g, x, y, w, h);
    gfx_t inner = gfx_sub(g, x + 2, y + 2, w - 4, h - 4);
    int cx = text_width_n(&font_ui, t->text, t->cursor);
    int scroll = MAX(0, cx - (w - 10));
    gfx_text(&inner, &font_ui, 2 - scroll, 1, t->text, COL_TEXT);
    if (focused && (timer_ms() / 530) % 2 == 0)
        gfx_vline(&inner, 2 - scroll + cx, 1, font_ui.height, COL_TEXT);
}

/* ---- clipboard ---------------------------------------------------------------------------------------- */

static char *clip;

void clipboard_set(const char *s, int n)
{
    kfree(clip);
    clip = kmalloc(n + 1);
    memcpy(clip, s, n);
    clip[n] = 0;
}

const char *clipboard_get(void) { return clip ? clip : ""; }
