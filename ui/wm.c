/* Window manager: Luna-style frames, z-order, focus, move/resize, minimise/maximise. */

#include "ui.h"

static window_t *bottom; /* z-order list, bottom first */
static window_t *active;
static int next_id = 1;
static int cascade;

enum { HT_NONE, HT_CLIENT, HT_CAPTION, HT_CLOSE, HT_MAX, HT_MIN, HT_L, HT_R, HT_T, HT_B, HT_TL, HT_TR, HT_BL, HT_BR, HT_FRAME };
enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_BUTTON, DRAG_CLIENT };

static struct {
    int mode;
    window_t *w;
    int hit;
    int sx, sy;
    rect_t start;
    bool inside;
} drag;

static window_t *hover_win;
static window_t *hot_win; /* caption button hover */
static int hot_hit;

/* ---- list helpers ---------------------------------------------------------------- */

static void unlink(window_t *w)
{
    window_t **pp = &bottom;
    while (*pp && *pp != w)
        pp = &(*pp)->next;
    if (*pp)
        *pp = w->next;
    w->next = NULL;
}

static void push_top(window_t *w)
{
    window_t **pp = &bottom;
    while (*pp)
        pp = &(*pp)->next;
    *pp = w;
    w->next = NULL;
}

static rect_t frame_rect(const window_t *w) { return rect_make(w->x, w->y, w->w, w->h); }

static bool visible(const window_t *w) { return !w->dead && !(w->flags & WF_MINIMIZED); }

window_t *wm_first(void) { return bottom; }

bool wm_alive(window_t *w)
{
    for (window_t *it = bottom; it; it = it->next)
        if (it == w && !it->dead)
            return true;
    return false;
}

int wm_count(void)
{
    int n = 0;
    for (window_t *w = bottom; w; w = w->next)
        if (!w->dead)
            n++;
    return n;
}

window_t *wm_find(const window_ops_t *ops)
{
    for (window_t *w = bottom; w; w = w->next)
        if (!w->dead && w->ops == ops)
            return w;
    return NULL;
}

window_t *wm_active(void) { return active; }

void wm_deactivate(void)
{
    window_t *old = active;
    active = NULL;
    if (old) {
        ui_invalidate(rect_make(old->x, old->y, old->w, TITLE_H));
        taskbar_changed();
    }
}

static window_t *topmost_visible(void)
{
    window_t *top = NULL;
    for (window_t *w = bottom; w; w = w->next)
        if (visible(w))
            top = w;
    return top;
}

static window_t *window_at(int x, int y)
{
    window_t *hit = NULL;
    for (window_t *w = bottom; w; w = w->next)
        if (visible(w) && rect_contains(frame_rect(w), x, y))
            hit = w;
    return hit;
}

/* ---- geometry ------------------------------------------------------------------- */

rect_t wm_client_rect(const window_t *w)
{
    return rect_make(w->x + FRAME, w->y + TITLE_H, w->w - 2 * FRAME, w->h - TITLE_H - FRAME);
}

static int button_count(const window_t *w)
{
    if (w->flags & WF_DIALOG)
        return 1;
    return 3;
}

/* caption buttons from the right: 0 close, 1 maximise, 2 minimise (local coordinates) */
static rect_t caption_button(const window_t *w, int slot)
{
    int top = (w->flags & WF_MAXIMIZED) ? 4 + 2 : 5;
    return rect_make(w->w - 5 - 21 - slot * 23, top, 21, 21);
}

static int hit_test(window_t *w, int x, int y)
{
    int lx = x - w->x, ly = y - w->y;
    if (lx < 0 || ly < 0 || lx >= w->w || ly >= w->h)
        return HT_NONE;
    bool sizing = (w->flags & WF_RESIZABLE) && !(w->flags & WF_MAXIMIZED);
    if (sizing) {
        const int c = 14;
        bool left = lx < FRAME, right = lx >= w->w - FRAME, top = ly < FRAME, bot = ly >= w->h - FRAME;
        if ((top && lx < c) || (left && ly < c))
            return HT_TL;
        if ((top && lx >= w->w - c) || (right && ly < c))
            return HT_TR;
        if ((bot && lx < c) || (left && ly >= w->h - c))
            return HT_BL;
        if ((bot && lx >= w->w - c) || (right && ly >= w->h - c))
            return HT_BR;
        if (left)
            return HT_L;
        if (right)
            return HT_R;
        if (top)
            return HT_T;
        if (bot)
            return HT_B;
    }
    if (ly < TITLE_H) {
        for (int i = 0; i < button_count(w); i++)
            if (rect_contains(caption_button(w, i), lx, ly)) {
                if (i == 1 && (w->flags & WF_NO_MAX))
                    return HT_CAPTION;
                return i == 0 ? HT_CLOSE : i == 1 ? HT_MAX : HT_MIN;
            }
        return HT_CAPTION;
    }
    if (rect_contains(wm_client_rect(w), x, y))
        return HT_CLIENT;
    return HT_FRAME;
}

