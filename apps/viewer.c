/* Kepler Picture Viewer: decodes JPEG files from the RAM disk. */

#include "apps.h"
#include "jpeg.h"
#include "ramfs.h"

#define BAR_H 44

enum { VB_PREV, VB_NEXT, VB_FIT, VB_ACTUAL, VB_ZOOMIN, VB_ZOOMOUT, VB_ROTCW, VB_ROTCCW, VB_COUNT };

typedef struct {
    image_t *src;
    image_t *scaled;
    int zoom; /* percent; 0 = best fit */
    char path[128];
    int hover, pressed;
} viewer_t;

static rect_t vb_rect(window_t *w, int i)
{
    rect_t c = wm_client_rect(w);
    int total = VB_COUNT * 34 + 3 * 12;
    int x = (c.w - total) / 2 + i * 34 + (i / 2) * 12;
    return rect_make(x, c.h - BAR_H + 7, 30, 30);
}

static image_t *rotate(const image_t *src, bool cw)
{
    image_t *out = image_new(src->h, src->w);
    for (int y = 0; y < src->h; y++)
        for (int x = 0; x < src->w; x++) {
            int nx = cw ? src->h - 1 - y : y;
            int ny = cw ? x : src->w - 1 - x;
            out->px[ny * out->w + nx] = src->px[y * src->w + x];
        }
    return out;
}

static void target_size(window_t *w, viewer_t *v, int *tw, int *th)
{
    rect_t c = wm_client_rect(w);
    int aw = c.w - 24, ah = c.h - BAR_H - 24;
    if (!v->zoom) {
        int pct = MIN(100, MIN(aw * 100 / v->src->w, ah * 100 / v->src->h));
        *tw = MAX(1, v->src->w * pct / 100);
        *th = MAX(1, v->src->h * pct / 100);
    } else {
        *tw = MAX(1, v->src->w * v->zoom / 100);
        *th = MAX(1, v->src->h * v->zoom / 100);
    }
}

static void glyph(gfx_t *g, int i, int cx, int cy, uint32_t c)
{
    switch (i) {
    case VB_PREV:
    case VB_NEXT: {
        int d = i == VB_PREV ? -1 : 1;
        for (int k = 0; k < 6; k++)
            gfx_vline(g, cx - d * 3 + d * k, cy - 6 + k, 13 - 2 * k, c);
        break;
    }
    case VB_FIT:
        gfx_rect(g, cx - 7, cy - 6, 14, 12, c);
        gfx_rect(g, cx - 4, cy - 3, 8, 6, c);
        break;
    case VB_ACTUAL:
        gfx_rect(g, cx - 7, cy - 6, 14, 12, c);
        gfx_text_centered(g, &font_ui_bold, cx - 7, cy - 6, 14, "1", c);
        break;
    case VB_ZOOMIN:
    case VB_ZOOMOUT:
        gfx_circle(g, cx - 1, cy - 1, 6, c);
        gfx_circle(g, cx - 1, cy - 1, 4, HEX(0xFFFFFF));
        gfx_hline(g, cx - 4, cy - 1, 6, c);
        if (i == VB_ZOOMIN)
            gfx_vline(g, cx - 1, cy - 4, 6, c);
        gfx_thick_line(g, cx + 3, cy + 3, cx + 6, cy + 6, 1, c);
        break;
    case VB_ROTCW:
    case VB_ROTCCW: {
        int d = i == VB_ROTCW ? 1 : -1;
        for (int a = 0; a < 7; a++) {
            gfx_pixel(g, cx - 5 + a, cy + 5 - (a * a) / 8, c);
            gfx_pixel(g, cx + 5 - a, cy + 5 - (a * a) / 8, c);
        }
        gfx_fill(g, cx + d * 4 - 2, cy - 4, 4, 4, c);
        break;
    }
    }
}

