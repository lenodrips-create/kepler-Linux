/* Window manager: Big Sur-style frames, z-order, focus, move/resize, minimise to the dock, zoom. */

#include "ui.h"

static window_t *bottom; /* z-order list, bottom first */
static window_t *active;
static int next_id = 1;
static int cascade;

enum { HT_NONE, HT_CLIENT, HT_CAPTION, HT_CLOSE, HT_MAX, HT_MIN, HT_L, HT_R, HT_T, HT_B, HT_TL, HT_TR, HT_BL, HT_BR,
       HT_MENU0 = 100 };

/* with no menu bar across the screen, a window's menus sit in its own title bar */
#define MENU_X0  78
#define MENU_PAD 18
enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_BUTTON, DRAG_CLIENT };

/* the active window's shadow; inactive ones get a tighter, lighter one */
#define SH_BLUR  22
#define SH_DY    10
#define GRIP_OUT 5 /* resize grips reach this far outside the frame... */
#define GRIP_IN  3 /* ...and this far in */

static struct {
    int mode;
    window_t *w;
    int hit;
    int sx, sy;
    rect_t start;
    bool inside;
} drag;

static window_t *hover_win;
static window_t *hot_win; /* the mouse is over this window's traffic lights */
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

/* everything a window paints, shadow included */
static rect_t outer_rect(const window_t *w)
{
    if (w->flags & WF_FULLSCREEN)
        return rect_make(w->x, w->y, w->w, w->h);
    return rect_make(w->x - SH_BLUR, w->y - SH_BLUR + SH_DY, w->w + 2 * SH_BLUR, w->h + 2 * SH_BLUR);
}

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

static void invalidate_outer(window_t *w)
{
    if (w && visible(w))
        ui_invalidate(outer_rect(w));
}