/* ---- invalidation ------------------------------------------------------------------ */

void wm_invalidate(window_t *w)
{
    if (w && visible(w))
        ui_invalidate(frame_rect(w));
}

void wm_invalidate_client(window_t *w)
{
    if (w && visible(w))
        ui_invalidate(wm_client_rect(w));
}

void wm_invalidate_client_rect(window_t *w, int x, int y, int cw, int ch)
{
    if (!w || !visible(w))
        return;
    rect_t c = wm_client_rect(w);
    ui_invalidate(rect_intersect(c, rect_make(c.x + x, c.y + y, cw, ch)));
}

static void invalidate_caption(window_t *w)
{
    if (w && visible(w))
        ui_invalidate(rect_make(w->x, w->y, w->w, TITLE_H));
}

/* ---- lifecycle --------------------------------------------------------------------- */

window_t *wm_create(const char *title, int icon, int cw, int ch, const window_ops_t *ops, void *data, uint32_t flags)
{
    window_t *w = kzalloc(sizeof *w);
    w->id = next_id++;
    strlcpy(w->title, title, sizeof w->title);
    w->icon = icon;
    w->flags = flags;
    w->ops = ops;
    w->data = data;
    w->bg = COL_FACE;
    w->w = cw + 2 * FRAME;
    w->h = ch + TITLE_H + FRAME;
    w->min_w = 200;
    w->min_h = 120;
    w->cursor = CURSOR_ARROW;
    w->created = timer_ms();

    int sw = g_screen.w, sh = g_screen.h - TASKBAR_H;
    w->x = 110 + (cascade % 8) * 30;
    w->y = 50 + (cascade % 8) * 30;
    cascade++;
    if (w->x + w->w > sw)
        w->x = MAX(0, sw - w->w);
    if (w->y + w->h > sh)
        w->y = MAX(0, sh - w->h);

    push_top(w);
    wm_focus(w);
    taskbar_changed();
    return w;
}

void wm_center(window_t *w)
{
    ui_invalidate(frame_rect(w));
    w->x = (g_screen.w - w->w) / 2;
    w->y = MAX(0, (g_screen.h - TASKBAR_H - w->h) / 2);
    ui_invalidate(frame_rect(w));
}

void wm_move(window_t *w, int x, int y)
{
    ui_invalidate(frame_rect(w));
    w->x = x;
    w->y = y;
    ui_invalidate(frame_rect(w));
}

void wm_set_client_size(window_t *w, int cw, int ch)
{
    ui_invalidate(frame_rect(w));
    w->w = cw + 2 * FRAME;
    w->h = ch + TITLE_H + FRAME;
    ui_invalidate(frame_rect(w));
    if (w->ops->resized)
        w->ops->resized(w);
}

void wm_set_title(window_t *w, const char *title)
{
    strlcpy(w->title, title, sizeof w->title);
    invalidate_caption(w);
    taskbar_changed();
}

void wm_set_tick(window_t *w, uint32_t ms)
{
    w->tick_ms = ms;
    w->tick_last = timer_ms();
}

void wm_set_modal(window_t *owner, window_t *dialog)
{
    if (!owner)
        return;
    owner->modal = dialog;
    dialog->owner = owner;
}

void wm_focus(window_t *w)
{
    if (!w || w->dead)
        return;
    if (w->modal && wm_alive(w->modal))
        w = w->modal;
    if (w->flags & WF_MINIMIZED) {
        w->flags &= ~WF_MINIMIZED;
        ui_invalidate(frame_rect(w));
    }
    window_t *old = active;
    /* raise the owner first so its dialog stays above it */
    if (w->owner && wm_alive(w->owner) && !(w->owner->flags & WF_MINIMIZED)) {
        unlink(w->owner);
        push_top(w->owner);
        ui_invalidate(frame_rect(w->owner));
    }
    unlink(w);
    push_top(w);
    active = w;
    ui_invalidate(frame_rect(w));
    if (old && old != w)
        invalidate_caption(old);
    taskbar_changed();
}