static void v_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    viewer_t *v = w->data;
    int area_h = ch - BAR_H;
    gfx_fill(g, 0, 0, cw, area_h, HEX(0xFFFFFF));
    if (v->src) {
        int tw, th;
        target_size(w, v, &tw, &th);
        if (!v->scaled || v->scaled->w != tw || v->scaled->h != th) {
            image_free(v->scaled);
            v->scaled = (tw == v->src->w && th == v->src->h) ? NULL : image_resize(v->src, tw, th);
        }
        const image_t *img = v->scaled ? v->scaled : v->src;
        int x = (cw - img->w) / 2, y = (area_h - img->h) / 2;
        gfx_t ag = gfx_sub(g, 0, 0, cw, area_h);
        gfx_fill(&ag, x + 3, y + 3, img->w, img->h, RGBA(0, 0, 0, 40));
        gfx_blit(&ag, x, y, img);
        gfx_rect(&ag, x - 1, y - 1, img->w + 2, img->h + 2, HEX(0xA0A0A0));
    } else {
        gfx_text_centered(g, &font_ui, 0, area_h / 2 - 7, cw, "Drawing not available. The file could not be decoded.", HEX(0x555555));
    }

    gfx_vgrad(g, 0, area_h, cw, BAR_H, HEX(0xF5F8FD), HEX(0xD4DEF0));
    gfx_hline(g, 0, area_h, cw, HEX(0xA9B8D3));
    for (int i = 0; i < VB_COUNT; i++) {
        rect_t r = vb_rect(w, i);
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        bool hot = v->hover == i, down = hot && v->pressed == i;
        uint32_t top = hot ? HEX(0xFFFFFF) : HEX(0xF8FBFF), bot = down ? HEX(0xA9C3EC) : hot ? HEX(0xD2E2FA) : HEX(0xDCE6F5);
        gfx_circle(g, cx, cy, 15, hot ? HEX(0x5D86C5) : HEX(0x9AB1D6));
        gfx_t bg = gfx_sub(g, 0, 0, cw, ch);
        for (int k = 0; k < 28; k++) {
            uint32_t c = color_lerp(top, bot, k * 256 / 27);
            int half = 0;
            while ((half + 1) * (half + 1) + (k - 14) * (k - 14) <= 13 * 13 + 4)
                half++;
            gfx_fill(&bg, cx - half, cy - 14 + k, 2 * half, 1, c);
        }
        uint32_t gc = (i == VB_PREV || i == VB_NEXT) ? HEX(0x2E8B2E) : HEX(0x2A5BB8);
        glyph(g, i, cx, cy, gc);
    }
}

static void v_mouse(window_t *w, int kind, int x, int y, int button)
{
    viewer_t *v = w->data;
    int h = -1;
    for (int i = 0; i < VB_COUNT; i++) {
        rect_t r = vb_rect(w, i);
        if (rect_contains(r, x, y))
            h = i;
    }
    if (kind == MS_LEAVE)
        h = -1;
    if (h != v->hover) {
        v->hover = h;
        wm_invalidate_client(w);
    }
    if (kind == MS_WHEEL && v->src) {
        int z = v->zoom ? v->zoom : 100;
        v->zoom = CLAMP(z + (button < 0 ? 25 : -25), 25, 400);
        wm_invalidate_client(w);
    }
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        v->pressed = h;
        wm_invalidate_client(w);
    }
    if (kind != MS_UP || button != MOUSE_LEFT)
        return;
    int p = v->pressed;
    v->pressed = -1;
    wm_invalidate_client(w);
    if (p < 0 || p != h || !v->src)
        return;
    int z = v->zoom ? v->zoom : 100;
    switch (p) {
    case VB_FIT: v->zoom = 0; break;
    case VB_ACTUAL: v->zoom = 100; break;
    case VB_ZOOMIN: v->zoom = MIN(400, z + 25); break;
    case VB_ZOOMOUT: v->zoom = MAX(25, z - 25); break;
    case VB_ROTCW:
    case VB_ROTCCW: {
        image_t *r = rotate(v->src, p == VB_ROTCW);
        image_free(v->src);
        image_free(v->scaled);
        v->scaled = NULL;
        v->src = r;
        break;
    }
    case VB_PREV:
    case VB_NEXT:
        ui_beep(660, 40);
        break;
    }
}

static void v_key(window_t *w, const input_event_t *ev)
{
    viewer_t *v = w->data;
    if (!ev->pressed)
        return;
    if (ev->key == '+' || ev->key == '=')
        v->zoom = MIN(400, (v->zoom ? v->zoom : 100) + 25);
    else if (ev->key == '-')
        v->zoom = MAX(25, (v->zoom ? v->zoom : 100) - 25);
    else if (ev->key == '0' || ev->key == KEY_ESC)
        v->zoom = 0;
    else
        return;
    wm_invalidate_client(w);
}

static void v_close(window_t *w)
{
    viewer_t *v = w->data;
    image_free(v->src);
    image_free(v->scaled);
    kfree(v);
}

static void v_resized(window_t *w) { wm_invalidate_client(w); }

static const window_ops_t v_ops = { v_paint, v_key, v_mouse, NULL, v_close, v_resized };

window_t *viewer_open(const char *path)
{
    viewer_t *v = kzalloc(sizeof *v);
    v->hover = v->pressed = -1;
    strlcpy(v->path, path, sizeof v->path);
    fnode_t *f = fs_lookup(path);
    if (f && !f->dir && !jpeg_decode(f->data, f->size, &v->src))
        v->src = NULL;
    char title[128];
    ksnprintf(title, sizeof title, "%s - Kepler Picture Viewer", f ? f->name : path);
    int cw = 640, ch = 470;
    if (v->src) {
        cw = CLAMP(v->src->w + 60, 480, 1000);
        ch = CLAMP(v->src->h + BAR_H + 60, 360, 640);
    }
    window_t *w = wm_create(title, ICON_PICTURES, cw, ch, &v_ops, v, WF_RESIZABLE);
    w->bg = HEX(0xFFFFFF);
    w->min_w = 420;
    w->min_h = 260;
    return w;
}
