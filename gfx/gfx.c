/* 2D drawing primitives on 32-bit pixel buffers. */

#include "gfx.h"

/* ---- rects ------------------------------------------------------------------ */

rect_t rect_intersect(rect_t a, rect_t b)
{
    int x0 = MAX(a.x, b.x), y0 = MAX(a.y, b.y);
    int x1 = MIN(a.x + a.w, b.x + b.w), y1 = MIN(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return (rect_t){ 0, 0, 0, 0 };
    return (rect_t){ x0, y0, x1 - x0, y1 - y0 };
}

rect_t rect_union(rect_t a, rect_t b)
{
    if (rect_empty(a))
        return b;
    if (rect_empty(b))
        return a;
    int x0 = MIN(a.x, b.x), y0 = MIN(a.y, b.y);
    int x1 = MAX(a.x + a.w, b.x + b.w), y1 = MAX(a.y + a.h, b.y + b.h);
    return (rect_t){ x0, y0, x1 - x0, y1 - y0 };
}

bool rect_empty(rect_t r) { return r.w <= 0 || r.h <= 0; }
bool rect_contains(rect_t r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

/* ---- colors ----------------------------------------------------------------- */

uint32_t color_lerp(uint32_t a, uint32_t b, int t)
{
    if (t <= 0)
        return a;
    if (t >= 256)
        return b;
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        r |= (uint32_t)(ca + (((cb - ca) * t) >> 8)) << sh;
    }
    return r;
}

uint32_t color_shade(uint32_t c, int pct)
{
    uint32_t out = c & 0xFF000000u;
    for (int sh = 0; sh < 24; sh += 8) {
        int v = (c >> sh) & 0xFF;
        v = pct <= 100 ? v * pct / 100 : v + (255 - v) * (pct - 100) / 100;
        out |= (uint32_t)CLAMP(v, 0, 255) << sh;
    }
    return out;
}

/* blend src over an opaque destination, a = 0..255 */
static inline uint32_t mix(uint32_t d, uint32_t s, uint32_t a)
{
    uint32_t ia = 255 - a;
    uint32_t rb = (s & 0xFF00FF) * a + (d & 0xFF00FF) * ia + 0x800080;
    rb = ((rb + ((rb >> 8) & 0xFF00FF)) >> 8) & 0xFF00FF;
    uint32_t g = (s & 0x00FF00) * a + (d & 0x00FF00) * ia + 0x008000;
    g = ((g + ((g >> 8) & 0x00FF00)) >> 8) & 0x00FF00;
    return 0xFF000000u | rb | g;
}

/* Porter-Duff "over" for straight-alpha ARGB destinations */
static inline uint32_t over(uint32_t d, uint32_t s, uint32_t a)
{
    uint32_t da = d >> 24;
    if (a >= 255 || da == 0)
        return (s & 0xFFFFFF) | (a << 24);
    uint32_t ws = a * 255, wd = da * (255 - a);
    uint32_t sum = ws + wd;
    uint32_t r = (((s >> 16) & 0xFF) * ws + ((d >> 16) & 0xFF) * wd) / sum;
    uint32_t gg = (((s >> 8) & 0xFF) * ws + ((d >> 8) & 0xFF) * wd) / sum;
    uint32_t b = ((s & 0xFF) * ws + (d & 0xFF) * wd) / sum;
    uint32_t oa = (sum + 127) / 255;
    return (oa << 24) | (r << 16) | (gg << 8) | b;
}

static inline void put(gfx_t *g, uint32_t *p, uint32_t c, uint32_t a)
{
    if (a == 0)
        return;
    if (g->argb)
        *p = over(*p, c, a);
    else
        *p = a >= 255 ? (c | 0xFF000000u) : mix(*p, c, a);
}

/* ---- images and contexts ----------------------------------------------------- */

image_t *image_new(int w, int h)
{
    image_t *img = kmalloc(sizeof *img);
    img->w = w;
    img->h = h;
    img->px = kzalloc((size_t)w * h * 4);
    return img;
}

void image_free(image_t *img)
{
    if (!img)
        return;
    kfree(img->px);
    kfree(img);
}

gfx_t gfx_for_buffer(uint32_t *px, int w, int h, int stride)
{
    gfx_t g = { px, w, h, stride, 0, 0, { 0, 0, w, h }, false };
    return g;
}

gfx_t gfx_for_image(image_t *img)
{
    gfx_t g = gfx_for_buffer(img->px, img->w, img->h, img->w);
    g.argb = true;
    return g;
}

gfx_t gfx_sub(const gfx_t *g, int x, int y, int w, int h)
{
    gfx_t s = *g;
    s.ox = g->ox + x;
    s.oy = g->oy + y;
    s.clip = rect_intersect(g->clip, rect_make(s.ox, s.oy, w, h));
    return s;
}

/* clip a local rect, returning it in absolute coordinates */
static inline bool clip_rect(const gfx_t *g, int *x, int *y, int *w, int *h)
{
    rect_t r = rect_intersect(g->clip, rect_make(*x + g->ox, *y + g->oy, *w, *h));
    if (rect_empty(r))
        return false;
    *x = r.x;
    *y = r.y;
    *w = r.w;
    *h = r.h;
    return true;
}

/* ---- basic shapes ------------------------------------------------------------- */

void gfx_pixel(gfx_t *g, int x, int y, uint32_t c)
{
    x += g->ox;
    y += g->oy;
    if (!rect_contains(g->clip, x, y))
        return;
    put(g, &g->px[y * g->stride + x], c, ALPHA(c));
}

static void pixel_cov(gfx_t *g, int x, int y, uint32_t c, int cov)
{
    x += g->ox;
    y += g->oy;
    if (!rect_contains(g->clip, x, y))
        return;
    put(g, &g->px[y * g->stride + x], c, (ALPHA(c) * cov + 127) / 255);
}

void gfx_fill(gfx_t *g, int x, int y, int w, int h, uint32_t c)
{
    if (!clip_rect(g, &x, &y, &w, &h))
        return;
    uint32_t a = ALPHA(c);
    if (a == 0)
        return;
    for (int j = 0; j < h; j++) {
        uint32_t *row = &g->px[(y + j) * g->stride + x];
        if (a == 255 && !g->argb) {
            memset32(row, c, w);
        } else {
            for (int i = 0; i < w; i++)
                put(g, &row[i], c, a);
        }
    }
}

void gfx_clear(gfx_t *g, uint32_t c)
{
    for (int j = 0; j < g->h; j++)
        memset32(&g->px[j * g->stride], c, g->w);
}

void gfx_hline(gfx_t *g, int x, int y, int w, uint32_t c) { gfx_fill(g, x, y, w, 1, c); }
void gfx_vline(gfx_t *g, int x, int y, int h, uint32_t c) { gfx_fill(g, x, y, 1, h, c); }

void gfx_rect(gfx_t *g, int x, int y, int w, int h, uint32_t c)
{
    gfx_hline(g, x, y, w, c);
    gfx_hline(g, x, y + h - 1, w, c);
    gfx_vline(g, x, y + 1, h - 2, c);
    gfx_vline(g, x + w - 1, y + 1, h - 2, c);
}

void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = ABS(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -ABS(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(g, x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void disc(gfx_t *g, int cx, int cy, int r, uint32_t c)
{
    for (int y = -r; y <= r; y++) {
        int w = 0;
        while ((w + 1) * (w + 1) + y * y <= r * r + r)
            w++;
        gfx_fill(g, cx - w, cy + y, 2 * w + 1, 1, c);
    }
}

void gfx_thick_line(gfx_t *g, int x0, int y0, int x1, int y1, int r, uint32_t c)
{
    if (r <= 0) {
        gfx_line(g, x0, y0, x1, y1, c);
        return;
    }
    int dx = ABS(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -ABS(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        disc(g, x0, y0, r, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_vgrad(gfx_t *g, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    for (int j = 0; j < h; j++) {
        int t = h > 1 ? j * 256 / (h - 1) : 0;
        gfx_fill(g, x, y + j, w, 1, color_lerp(top, bottom, t));
    }
}

void gfx_hgrad(gfx_t *g, int x, int y, int w, int h, uint32_t left, uint32_t right)
{
    for (int i = 0; i < w; i++) {
        int t = w > 1 ? i * 256 / (w - 1) : 0;
        gfx_fill(g, x + i, y, 1, h, color_lerp(left, right, t));
    }
}

/* stops are positions 0..255 along the height, ascending */
void gfx_vgrad_stops(gfx_t *g, int x, int y, int w, int h, const uint32_t *colors, const int *stops, int n)
{
    for (int j = 0; j < h; j++)
        gfx_fill(g, x, y + j, w, 1, gradient_at(colors, stops, n, h > 1 ? j * 255 / (h - 1) : 0));
}

/* ---- antialiased rounded corners ------------------------------------------------- */

#define MAX_RADIUS 40
static uint8_t *corner_masks[MAX_RADIUS + 1];

/* coverage of the top-left corner box of a radius-r rounding, 4x4 supersampled */
static const uint8_t *corner_mask(int r)
{
    if (r > MAX_RADIUS)
        r = MAX_RADIUS;
    if (corner_masks[r])
        return corner_masks[r];
    uint8_t *m = kmalloc(r * r + 1);
    int c = r * 8, rr = c * c;
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            int n = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    int px = i * 8 + sx * 2 + 1 - c, py = j * 8 + sy * 2 + 1 - c;
                    if (px * px + py * py <= rr)
                        n++;
                }
            m[j * r + i] = n * 255 / 16;
        }
    corner_masks[r] = m;
    return m;
}

/* coverage (0..255) of pixel (i, j) inside a w x h rounded rect */
static int round_cov(int i, int j, int w, int h, int r, int corners)
{
    if (i < 0 || j < 0 || i >= w || j >= h)
        return 0;
    if (r <= 0)
        return 255;
    const uint8_t *m = corner_mask(r);
    int ci = -1, cj = -1;
    if (i < r && j < r && (corners & 1)) {
        ci = i;
        cj = j;
    } else if (i >= w - r && j < r && (corners & 2)) {
        ci = w - 1 - i;
        cj = j;
    } else if (i < r && j >= h - r && (corners & 4)) {
        ci = i;
        cj = h - 1 - j;
    } else if (i >= w - r && j >= h - r && (corners & 8)) {
        ci = w - 1 - i;
        cj = h - 1 - j;
    }
    if (ci < 0)
        return 255;
    return m[cj * r + ci];
}

uint32_t gradient_at(const uint32_t *colors, const int *stops, int n, int pos)
{
    if (pos <= stops[0])
        return colors[0];
    for (int k = 0; k < n - 1; k++) {
        if (pos <= stops[k + 1]) {
            int span = stops[k + 1] - stops[k];
            int t = span > 0 ? (pos - stops[k]) * 256 / span : 0;
            return color_lerp(colors[k], colors[k + 1], t);
        }
    }
    return colors[n - 1];
}

void gfx_round_rect_stops(gfx_t *g, int x, int y, int w, int h, int r, const uint32_t *colors, const int *stops, int n,
                          int corners)
{
    if (w <= 0 || h <= 0)
        return;
    r = MIN(r, MIN(w, h) / 2);
    r = MIN(r, MAX_RADIUS);
    for (int j = 0; j < h; j++) {
        uint32_t c = gradient_at(colors, stops, n, h > 1 ? j * 255 / (h - 1) : 0);
        bool corner_row = j < r || j >= h - r;
        if (!corner_row) {
            gfx_fill(g, x, y + j, w, 1, c);
            continue;
        }
        for (int i = 0; i < r; i++) {
            pixel_cov(g, x + i, y + j, c, round_cov(i, j, w, h, r, corners));
            pixel_cov(g, x + w - 1 - i, y + j, c, round_cov(w - 1 - i, j, w, h, r, corners));
        }
        gfx_fill(g, x + r, y + j, w - 2 * r, 1, c);
    }
}

void gfx_round_rect_vgrad(gfx_t *g, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom, int corners)
{
    const uint32_t colors[2] = { top, bottom };
    const int stops[2] = { 0, 255 };
    gfx_round_rect_stops(g, x, y, w, h, r, colors, stops, 2, corners);
}

void gfx_round_rect(gfx_t *g, int x, int y, int w, int h, int r, uint32_t c, int corners)
{
    gfx_round_rect_vgrad(g, x, y, w, h, r, c, c, corners);
}

void gfx_round_frame(gfx_t *g, int x, int y, int w, int h, int r, uint32_t c, int corners)
{
    if (w <= 2 || h <= 2)
        return;
    r = MIN(r, MIN(w, h) / 2);
    r = MIN(r, MAX_RADIUS);
    int ir = r > 1 ? r - 1 : 0;
    for (int j = 0; j < h; j++) {
        bool corner_row = j < r || j >= h - r;
        if (!corner_row) {
            gfx_pixel(g, x, y + j, c);
            gfx_pixel(g, x + w - 1, y + j, c);
            continue;
        }
        for (int i = 0; i < w; i++) {
            if (i >= r && i < w - r && j > 0 && j < h - 1)
                continue;
            int outer = round_cov(i, j, w, h, r, corners);
            int inner = round_cov(i - 1, j - 1, w - 2, h - 2, ir, corners);
            int cov = outer - inner;
            if (cov > 0)
                pixel_cov(g, x + i, y + j, c, cov);
        }
    }
}

void gfx_circle(gfx_t *g, int cx, int cy, int r, uint32_t c)
{
    /* the circle covers [cx - r, cx + r) on both axes */
    int c8 = r * 8, rr = c8 * c8;
    for (int j = 0; j < 2 * r; j++)
        for (int i = 0; i < 2 * r; i++) {
            int dx = i * 8 + 4 - c8, dy = j * 8 + 4 - c8;
            int d2 = dx * dx + dy * dy;
            if (d2 < (c8 - 8) * (c8 - 8)) {
                pixel_cov(g, cx - r + i, cy - r + j, c, 255);
                continue;
            }
            if (d2 > (c8 + 8) * (c8 + 8))
                continue;
            int n = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    int px = i * 8 + sx * 2 + 1 - c8, py = j * 8 + sy * 2 + 1 - c8;
                    if (px * px + py * py <= rr)
                        n++;
                }
            if (n)
                pixel_cov(g, cx - r + i, cy - r + j, c, n * 255 / 16);
        }
}

/* ---- blits --------------------------------------------------------------------- */

void gfx_blit_region(gfx_t *g, int x, int y, const image_t *img, int sx, int sy, int w, int h)
{
    int ax = x, ay = y, aw = w, ah = h;
    if (!clip_rect(g, &ax, &ay, &aw, &ah))
        return;
    int offx = ax - (x + g->ox), offy = ay - (y + g->oy);
    for (int j = 0; j < ah; j++)
        memcpy(&g->px[(ay + j) * g->stride + ax], &img->px[(sy + offy + j) * img->w + sx + offx], aw * 4);
}

void gfx_blit(gfx_t *g, int x, int y, const image_t *img) { gfx_blit_region(g, x, y, img, 0, 0, img->w, img->h); }

void gfx_blit_alpha(gfx_t *g, int x, int y, const image_t *img, int opacity)
{
    int ax = x, ay = y, aw = img->w, ah = img->h;
    if (!clip_rect(g, &ax, &ay, &aw, &ah))
        return;
    int offx = ax - (x + g->ox), offy = ay - (y + g->oy);
    for (int j = 0; j < ah; j++) {
        uint32_t *d = &g->px[(ay + j) * g->stride + ax];
        const uint32_t *s = &img->px[(offy + j) * img->w + offx];
        for (int i = 0; i < aw; i++) {
            uint32_t a = ALPHA(s[i]);
            if (opacity < 255)
                a = a * opacity / 255;
            put(g, &d[i], s[i], a);
        }
    }
}

void gfx_blit_scaled(gfx_t *g, int x, int y, int w, int h, const image_t *img)
{
    if (w <= 0 || h <= 0)
        return;
    int ax = x, ay = y, aw = w, ah = h;
    if (!clip_rect(g, &ax, &ay, &aw, &ah))
        return;
    int offx = ax - (x + g->ox), offy = ay - (y + g->oy);
    for (int j = 0; j < ah; j++) {
        int sy = (offy + j) * img->h / h;
        uint32_t *d = &g->px[(ay + j) * g->stride + ax];
        const uint32_t *srow = &img->px[sy * img->w];
        for (int i = 0; i < aw; i++) {
            uint32_t s = srow[(offx + i) * img->w / w];
            put(g, &d[i], s, ALPHA(s));
        }
    }
}

void gfx_tint(gfx_t *g, int x, int y, int w, int h, uint32_t c, int a) { gfx_fill(g, x, y, w, h, WITH_ALPHA(c, a)); }

/* fade towards grey (amount 0..256) and darken (0..256), like the XP log-off screen */
void gfx_desaturate(gfx_t *g, int x, int y, int w, int h, int amount, int darken)
{
    if (!clip_rect(g, &x, &y, &w, &h))
        return;
    for (int j = 0; j < h; j++) {
        uint32_t *row = &g->px[(y + j) * g->stride + x];
        for (int i = 0; i < w; i++) {
            uint32_t p = row[i];
            int r = (p >> 16) & 0xFF, gg = (p >> 8) & 0xFF, b = p & 0xFF;
            int l = (r * 77 + gg * 151 + b * 28) >> 8;
            r += ((l - r) * amount) >> 8;
            gg += ((l - gg) * amount) >> 8;
            b += ((l - b) * amount) >> 8;
            r = r * (256 - darken) >> 8;
            gg = gg * (256 - darken) >> 8;
            b = b * (256 - darken) >> 8;
            row[i] = RGB(r, gg, b);
        }
    }
}

/* ---- resampling ---------------------------------------------------------------- */

static int ffloor(float f)
{
    int i = (int)f;
    return (f < 0 && (float)i != f) ? i - 1 : i;
}

static float cubic(float t)
{
    const float a = -0.5f;
    if (t < 0)
        t = -t;
    if (t < 1)
        return ((a + 2) * t - (a + 3)) * t * t + 1;
    if (t < 2)
        return ((a * t - 5 * a) * t + 8 * a) * t - 4 * a;
    return 0;
}

typedef struct {
    int idx[4];
    int w[4]; /* fixed point, sum = 1024 */
} taps_t;

static taps_t *make_taps(int src, int dst)
{
    taps_t *t = kmalloc(sizeof(taps_t) * dst);
    float scale = (float)src / dst;
    for (int i = 0; i < dst; i++) {
        float s = (i + 0.5f) * scale - 0.5f;
        int base = ffloor(s);
        float f = s - base;
        float wf[4] = { cubic(1 + f), cubic(f), cubic(1 - f), cubic(2 - f) };
        int sum = 0;
        for (int k = 0; k < 4; k++) {
            t[i].idx[k] = CLAMP(base - 1 + k, 0, src - 1);
            t[i].w[k] = (int)(wf[k] * 1024 + (wf[k] >= 0 ? 0.5f : -0.5f));
            sum += t[i].w[k];
        }
        t[i].w[1] += 1024 - sum;
    }
    return t;
}

static uint32_t taps_apply(const uint32_t *p[4], const int *w)
{
    int r = 0, g = 0, b = 0;
    for (int k = 0; k < 4; k++) {
        uint32_t c = *p[k];
        r += (int)((c >> 16) & 0xFF) * w[k];
        g += (int)((c >> 8) & 0xFF) * w[k];
        b += (int)(c & 0xFF) * w[k];
    }
    r = CLAMP((r + 512) >> 10, 0, 255);
    g = CLAMP((g + 512) >> 10, 0, 255);
    b = CLAMP((b + 512) >> 10, 0, 255);
    return RGB(r, g, b);
}

static image_t *resize_box(const image_t *src, int w, int h)
{
    image_t *dst = image_new(w, h);
    for (int y = 0; y < h; y++) {
        int y0 = y * src->h / h, y1 = MAX(y0 + 1, (y + 1) * src->h / h);
        for (int x = 0; x < w; x++) {
            int x0 = x * src->w / w, x1 = MAX(x0 + 1, (x + 1) * src->w / w);
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++) {
                    uint32_t c = src->px[sy * src->w + sx];
                    a += c >> 24;
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += c & 0xFF;
                    n++;
                }
            dst->px[y * w + x] = RGBA(r / n, g / n, b / n, a / n);
        }
    }
    return dst;
}

image_t *image_resize(const image_t *src, int w, int h)
{
    if (w * 2 <= src->w && h * 2 <= src->h)
        return resize_box(src, w, h);
    taps_t *tx = make_taps(src->w, w), *ty = make_taps(src->h, h);
    image_t *tmp = image_new(w, src->h);
    for (int y = 0; y < src->h; y++) {
        const uint32_t *row = &src->px[y * src->w];
        for (int x = 0; x < w; x++) {
            const uint32_t *p[4] = { &row[tx[x].idx[0]], &row[tx[x].idx[1]], &row[tx[x].idx[2]], &row[tx[x].idx[3]] };
            tmp->px[y * w + x] = taps_apply(p, tx[x].w);
        }
    }
    image_t *dst = image_new(w, h);
    for (int y = 0; y < h; y++) {
        const uint32_t *r[4];
        for (int k = 0; k < 4; k++)
            r[k] = &tmp->px[ty[y].idx[k] * w];
        for (int x = 0; x < w; x++) {
            const uint32_t *p[4] = { r[0] + x, r[1] + x, r[2] + x, r[3] + x };
            dst->px[y * w + x] = taps_apply(p, ty[y].w);
        }
    }
    image_free(tmp);
    kfree(tx);
    kfree(ty);
    return dst;
}

/* scale to fill w x h, cropping the overflow evenly */
image_t *image_cover(const image_t *src, int w, int h)
{
    int sw, sh;
    if ((int64_t)src->w * h > (int64_t)src->h * w) {
        sh = h;
        sw = (src->w * h + src->h - 1) / src->h;
    } else {
        sw = w;
        sh = (src->h * w + src->w - 1) / src->w;
    }
    image_t *scaled = image_resize(src, sw, sh);
    if (sw == w && sh == h)
        return scaled;
    image_t *out = image_new(w, h);
    int ox = (sw - w) / 2, oy = (sh - h) / 2;
    for (int y = 0; y < h; y++)
        memcpy(&out->px[y * w], &scaled->px[(y + oy) * sw + ox], w * 4);
    image_free(scaled);
    return out;
}

/* unsharp mask with a 3x3 box blur; amount is in 1/256ths */
void image_sharpen(image_t *img, int amount)
{
    int w = img->w, h = img->h;
    uint32_t *src = kmalloc((size_t)w * h * 4);
    memcpy(src, img->px, (size_t)w * h * 4);
    for (int y = 1; y < h - 1; y++)
        for (int x = 1; x < w - 1; x++) {
            int sum[3] = { 0, 0, 0 };
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    uint32_t c = src[(y + dy) * w + x + dx];
                    sum[0] += (c >> 16) & 0xFF;
                    sum[1] += (c >> 8) & 0xFF;
                    sum[2] += c & 0xFF;
                }
            uint32_t c = src[y * w + x];
            int ch[3] = { (int)((c >> 16) & 0xFF), (int)((c >> 8) & 0xFF), (int)(c & 0xFF) };
            for (int k = 0; k < 3; k++) {
                int blur = sum[k] / 9;
                ch[k] = CLAMP(ch[k] + ((ch[k] - blur) * amount >> 8), 0, 255);
            }
            img->px[y * w + x] = RGB(ch[0], ch[1], ch[2]);
        }
    kfree(src);
}