void wm_close(window_t *w)
{
    if (!w || w->dead)
        return;
    if (w->modal && wm_alive(w->modal))
        wm_close(w->modal);
    w->dead = true;
    if (w->ops->close)
        w->ops->close(w);
    if (w->owner && w->owner->modal == w)
        w->owner->modal = NULL;
    ui_invalidate(frame_rect(w));
    if (drag.w == w)
        drag.mode = DRAG_NONE;
    if (hover_win == w)
        hover_win = NULL;
    if (hot_win == w)
        hot_win = NULL;
    if (active == w) {
        active = NULL;
        window_t *next = (w->owner && wm_alive(w->owner) && visible(w->owner)) ? w->owner : topmost_visible();
        if (next)
            wm_focus(next);
    }
    taskbar_changed();
}

/* free closed windows once nobody is in the middle of handling them */
static void reap(void)
{
    window_t **pp = &bottom;
    while (*pp) {
        window_t *w = *pp;
        if (w->dead && drag.w != w) {
            *pp = w->next;
            kfree(w);
        } else {
            pp = &w->next;
        }
    }
}

void wm_minimize(window_t *w)
{
    if (!w || (w->flags & WF_MINIMIZED))
        return;
    w->flags |= WF_MINIMIZED;
    ui_invalidate(frame_rect(w));
    if (active == w) {
        active = NULL;
        window_t *next = topmost_visible();
        if (next)
            wm_focus(next);
    }
    taskbar_changed();
}

void wm_minimize_all(void)
{
    for (window_t *w = bottom; w; w = w->next)
        if (!w->dead && !(w->flags & WF_NO_TASKBAR))
            wm_minimize(w);
}

void wm_toggle_maximize(window_t *w)
{
    if ((w->flags & (WF_DIALOG | WF_NO_MAX)))
        return;
    ui_invalidate(frame_rect(w));
    if (w->flags & WF_MAXIMIZED) {
        w->flags &= ~WF_MAXIMIZED;
        w->x = w->restore.x;
        w->y = w->restore.y;
        w->w = w->restore.w;
        w->h = w->restore.h;
    } else {
        w->restore = frame_rect(w);
        w->flags |= WF_MAXIMIZED;
        w->x = -FRAME;
        w->y = -FRAME;
        w->w = g_screen.w + 2 * FRAME;
        w->h = g_screen.h - TASKBAR_H + 2 * FRAME;
    }
    ui_invalidate(frame_rect(w));
    if (w->ops->resized)
        w->ops->resized(w);
}

void wm_restore(window_t *w)
{
    if (w->flags & WF_MINIMIZED)
        wm_focus(w);
    else if (w->flags & WF_MAXIMIZED)
        wm_toggle_maximize(w);
}

void wm_cycle(void)
{
    /* bring the bottom-most normal window to the front */
    for (window_t *w = bottom; w; w = w->next)
        if (!w->dead && !(w->flags & WF_NO_TASKBAR) && w != active) {
            wm_focus(w);
            return;
        }
}

/* ---- painting ------------------------------------------------------------------------ */

static const uint32_t cap_active[] = { HEX(0x0A3FD6), HEX(0x3D93FF), HEX(0x2D82FA), HEX(0x0F63EE),
                                       HEX(0x0256E6), HEX(0x004ED9), HEX(0x0042C2) };
static const uint32_t cap_inactive[] = { HEX(0x7C95D6), HEX(0xABC6F6), HEX(0x9DB8EC), HEX(0x8BA8E6),
                                         HEX(0x7E9CE1), HEX(0x7795DC), HEX(0x6D8BD2) };
static const int cap_stops[] = { 0, 10, 28, 62, 150, 222, 255 };

static const uint32_t frame_active[4] = { HEX(0x0831D9), HEX(0x166AEE), HEX(0x0B5BE3), HEX(0x0049D0) };
static const uint32_t frame_inactive[4] = { HEX(0x6F8AD2), HEX(0xA9C1F2), HEX(0x8FA9E6), HEX(0x7C98DE) };