void wm_deactivate(void)
{
    window_t *old = active;
    active = NULL;
    if (old) {
        invalidate_outer(old);
        shell_windows_changed();
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

/* ---- apps --------------------------------------------------------------------------- */

void wm_set_app(window_t *w, const char *app, struct menubar *menus, void (*fn)(int id, void *ctx), void *ctx)
{
    strlcpy(w->app, app, sizeof w->app);
    w->menus = menus;
    w->menu_fn = fn;
    w->menu_ctx = ctx;
    shell_windows_changed();
}

window_t *wm_menu_owner(void)
{
    window_t *w = active;
    while (w && !w->menus && w->owner && wm_alive(w->owner))
        w = w->owner;
    return w && !w->dead ? w : NULL;
}

window_t *wm_app_window(const char *app)
{
    window_t *shown = NULL, *any = NULL;
    for (window_t *w = bottom; w; w = w->next) {
        if (w->dead || strcmp(w->app, app))
            continue;
        any = w;
        if (visible(w))
            shown = w;
    }
    return shown ? shown : any;
}

void wm_hide_app(const char *app)
{
    for (window_t *w = bottom; w; w = w->next)
        if (!w->dead && !strcmp(w->app, app))
            wm_minimize(w);
}

/* ---- geometry ------------------------------------------------------------------- */

static int caption_h(const window_t *w)
{
    if (w->flags & WF_FULLSCREEN)
        return 0;
    return (w->flags & WF_UNIFIED) ? w->caption_h : TITLE_H;
}

static bool has_menus(const window_t *w)
{
    return w->menus && w->menus->n > 0 && !(w->flags & (WF_UNIFIED | WF_FULLSCREEN));
}

/* local coordinates of title-bar menu i */
static rect_t menu_title_rect(const window_t *w, int i)
{
    int x = MENU_X0;
    for (int k = 0; k < i; k++)
        x += text_width(&font_menu, w->menus->entries[k].title) + MENU_PAD;
    return rect_make(x, 4, text_width(&font_menu, w->menus->entries[i].title) + MENU_PAD, TITLE_H - 8);
}

static int menus_end(const window_t *w)
{
    if (!has_menus(w))
        return 70;
    rect_t r = menu_title_rect(w, w->menus->n - 1);
    return r.x + r.w + 12;
}

rect_t wm_client_rect(const window_t *w)
{
    if (w->flags & (WF_UNIFIED | WF_FULLSCREEN))
        return frame_rect(w);
    return rect_make(w->x, w->y + TITLE_H, w->w, w->h - TITLE_H);
}

/* traffic lights, left to right: 0 close, 1 minimise, 2 zoom (local coordinates) */
static rect_t light_rect(const window_t *w, int i)
{
    int cy = MIN(caption_h(w), 52) / 2;
    return rect_make(13 + i * 20, cy - 7, 14, 14);
}

static bool light_enabled(const window_t *w, int i)
{
    if (i == 0)
        return true;
    if (w->flags & WF_DIALOG)
        return false;
    return i == 1 || !(w->flags & WF_NO_MAX);
}

static int hit_test(window_t *w, int x, int y)
{
    int lx = x - w->x, ly = y - w->y;
    if (w->flags & WF_FULLSCREEN)
        return (lx < 0 || ly < 0 || lx >= w->w || ly >= w->h) ? HT_NONE : HT_CLIENT;
    bool sizing = (w->flags & WF_RESIZABLE) && !(w->flags & WF_MAXIMIZED);
    if (sizing) {
        if (lx < -GRIP_OUT || ly < -GRIP_OUT || lx >= w->w + GRIP_OUT || ly >= w->h + GRIP_OUT)
            return HT_NONE;
        const int c = 12;
        bool left = lx < GRIP_IN, right = lx >= w->w - GRIP_IN, top = ly < GRIP_IN, bot = ly >= w->h - GRIP_IN;
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
    } else if (lx < 0 || ly < 0 || lx >= w->w || ly >= w->h) {
        return HT_NONE;
    }
    if (ly < caption_h(w)) {
        for (int i = 0; i < 3 && !(w->flags & WF_ALERT); i++)
            if (rect_contains(light_rect(w, i), lx, ly) && light_enabled(w, i))
                return i == 0 ? HT_CLOSE : i == 1 ? HT_MIN : HT_MAX;
        if ((w->flags & WF_UNIFIED) && w->ops->caption_hit && w->ops->caption_hit(w, lx, ly))
            return HT_CLIENT;
        for (int i = 0; has_menus(w) && i < w->menus->n; i++)
            if (rect_contains(menu_title_rect(w, i), lx, ly))
                return HT_MENU0 + i;
        return HT_CAPTION;
    }
    return HT_CLIENT;
}

static window_t *window_at(int x, int y)
{
    window_t *hit = NULL;
    for (window_t *w = bottom; w; w = w->next)
        if (visible(w) && hit_test(w, x, y) != HT_NONE)
            hit = w;
    return hit;
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

static void invalidate_lights(window_t *w)
{
    if (w && visible(w))
        ui_invalidate(rect_make(w->x, w->y, 80, caption_h(w)));
}

/* ---- lifecycle --------------------------------------------------------------------- */

window_t *wm_create(const char *title, int icon, int cw, int ch, const window_ops_t *ops, void *data, uint32_t flags)
{
    window_t *w = kzalloc(sizeof *w);
    w->id = next_id++;
    strlcpy(w->title, title, sizeof w->title);
    strlcpy(w->app, title, sizeof w->app);
    w->icon = icon;
    w->ops = ops;
    w->data = data;
    w->bg = COL_FACE;
    w->w = cw;
    if (flags & WF_ALERT)
        flags |= WF_UNIFIED;
    w->flags = flags;
    w->h = ch + ((flags & WF_UNIFIED) ? 0 : TITLE_H);
    w->caption_h = (flags & WF_ALERT) ? ch : TITLE_H;
    w->min_w = 200;
    w->min_h = 120;
    w->cursor = CURSOR_ARROW;
    w->created = timer_ms();

    rect_t a = ui_work_area();
    w->x = a.x + 130 + (cascade % 8) * 26;
    w->y = a.y + 28 + (cascade % 8) * 26;
    cascade++;
    if (w->x + w->w > a.x + a.w)
        w->x = MAX(a.x, a.x + a.w - w->w);
    if (w->y + w->h > a.y + a.h)
        w->y = MAX(a.y, a.y + a.h - w->h);

    push_top(w);
    wm_focus(w);
    shell_windows_changed();
    return w;
}

void wm_center(window_t *w)
{
    rect_t a = ui_work_area();
    invalidate_outer(w);
    w->x = a.x + (a.w - w->w) / 2;
    w->y = MAX(a.y, a.y + (a.h - w->h) / 2 - 20);
    invalidate_outer(w);
}

void wm_move(window_t *w, int x, int y)
{
    invalidate_outer(w);
    w->x = x;
    w->y = y;
    invalidate_outer(w);
}

void wm_set_client_size(window_t *w, int cw, int ch)
{
    invalidate_outer(w);
    w->w = cw;
    w->h = ch + ((w->flags & WF_UNIFIED) ? 0 : TITLE_H);
    invalidate_outer(w);
    if (w->ops->resized)
        w->ops->resized(w);
}

void wm_set_title(window_t *w, const char *title)
{
    strlcpy(w->title, title, sizeof w->title);
    if (visible(w))
        ui_invalidate(rect_make(w->x, w->y, w->w, caption_h(w)));
    shell_windows_changed();
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
    strlcpy(dialog->app, owner->app, sizeof dialog->app);
}

void wm_focus(window_t *w)
{
    if (!w || w->dead)
        return;
    if (w->modal && wm_alive(w->modal))
        w = w->modal;
    if (w->flags & WF_MINIMIZED) {
        w->flags &= ~WF_MINIMIZED;
        image_free(w->thumb);
        w->thumb = NULL;
    }
    window_t *old = active;
    /* raise the owner first so its dialog stays above it */
    if (w->owner && wm_alive(w->owner) && !(w->owner->flags & WF_MINIMIZED)) {
        unlink(w->owner);
        push_top(w->owner);
        invalidate_outer(w->owner);
    }
    unlink(w);
    push_top(w);
    active = w;
    invalidate_outer(w);
    if (old && old != w)
        invalidate_outer(old);
    shell_windows_changed();
}

void wm_close(window_t *w)
{
    if (!w || w->dead)
        return;
    if (w->ops->close_request && !w->closing) {
        w->ops->close_request(w);
        return;
    }
    if (w->modal && wm_alive(w->modal))
        wm_close(w->modal);
    invalidate_outer(w);
    w->dead = true;
    if (w->ops->close)
        w->ops->close(w);
    if (w->owner && w->owner->modal == w)
        w->owner->modal = NULL;
    image_free(w->thumb);
    w->thumb = NULL;
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
    shell_windows_changed();
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

/* ---- painting ------------------------------------------------------------------------ */

#define CR WIN_RADIUS

/* What sits under a window's four rounded corners is saved before the window
 * paints and blended back afterwards, so apps can fill their client area
 * edge to edge and still get round corners. */
typedef uint32_t corners_t[4][CR * CR];

static void corners_save(gfx_t *g, int W, int H, corners_t s)
{
    for (int k = 0; k < 4; k++) {
        int cx = (k & 1) ? W - CR : 0, cy = (k & 2) ? H - CR : 0;
        for (int j = 0; j < CR; j++)
            for (int i = 0; i < CR; i++) {
                int ax = g->ox + cx + i, ay = g->oy + cy + j;
                if (rect_contains(g->clip, ax, ay))
                    s[k][j * CR + i] = g->px[ay * g->stride + ax];
            }
    }
}

static void corners_restore(gfx_t *g, int W, int H, corners_t s)
{
    for (int k = 0; k < 4; k++) {
        int cx = (k & 1) ? W - CR : 0, cy = (k & 2) ? H - CR : 0;
        for (int j = 0; j < CR; j++)
            for (int i = 0; i < CR; i++) {
                int ax = g->ox + cx + i, ay = g->oy + cy + j;
                if (!rect_contains(g->clip, ax, ay))
                    continue;
                int cov = gfx_round_coverage(cx + i, cy + j, W, H, CR, CORNER_ALL);
                if (cov < 255) {
                    uint32_t *p = &g->px[ay * g->stride + ax];
                    *p = color_lerp(s[k][j * CR + i], *p, cov * 256 / 255);
                }
            }
    }
}

static void paint_titlebar(gfx_t *g, window_t *w, bool act)
{
    int W = w->w;
    if (act)
        gfx_vgrad(g, 0, 0, W, TITLE_H, HEX(0xEDEDED), HEX(0xE2E2E2));
    else
        gfx_fill(g, 0, 0, W, TITLE_H, HEX(0xF6F6F6));
    gfx_hline(g, 0, TITLE_H - 1, W, act ? HEX(0xD1D1D1) : HEX(0xE2E2E2));
    if (has_menus(w)) {
        const menu_item_t *open = menu_current();
        for (int i = 0; i < w->menus->n; i++) {
            rect_t r = menu_title_rect(w, i);
            if (open && open == w->menus->entries[i].items)
                gfx_round_rect(g, r.x, r.y, r.w, r.h, 5, RGBA(0, 0, 0, 30), CORNER_ALL);
            gfx_text(g, &font_menu, r.x + MENU_PAD / 2, (TITLE_H - font_menu.height) / 2, w->menus->entries[i].title,
                     act ? HEX(0x1D1D1F) : HEX(0xA0A0A0));
        }
    }
    /* centred on the window, but never under the traffic lights or the menus */
    int tw = text_width(&font_title, w->title);
    int tx = MAX(menus_end(w), (W - tw) / 2);
    int ty = (TITLE_H - font_title.height) / 2;
    gfx_text_clipped(g, &font_title, tx, ty, W - 12 - tx, w->title, act ? HEX(0x404040) : HEX(0xB2B2B2));
}

static void paint_lights(gfx_t *g, window_t *w, bool act)
{
    static const uint32_t fills[3] = { 0xFF5F57, 0xFEBC2E, 0x28C840 };
    static const uint32_t edges[3] = { 0xE0443E, 0xDEA123, 0x1AAB29 };
    static const uint32_t marks[3] = { 0x4D0000, 0x985712, 0x0A5F14 };
    static const int glyphs[3] = { GLYPH_TL_CLOSE, GLYPH_TL_MIN, GLYPH_TL_ZOOM };
    static const int hts[3] = { HT_CLOSE, HT_MIN, HT_MAX };
    bool hot = hot_win == w;
    for (int i = 0; i < 3; i++) {
        rect_t r = light_rect(w, i);
        int cx = r.x + 7, cy = r.y + 7;
        bool en = light_enabled(w, i);
        uint32_t fill = HEX(fills[i]), edge = HEX(edges[i]);
        if (!en) {
            fill = HEX(0xE3E3E3);
            edge = HEX(0xD0D0D0);
        } else if (!act && !hot) {
            fill = HEX(0xDDDDDD);
            edge = HEX(0xC8C8C8);
        }
        bool pressed = en && drag.mode == DRAG_BUTTON && drag.w == w && drag.hit == hts[i] && drag.inside;
        if (pressed) {
            fill = color_shade(fill, 78);
            edge = color_shade(edge, 78);
        }
        gfx_circle(g, cx, cy, 6, edge);
        gfx_circle(g, cx, cy, 5, fill);
        if (hot && en)
            gfx_blit_tint(g, cx - 6, cy - 6, icon_get(glyphs[i], 12), WITH_ALPHA(HEX(marks[i]), 210));
    }
}

static void paint_window(gfx_t *g, window_t *w, bool act, bool shadow)
{
    if (w->flags & WF_FULLSCREEN) {
        gfx_t cg = gfx_sub(g, w->x, w->y, w->w, w->h);
        gfx_fill(&cg, 0, 0, w->w, w->h, w->bg);
        if (w->ops->paint)
            w->ops->paint(w, &cg, w->w, w->h);
        return;
    }
    if (shadow) {
        if (act)
            gfx_shadow(g, w->x, w->y, w->w, w->h, CR, SH_BLUR, SH_DY, 78);
        else
            gfx_shadow(g, w->x, w->y, w->w, w->h, CR, 12, 4, 46);
    }
    gfx_t wg = gfx_sub(g, w->x, w->y, w->w, w->h);
    corners_t saved;
    corners_save(&wg, w->w, w->h, saved);
    if (!(w->flags & WF_UNIFIED))
        paint_titlebar(&wg, w, act);
    rect_t c = wm_client_rect(w);
    gfx_t cg = gfx_sub(g, c.x, c.y, c.w, c.h);
    gfx_fill(&cg, 0, 0, c.w, c.h, w->bg);
    if (w->ops->paint)
        w->ops->paint(w, &cg, c.w, c.h);
    if (!(w->flags & WF_ALERT))
        paint_lights(&wg, w, act);
    corners_restore(&wg, w->w, w->h, saved);
    gfx_round_frame(&wg, 0, 0, w->w, w->h, CR, RGBA(0, 0, 0, act ? 64 : 44), CORNER_ALL);
    gfx_hline(&wg, CR, 1, w->w - 2 * CR, RGBA(255, 255, 255, 150));
}

void wm_paint(gfx_t *g, rect_t dirty)
{
    for (window_t *w = bottom; w; w = w->next) {
        if (!visible(w) || rect_empty(rect_intersect(outer_rect(w), dirty)))
            continue;
        paint_window(g, w, w == active, true);
    }
}

/* the picture the dock shows while a window is minimised */
static void snapshot(window_t *w)
{
    image_free(w->thumb);
    w->thumb = NULL;
    int W = w->w, H = w->h;
    if (W < 16 || H < 16)
        return;
    image_t full = { kzalloc((size_t)W * H * 4), W, H };
    gfx_t g = gfx_for_buffer(full.px, W, H, W);
    int ox = w->x, oy = w->y;
    w->x = w->y = 0;
    paint_window(&g, w, true, false);
    w->x = ox;
    w->y = oy;
    int tw = 44, th = 44;
    if (W >= H)
        th = MAX(8, 44 * H / W);
    else
        tw = MAX(8, 44 * W / H);
    w->thumb = image_resize(&full, tw, th);
    kfree(full.px);
}

/* ---- minimise, zoom, cycle ------------------------------------------------------------ */

void wm_minimize(window_t *w)
{
    if (!w || (w->flags & WF_MINIMIZED))
        return;
    snapshot(w);
    invalidate_outer(w);
    w->flags |= WF_MINIMIZED;
    if (active == w) {
        active = NULL;
        window_t *next = topmost_visible();
        if (next)
            wm_focus(next);
    }
    shell_windows_changed();
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
    if (w->flags & WF_FULLSCREEN) {
        wm_set_fullscreen(w, false);
        return;
    }
    invalidate_outer(w);
    if (w->flags & WF_MAXIMIZED) {
        w->flags &= ~WF_MAXIMIZED;
        w->x = w->restore.x;
        w->y = w->restore.y;
        w->w = w->restore.w;
        w->h = w->restore.h;
    } else {
        w->restore = frame_rect(w);
        w->flags |= WF_MAXIMIZED;
        rect_t a = ui_work_area();
        w->x = a.x;
        w->y = a.y;
        w->w = a.w;
        w->h = a.h;
    }
    invalidate_outer(w);
    if (w->ops->resized)
        w->ops->resized(w);
}

void wm_set_fullscreen(window_t *w, bool on)
{
    if (!w || w->dead || on == !!(w->flags & WF_FULLSCREEN))
        return;
    invalidate_outer(w);
    if (on) {
        w->restore = frame_rect(w);
        w->flags |= WF_FULLSCREEN;
        w->x = w->y = 0;
        w->w = g_screen.w;
        w->h = g_screen.h;
        wm_focus(w);
    } else {
        w->flags &= ~WF_FULLSCREEN;
        w->x = w->restore.x;
        w->y = w->restore.y;
        w->w = w->restore.w;
        w->h = w->restore.h;
    }
    ui_invalidate_all();
    if (w->ops->resized)
        w->ops->resized(w);
    shell_windows_changed();
}

window_t *wm_fullscreen(void)
{
    window_t *a = active;
    return a && !a->dead && (a->flags & WF_FULLSCREEN) && !(a->flags & WF_MINIMIZED) ? a : NULL;
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
        invalidate_lights(hot_win);
    hot_win = w;
    hot_hit = ht;
    if (w)
        invalidate_lights(w);
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
        int ny = MAX(TOPBAR_H, r.y + r.h - nh);
        r.h = r.y + r.h - ny;
        r.y = ny;
    }
    if (h == HT_B || h == HT_BL || h == HT_BR)
        r.h = MAX(w->min_h, r.h + dy);
    if (r.x == w->x && r.y == w->y && r.w == w->w && r.h == w->h)
        return;
    invalidate_outer(w);
    w->x = r.x;
    w->y = r.y;
    w->w = r.w;
    w->h = r.h;
    invalidate_outer(w);
    if (w->ops->resized)
        w->ops->resized(w);
}

bool wm_dragging(void) { return drag.mode != DRAG_NONE; }

static window_t *menu_win;
static const menu_item_t *menu_shown;

static void open_title_menu(window_t *w, int i);

/* dragging along the title bar with a menu open switches menus */
static void menu_slide(int x, int y)
{
    window_t *w = menu_win;
    if (!w || !wm_alive(w) || !has_menus(w))
        return;
    for (int i = 0; i < w->menus->n; i++) {
        rect_t r = menu_title_rect(w, i);
        if (rect_contains(r, x - w->x, y - w->y) && menu_current() != w->menus->entries[i].items) {
            open_title_menu(w, i);
            return;
        }
    }
}

static void open_title_menu(window_t *w, int i)
{
    rect_t r = menu_title_rect(w, i);
    const menubar_entry_t *e = &w->menus->entries[i];
    menu_open_bar(e->items, e->n, w->x + r.x, w->y + TITLE_H - 2, w->menu_fn, w->menu_ctx, rect_make(w->x, w->y, w->w, TITLE_H),
                  menu_slide);
    menu_win = w;
    menu_shown = e->items;
    ui_invalidate(rect_make(w->x, w->y, w->w, TITLE_H));
}

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
                ny = CLAMP(ny, TOPBAR_H, g_screen.h - 40);
                nx = CLAMP(nx, 60 - w->w, g_screen.w - 60);
                if (nx != w->x || ny != w->y) {
                    w->flags &= ~WF_MAXIMIZED; /* a zoomed window dragged away is a normal one again */
                    wm_move(w, nx, ny);
                }
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
                invalidate_lights(w);
            }
            if (kind == MS_UP && button == MOUSE_LEFT) {
                drag.mode = DRAG_NONE;
                invalidate_lights(w);
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
        if (ht >= HT_MENU0 && kind == MS_DOWN) {
            open_title_menu(w, ht - HT_MENU0);
            return true;
        }
        switch (ht) {
        case HT_CAPTION:
            if (kind == MS_DBLCLICK) {
                if ((w->flags & WF_RESIZABLE) && !(w->flags & (WF_DIALOG | WF_NO_MAX)))
                    wm_toggle_maximize(w);
            } else if (button == MOUSE_LEFT) {
                drag = (typeof(drag)){ DRAG_MOVE, w, ht, x, y, frame_rect(w), true };
            }
            break;
        case HT_CLOSE:
        case HT_MAX:
        case HT_MIN:
            if (button == MOUSE_LEFT && kind == MS_DOWN) {
                drag = (typeof(drag)){ DRAG_BUTTON, w, ht, x, y, frame_rect(w), true };
                invalidate_lights(w);
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
    /* a title-bar menu closed: drop its highlight */
    if (menu_win && menu_current() != menu_shown) {
        if (wm_alive(menu_win))
            ui_invalidate(rect_make(menu_win->x, menu_win->y, menu_win->w, TITLE_H));
        menu_shown = menu_current();
        if (!menu_shown)
            menu_win = NULL;
    }
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