static void paint_caption_button(gfx_t *g, window_t *w, int slot, bool act)
{
    rect_t r = caption_button(w, slot);
    int ht = slot == 0 ? HT_CLOSE : slot == 1 ? HT_MAX : HT_MIN;
    bool disabled = slot == 1 && (w->flags & WF_NO_MAX);
    bool hot = hot_win == w && hot_hit == ht && !disabled;
    bool pressed = drag.mode == DRAG_BUTTON && drag.w == w && drag.hit == ht && drag.inside;

    uint32_t top, bot;
    if (slot == 0) {
        top = HEX(0xEB9A7E);
        bot = HEX(0xCF3F18);
        if (hot) {
            top = HEX(0xF6B49B);
            bot = HEX(0xE0552B);
        }
        if (pressed) {
            top = HEX(0xB13414);
            bot = HEX(0xD6603C);
        }
    } else {
        top = HEX(0x6BA2FA);
        bot = HEX(0x2462E2);
        if (hot) {
            top = HEX(0x8CBBFF);
            bot = HEX(0x3E7CF0);
        }
        if (pressed) {
            top = HEX(0x1E4FBF);
            bot = HEX(0x3C76E6);
        }
    }
    if (!act) {
        top = color_lerp(top, HEX(0xD6E2FA), 120);
        bot = color_lerp(bot, HEX(0xB8CBF2), 120);
    }
    gfx_round_rect(g, r.x, r.y, r.w, r.h, 3, act ? HEX(0xFFFFFF) : HEX(0xEEF2FC), CORNER_ALL);
    gfx_round_rect_vgrad(g, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 2, top, bot, CORNER_ALL);
    /* soft inner highlight */
    gfx_hline(g, r.x + 3, r.y + 2, r.w - 6, RGBA(255, 255, 255, 90));

    uint32_t gc = disabled ? RGBA(255, 255, 255, 110) : HEX(0xFFFFFF);
    int x = r.x, y = r.y;
    if (slot == 0) {
        for (int i = 0; i < 9; i++) {
            gfx_fill(g, x + 6 + i, y + 6 + i, 2, 1, gc);
            gfx_fill(g, x + 13 - i, y + 6 + i, 2, 1, gc);
        }
    } else if (slot == 1) {
        if (w->flags & WF_MAXIMIZED) {
            gfx_rect(g, x + 8, y + 5, 8, 8, gc);
            gfx_hline(g, x + 8, y + 6, 8, gc);
            gfx_fill(g, x + 5, y + 9, 8, 8, top);
            gfx_rect(g, x + 5, y + 8, 8, 8, gc);
            gfx_hline(g, x + 5, y + 9, 8, gc);
        } else {
            gfx_rect(g, x + 5, y + 5, 11, 11, gc);
            gfx_fill(g, x + 5, y + 6, 11, 2, gc);
        }
    } else {
        gfx_fill(g, x + 6, y + 13, 7, 3, gc);
    }
}

static void paint_frame(gfx_t *g, window_t *w, bool act)
{
    int W = w->w, H = w->h;
    const uint32_t *fr = act ? frame_active : frame_inactive;
    gfx_round_rect_stops(g, 0, 0, W, TITLE_H, 8, act ? cap_active : cap_inactive, cap_stops, 7, CORNER_TOP);
    /* darker side edges along the caption */
    for (int i = 0; i < 2; i++) {
        gfx_vline(g, i, 8, TITLE_H - 8, WITH_ALPHA(fr[0], i ? 70 : 160));
        gfx_vline(g, W - 1 - i, 8, TITLE_H - 8, WITH_ALPHA(fr[0], i ? 70 : 160));
    }
    for (int i = 0; i < FRAME; i++) {
        gfx_vline(g, i, TITLE_H, H - TITLE_H, fr[i]);
        gfx_vline(g, W - 1 - i, TITLE_H, H - TITLE_H, fr[i]);
        gfx_hline(g, i, H - 1 - i, W - 2 * i, fr[i]);
    }

    int bx = 6;
    gfx_blit_alpha(g, bx, (TITLE_H - 16) / 2 + 1, icon_get(w->icon, 16), act ? 255 : 200);
    int text_x = bx + 16 + 6;
    int buttons_w = button_count(w) * 23 + 8;
    int max_w = W - text_x - buttons_w;
    int ty = (TITLE_H - font_title.height) / 2 + 1;
    if (act)
        gfx_text_clipped(g, &font_title, text_x + 1, ty + 1, max_w, w->title, HEX(0x0A1E7A));
    gfx_text_clipped(g, &font_title, text_x, ty, max_w, w->title, act ? HEX(0xFFFFFF) : HEX(0xDCE6FA));

    for (int i = 0; i < button_count(w); i++)
        paint_caption_button(g, w, i, act);
}

void wm_paint(gfx_t *g, rect_t dirty)
{
    for (window_t *w = bottom; w; w = w->next) {
        if (!visible(w))
            continue;
        rect_t fr = frame_rect(w);
        if (rect_empty(rect_intersect(fr, dirty)))
            continue;
        gfx_t wg = gfx_sub(g, w->x, w->y, w->w, w->h);
        paint_frame(&wg, w, w == active);
        rect_t c = wm_client_rect(w);
        gfx_t cg = gfx_sub(g, c.x, c.y, c.w, c.h);
        gfx_fill(&cg, 0, 0, c.w, c.h, w->bg);
        if (w->ops->paint)
            w->ops->paint(w, &cg, c.w, c.h);
    }
}

/* ---- input --------------------------------------------------------------------------- */

static int cursor_for(window_t *w, int ht)
{
    switch (ht) {
    case HT_L:
    case HT_R: return CURSOR_SIZE_WE;
    case HT_T:
    case HT_B: return CURSOR_SIZE_NS;
    case HT_TL:
    case HT_BR: return CURSOR_SIZE_NWSE;
    case HT_TR:
    case HT_BL: return CURSOR_SIZE_NESW;
    case HT_CLIENT: return w->cursor;
    default: return CURSOR_ARROW;
    }
}

static void send_client(window_t *w, int kind, int x, int y, int button)
{
    if (!w->ops->mouse || w->dead)
        return;
    rect_t c = wm_client_rect(w);
    w->ops->mouse(w, kind, x - c.x, y - c.y, button);
}

static void set_hot(window_t *w, int ht)
{
    if (ht != HT_CLOSE && ht != HT_MAX && ht != HT_MIN)
        w = NULL;
    if (w == hot_win && (!w || ht == hot_hit))
        return;
    if (hot_win)
        invalidate_caption(hot_win);
    hot_win = w;
    hot_hit = ht;
    if (w)
        invalidate_caption(w);
}

static void do_resize(int x, int y)
{
    window_t *w = drag.w;
    rect_t r = drag.start;
    int dx = x - drag.sx, dy = y - drag.sy;
    int h = drag.hit;
    if (h == HT_L || h == HT_TL || h == HT_BL) {
        int nw = MAX(w->min_w, r.w - dx);
        r.x += r.w - nw;
        r.w = nw;
    }
    if (h == HT_R || h == HT_TR || h == HT_BR)
        r.w = MAX(w->min_w, r.w + dx);
    if (h == HT_T || h == HT_TL || h == HT_TR) {
        int nh = MAX(w->min_h, r.h - dy);
        r.y += r.h - nh;
        r.h = nh;
    }
    if (h == HT_B || h == HT_BL || h == HT_BR)
        r.h = MAX(w->min_h, r.h + dy);
    if (r.x == w->x && r.y == w->y && r.w == w->w && r.h == w->h)
        return;
    ui_invalidate(frame_rect(w));
    w->x = r.x;
    w->y = r.y;
    w->w = r.w;
    w->h = r.h;
    ui_invalidate(frame_rect(w));
    if (w->ops->resized)
        w->ops->resized(w);
}

bool wm_dragging(void) { return drag.mode != DRAG_NONE; }

bool wm_mouse(int kind, int x, int y, int buttons, int button)
{
    if (drag.mode != DRAG_NONE) {
        window_t *w = drag.w;
        switch (drag.mode) {
        case DRAG_MOVE:
            if (kind == MS_DBLCLICK) {
                drag.mode = DRAG_NONE;
                if (!(w->flags & (WF_DIALOG | WF_NO_MAX)) && (w->flags & WF_RESIZABLE))
                    wm_toggle_maximize(w);
                break;
            }
            if (kind == MS_MOVE) {
                int nx = drag.start.x + x - drag.sx, ny = drag.start.y + y - drag.sy;
                ny = CLAMP(ny, 0, g_screen.h - TASKBAR_H - 20);
                nx = CLAMP(nx, 60 - w->w, g_screen.w - 60);
                if (nx != w->x || ny != w->y)
                    wm_move(w, nx, ny);
            }
            if (kind == MS_UP && button == MOUSE_LEFT)
                drag.mode = DRAG_NONE;
            break;
        case DRAG_RESIZE:
            if (kind == MS_MOVE)
                do_resize(x, y);
            if (kind == MS_UP && button == MOUSE_LEFT)
                drag.mode = DRAG_NONE;
            break;
        case DRAG_BUTTON: {
            bool inside = hit_test(w, x, y) == drag.hit;
            if (inside != drag.inside) {
                drag.inside = inside;
                invalidate_caption(w);
            }
            if (kind == MS_UP && button == MOUSE_LEFT) {
                drag.mode = DRAG_NONE;
                invalidate_caption(w);
                if (inside) {
                    if (drag.hit == HT_CLOSE)
                        wm_close(w);
                    else if (drag.hit == HT_MAX)
                        wm_toggle_maximize(w);
                    else if (drag.hit == HT_MIN)
                        wm_minimize(w);
                }
            }
            break;
        }
        case DRAG_CLIENT:
            send_client(w, kind, x, y, button);
            if (kind == MS_UP && !buttons)
                drag.mode = DRAG_NONE;
            if (kind == MS_MOVE)
                ui_set_cursor(w->cursor);
            break;
        }
        if (drag.mode == DRAG_NONE)
            drag.w = NULL;
        return true;
    }

    window_t *w = window_at(x, y);
    if (w != hover_win) {
        if (hover_win && !hover_win->dead && hover_win->ops->mouse)
            hover_win->ops->mouse(hover_win, MS_LEAVE, -1, -1, 0);
        hover_win = w;
    }
    if (!w) {
        set_hot(NULL, HT_NONE);
        return false;
    }
    int ht = hit_test(w, x, y);
    set_hot(w, ht);
    if (kind == MS_MOVE) {
        if (ht == HT_CLIENT)
            send_client(w, kind, x, y, button);
        ui_set_cursor(cursor_for(w, ht));
        return true;
    }

    if (kind == MS_DOWN || kind == MS_DBLCLICK) {
        if (w->modal && wm_alive(w->modal)) {
            if (kind == MS_DOWN) {
                wm_focus(w->modal);
                ui_beep(880, 60);
            }
            return true;
        }
        if (kind == MS_DOWN && w != active)
            wm_focus(w);
        switch (ht) {
        case HT_CAPTION:
            if (kind == MS_DBLCLICK) {
                if ((w->flags & WF_RESIZABLE) && !(w->flags & (WF_DIALOG | WF_NO_MAX)))
                    wm_toggle_maximize(w);
            } else if (button == MOUSE_LEFT && !(w->flags & WF_MAXIMIZED)) {
                drag = (typeof(drag)){ DRAG_MOVE, w, ht, x, y, frame_rect(w), true };
            }
            break;
        case HT_CLOSE:
        case HT_MAX:
        case HT_MIN:
            if (button == MOUSE_LEFT && kind == MS_DOWN) {
                drag = (typeof(drag)){ DRAG_BUTTON, w, ht, x, y, frame_rect(w), true };
                invalidate_caption(w);
            }
            break;
        case HT_L:
        case HT_R:
        case HT_T:
        case HT_B:
        case HT_TL:
        case HT_TR:
        case HT_BL:
        case HT_BR:
            if (button == MOUSE_LEFT && kind == MS_DOWN)
                drag = (typeof(drag)){ DRAG_RESIZE, w, ht, x, y, frame_rect(w), true };
            break;
        case HT_CLIENT:
            send_client(w, kind, x, y, button);
            if (kind == MS_DOWN && !w->dead)
                drag = (typeof(drag)){ DRAG_CLIENT, w, ht, x, y, frame_rect(w), true };
            break;
        }
        return true;
    }
    if (ht == HT_CLIENT && (kind == MS_UP || kind == MS_WHEEL))
        send_client(w, kind, x, y, button);
    return true;
}

void wm_key(const input_event_t *ev)
{
    window_t *w = active;
    if (!w || w->dead || (w->flags & WF_MINIMIZED))
        return;
    if (w->modal && wm_alive(w->modal)) {
        wm_focus(w->modal);
        w = w->modal;
    }
    if (w->ops->key)
        w->ops->key(w, ev);
}

void wm_tick(void)
{
    uint32_t now = timer_ms();
    for (window_t *w = bottom; w; w = w->next) {
        if (w->dead || !w->tick_ms || !w->ops->tick)
            continue;
        if (now - w->tick_last >= w->tick_ms) {
            w->tick_last = now;
            w->ops->tick(w);
        }
    }
    reap();
}
