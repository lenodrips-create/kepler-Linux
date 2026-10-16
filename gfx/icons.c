/*
 * Icons are drawn from vector descriptions at whatever size is asked for,
 * with 4x4 supersampled coverage, so they stay crisp from 16 to 48 px.
 * Coordinates below are in a 32x32 design grid.
 */

#include "icons.h"
#include "font.h"

/* ---- tiny vector canvas -------------------------------------------------------- */

#define PI 3.14159265f

typedef struct {
    image_t *img;
    gfx_t g;
    int size;
    bool clip;
    int cx0, cy0, cx1, cy1; /* clip in 1/8 px */
} cv_t;

enum { SH_RRECT, SH_ELLIPSE, SH_POLY, SH_CAPSULE, SH_RING };

typedef struct {
    int kind;
    int x0, y0, x1, y1, r;
    int cx, cy, rx, ry, t;
    int cosa, sina; /* rotation, x1024 */
    int half;
    bool gap;
    int g0x, g0y, g1x, g1y;
    int n, px[24], py[24];
    int ax, ay, bx, by, hw;
    int bx0, by0, bx1, by1;
} shape_t;

enum { P_SOLID, P_LINEAR, P_RADIAL };

typedef struct {
    int kind;
    uint32_t c0, c1;
    float x0, y0, x1, y1, r;
    bool erase;
} paint_t;

static paint_t solid(uint32_t c) { return (paint_t){ P_SOLID, c, c, 0, 0, 0, 0, 0, false }; }

static paint_t lin(float x0, float y0, float x1, float y1, uint32_t c0, uint32_t c1)
{
    return (paint_t){ P_LINEAR, c0, c1, x0, y0, x1, y1, 0, false };
}

/* vertical gradient between two y values */
static paint_t vlin(float y0, float y1, uint32_t c0, uint32_t c1) { return lin(0, y0, 0, y1, c0, c1); }

static paint_t rad(float cx, float cy, float r, uint32_t c0, uint32_t c1)
{
    return (paint_t){ P_RADIAL, c0, c1, cx, cy, 0, 0, r, false };
}

static paint_t erase(void)
{
    paint_t p = solid(0);
    p.erase = true;
    return p;
}

static int U(cv_t *cv, float v)
{
    float f = v * cv->size * 8.0f / 32.0f;
    return (int)(f >= 0 ? f + 0.5f : f - 0.5f);
}

static float fsin(float x)
{
    while (x > PI)
        x -= 2 * PI;
    while (x < -PI)
        x += 2 * PI;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}

static float fcos(float x) { return fsin(x + PI / 2); }

static uint32_t isqrt(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

static bool in_ellipse(int dx, int dy, int rx, int ry)
{
    if (rx <= 0 || ry <= 0)
        return false;
    int64_t a = (int64_t)dx * dx * ry * ry + (int64_t)dy * dy * rx * rx;
    return a <= (int64_t)rx * rx * ry * ry;
}

static bool inside(const shape_t *s, int x, int y)
{
    switch (s->kind) {
    case SH_RRECT: {
        if (x < s->x0 || x >= s->x1 || y < s->y0 || y >= s->y1)
            return false;
        int r = s->r;
        if (r <= 0)
            return true;
        int cx = x < s->x0 + r ? s->x0 + r : x >= s->x1 - r ? s->x1 - r : x;
        int cy = y < s->y0 + r ? s->y0 + r : y >= s->y1 - r ? s->y1 - r : y;
        int dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= r * r;
    }
    case SH_ELLIPSE:
    case SH_RING: {
        int dx = x - s->cx, dy = y - s->cy;
        if (s->cosa != 1024 || s->sina) {
            int rx = (dx * s->cosa + dy * s->sina) / 1024;
            int ry = (-dx * s->sina + dy * s->cosa) / 1024;
            dx = rx;
            dy = ry;
        }
        if (!in_ellipse(dx, dy, s->rx, s->ry))
            return false;
        if (s->kind == SH_ELLIPSE)
            return true;
        if (in_ellipse(dx, dy, s->rx - s->t, s->ry - s->t))
            return false;
        if (s->half == 1 && dy < 0)
            return false;
        if (s->half < 0 && dy >= 0)
            return false;
        if (s->gap) {
            int px = x - s->cx, py = y - s->cy;
            int64_t c0 = (int64_t)s->g0x * py - (int64_t)s->g0y * px;
            int64_t c1 = (int64_t)px * s->g1y - (int64_t)py * s->g1x;
            bool in_wedge = c0 >= 0 && c1 >= 0;
            /* half == 2 marks an inverted (complement) wedge */
            if (s->half == 2 ? !in_wedge : in_wedge)
                return false;
        }
        return true;
    }
    case SH_POLY: {
        bool in = false;
        for (int i = 0, j = s->n - 1; i < s->n; j = i++) {
            int xi = s->px[i], yi = s->py[i], xj = s->px[j], yj = s->py[j];
            if ((yi > y) != (yj > y)) {
                int64_t num = (int64_t)(xj - xi) * (y - yi);
                int64_t den = yj - yi;
                int64_t lhs = (int64_t)(x - xi) * den;
                if (den > 0 ? lhs < num : lhs > num)
                    in = !in;
            }
        }
        return in;
    }
    case SH_CAPSULE: {
        int64_t dx = s->bx - s->ax, dy = s->by - s->ay;
        int64_t px = x - s->ax, py = y - s->ay;
        int64_t dot = px * dx + py * dy, len2 = dx * dx + dy * dy;
        int64_t hw2 = (int64_t)s->hw * s->hw;
        if (dot <= 0 || len2 == 0)
            return px * px + py * py <= hw2;
        if (dot >= len2) {
            int64_t qx = px - dx, qy = py - dy;
            return qx * qx + qy * qy <= hw2;
        }
        int64_t cross = dx * py - dy * px;
        return cross * cross <= hw2 * len2;
    }
    }
    return false;
}

static uint32_t paint_at(cv_t *cv, const paint_t *p, int x, int y)
{
    if (p->kind == P_SOLID)
        return p->c0;
    int t;
    if (p->kind == P_LINEAR) {
        int64_t x0 = U(cv, p->x0), y0 = U(cv, p->y0), x1 = U(cv, p->x1), y1 = U(cv, p->y1);
        int64_t dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
        if (l2 == 0)
            return p->c0;
        int64_t d = (x - x0) * dx + (y - y0) * dy;
        if (d <= 0)
            return p->c0;
        if (d >= l2)
            return p->c1;
        t = (int)(d * 256 / l2);
    } else {
        int64_t dx = x - U(cv, p->x0), dy = y - U(cv, p->y0);
        int r = U(cv, p->r);
        t = r > 0 ? (int)(isqrt(dx * dx + dy * dy) * 256 / r) : 256;
    }
    return color_lerp(p->c0, p->c1, CLAMP(t, 0, 256));
}

/* 4x4 supersampled coverage of device pixel (px, py), 0..255 */
static int coverage(cv_t *cv, const shape_t *s, int px, int py)
{
    int n = 0;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) {
            int sx = px * 8 + 1 + 2 * i, sy = py * 8 + 1 + 2 * j;
            if (cv->clip && (sx < cv->cx0 || sx >= cv->cx1 || sy < cv->cy0 || sy >= cv->cy1))
                continue;
            if (inside(s, sx, sy))
                n++;
        }
    return n * 255 / 16;
}

static void fill(cv_t *cv, shape_t *s, paint_t p)
{
    int px0 = MAX(0, s->bx0 >> 3), py0 = MAX(0, s->by0 >> 3);
    int px1 = MIN(cv->size - 1, (s->bx1 >> 3) + 1), py1 = MIN(cv->size - 1, (s->by1 >> 3) + 1);
    for (int py = py0; py <= py1; py++)
        for (int px = px0; px <= px1; px++) {
            int cov = coverage(cv, s, px, py);
            if (!cov)
                continue;
            uint32_t *d = &cv->img->px[py * cv->size + px];
            if (p.erase) {
                uint32_t a = (*d >> 24) * (255 - cov) / 255;
                *d = (*d & 0xFFFFFF) | (a << 24);
                continue;
            }
            uint32_t c = paint_at(cv, &p, px * 8 + 4, py * 8 + 4);
            uint32_t a = ALPHA(c) * cov / 255;
            gfx_pixel(&cv->g, px, py, WITH_ALPHA(c, a));
        }
}

static void rrect(cv_t *cv, float x, float y, float w, float h, float r, paint_t p)
{
    shape_t s = { 0 };
    s.kind = SH_RRECT;
    s.x0 = U(cv, x);
    s.y0 = U(cv, y);
    s.x1 = U(cv, x + w);
    s.y1 = U(cv, y + h);
    s.r = MAX(0, U(cv, r));
    s.bx0 = s.x0;
    s.by0 = s.y0;
    s.bx1 = s.x1;
    s.by1 = s.y1;
    fill(cv, &s, p);
}

static void ellipse_rot(cv_t *cv, float cx, float cy, float rx, float ry, float deg, paint_t p)
{
    shape_t s = { 0 };
    s.kind = SH_ELLIPSE;
    s.cx = U(cv, cx);
    s.cy = U(cv, cy);
    s.rx = U(cv, rx);
    s.ry = U(cv, ry);
    s.cosa = (int)(fcos(deg * PI / 180) * 1024);
    s.sina = (int)(fsin(deg * PI / 180) * 1024);
    int m = MAX(s.rx, s.ry);
    s.bx0 = s.cx - m;
    s.by0 = s.cy - m;
    s.bx1 = s.cx + m;
    s.by1 = s.cy + m;
    fill(cv, &s, p);
}

static void ellipse(cv_t *cv, float cx, float cy, float rx, float ry, paint_t p)
{
    ellipse_rot(cv, cx, cy, rx, ry, 0, p);
}

static void circle(cv_t *cv, float cx, float cy, float r, paint_t p) { ellipse(cv, cx, cy, r, r, p); }

/* elliptical ring of thickness t; half > 0 keeps the lower half (in its own frame), < 0 the upper.
 * The wedge from angle g0 to g1 (degrees, clockwise, y down) is left out when g0 != g1. */
static void ring_ex(cv_t *cv, float cx, float cy, float rx, float ry, float t, float deg, int half, float g0, float g1,
                    paint_t p)
{
    shape_t s = { 0 };
    s.kind = SH_RING;
    s.cx = U(cv, cx);
    s.cy = U(cv, cy);
    s.rx = U(cv, rx);
    s.ry = U(cv, ry);
    s.t = U(cv, t);
    s.cosa = (int)(fcos(deg * PI / 180) * 1024);
    s.sina = (int)(fsin(deg * PI / 180) * 1024);
    s.half = half;
    if (g0 != g1) {
        s.gap = true;
        if (g1 - g0 > 180) {
            /* the wedge test only handles spans under 180 degrees: test the complement instead */
            float t = g0 + 360;
            g0 = g1;
            g1 = t;
            s.half = s.half ? s.half : 2;
        }
        s.g0x = (int)(fcos(g0 * PI / 180) * 1024);
        s.g0y = (int)(fsin(g0 * PI / 180) * 1024);
        s.g1x = (int)(fcos(g1 * PI / 180) * 1024);
        s.g1y = (int)(fsin(g1 * PI / 180) * 1024);
    }
    int m = MAX(s.rx, s.ry);
    s.bx0 = s.cx - m;
    s.by0 = s.cy - m;
    s.bx1 = s.cx + m;
    s.by1 = s.cy + m;
    fill(cv, &s, p);
}

static void ring(cv_t *cv, float cx, float cy, float r, float t, float g0, float g1, paint_t p)
{
    ring_ex(cv, cx, cy, r, r, t, 0, 0, g0, g1, p);
}

static void poly(cv_t *cv, const float *xy, int n, paint_t p)
{
    shape_t s = { 0 };
    s.kind = SH_POLY;
    s.n = n;
    s.bx0 = s.by0 = 1 << 20;
    s.bx1 = s.by1 = -(1 << 20);
    for (int i = 0; i < n; i++) {
        s.px[i] = U(cv, xy[2 * i]);
        s.py[i] = U(cv, xy[2 * i + 1]);
        s.bx0 = MIN(s.bx0, s.px[i]);
        s.by0 = MIN(s.by0, s.py[i]);
        s.bx1 = MAX(s.bx1, s.px[i]);
        s.by1 = MAX(s.by1, s.py[i]);
    }
    fill(cv, &s, p);
}

static void line(cv_t *cv, float x0, float y0, float x1, float y1, float w, paint_t p)
{
    shape_t s = { 0 };
    s.kind = SH_CAPSULE;
    s.ax = U(cv, x0);
    s.ay = U(cv, y0);
    s.bx = U(cv, x1);
    s.by = U(cv, y1);
    s.hw = MAX(1, U(cv, w / 2));
    s.bx0 = MIN(s.ax, s.bx) - s.hw;
    s.by0 = MIN(s.ay, s.by) - s.hw;
    s.bx1 = MAX(s.ax, s.bx) + s.hw;
    s.by1 = MAX(s.ay, s.by) + s.hw;
    fill(cv, &s, p);
}

static void clip(cv_t *cv, float x, float y, float w, float h)
{
    cv->clip = true;
    cv->cx0 = U(cv, x);
    cv->cy0 = U(cv, y);
    cv->cx1 = U(cv, x + w);
    cv->cy1 = U(cv, y + h);
}

static void unclip(cv_t *cv) { cv->clip = false; }

/* one device pixel in design units */
#define PX (32.0f / cv->size)

/* rounded rect with a 1px outline */
static void orrect(cv_t *cv, float x, float y, float w, float h, float r, uint32_t outline, paint_t p)
{
    rrect(cv, x, y, w, h, r, solid(outline));
    rrect(cv, x + PX, y + PX, w - 2 * PX, h - 2 * PX, MAX(0, r - PX), p);
}

static void glyph_text(cv_t *cv, const char *s, uint32_t c)
{
    const font_t *f = cv->size >= 40 ? &font_big_bold : cv->size >= 28 ? &font_big_bold
                    : cv->size >= 20 ? &font_heading : &font_ui_bold;
    int w = text_width(f, s);
    gfx_text(&cv->g, f, (cv->size - w) / 2, (cv->size - f->height) / 2 + (cv->size >= 28 ? 0 : 1), s, c);
}

/* ---- icon artwork ------------------------------------------------------------------ */

#define WHITE solid(HEX(0xFFFFFF))

typedef void (*icon_fn)(cv_t *cv);

static void soft_shadow(cv_t *cv, float cx, float cy, float rx)
{
    ellipse(cv, cx, cy, rx, 1.6f, rad(cx, cy, rx, RGBA(0, 0, 0, 60), RGBA(0, 0, 0, 0)));
}

static shape_t rrect_shape(cv_t *cv, float x, float y, float w, float h, float r)
{
    shape_t s = { 0 };
    s.kind = SH_RRECT;
    s.x0 = U(cv, x);
    s.y0 = U(cv, y);
    s.x1 = U(cv, x + w);
    s.y1 = U(cv, y + h);
    s.r = MAX(0, U(cv, r));
    s.bx0 = s.x0;
    s.by0 = s.y0;
    s.bx1 = s.x1;
    s.by1 = s.y1;
    return s;
}

/* a transparent layer the size of the canvas; drawn into, then masked and merged */
static cv_t layer_new(cv_t *cv)
{
    cv_t l = *cv;
    l.img = image_new(cv->size, cv->size);
    l.g = gfx_for_image(l.img);
    l.clip = false;
    return l;
}

static void layer_merge(cv_t *cv, cv_t *l, const shape_t *mask)
{
    for (int py = 0; py < cv->size; py++)
        for (int px = 0; px < cv->size; px++) {
            uint32_t *d = &l->img->px[py * cv->size + px];
            if (!(*d >> 24))
                continue;
            uint32_t a = (*d >> 24) * coverage(l, mask, px, py) / 255;
            *d = (*d & 0xFFFFFF) | (a << 24);
        }
    gfx_blit_alpha(&cv->g, 0, 0, l->img, 255);
    image_free(l->img);
}

/* the Big Sur app icon shape: a rounded square over a soft shadow */
#define SQ_X 3.0f
#define SQ_Y 2.4f
#define SQ_S 26.0f
#define SQ_R 5.9f

static void squircle_merge(cv_t *cv, cv_t *l)
{
    rrect(cv, SQ_X - 0.5f, SQ_Y + 0.4f, SQ_S + 1.0f, SQ_S + 1.1f, SQ_R + 0.5f, solid(RGBA(0, 0, 0, 20)));
    rrect(cv, SQ_X, SQ_Y + 0.7f, SQ_S, SQ_S, SQ_R, solid(RGBA(0, 0, 0, 46)));
    shape_t m = rrect_shape(cv, SQ_X, SQ_Y, SQ_S, SQ_S, SQ_R);
    layer_merge(cv, l, &m);
}

static void backdrop(cv_t *l, uint32_t top, uint32_t bottom)
{
    rrect(l, 0, 0, 32, 32, 0, vlin(SQ_Y, SQ_Y + SQ_S, top, bottom));
}

/* ---- glyphs (white on transparent) ---- */

static void g_kepler(cv_t *cv)
{
    ring_ex(cv, 16, 16.8f, 14.6f, 4.6f, 2.0f, -20, -1, 0, 0, WHITE);
    circle(cv, 16, 16, 8.6f, WHITE);
    ring_ex(cv, 16, 16.8f, 15.6f, 5.6f, 4.0f, -20, 1, 0, 0, erase());
    ring_ex(cv, 16, 16.8f, 14.6f, 4.6f, 2.0f, -20, 1, 0, 0, WHITE);
    circle(cv, 27.2f, 7.0f, 2.1f, WHITE);
}

static void g_wifi(cv_t *cv)
{
    circle(cv, 16, 25, 2.7f, WHITE);
    ring(cv, 16, 25, 10.4f, 3.0f, -45, 225, WHITE);
    ring(cv, 16, 25, 17.4f, 3.0f, -45, 225, WHITE);
}

static void g_speaker(cv_t *cv)
{
    const float body[] = { 3.5f, 12, 9, 12, 16, 5.5f, 16, 26.5f, 9, 20, 3.5f, 20 };
    poly(cv, body, 6, WHITE);
    ring(cv, 16.5f, 16, 6.6f, 2.5f, 50, 310, WHITE);
    ring(cv, 16.5f, 16, 12.2f, 2.5f, 52, 308, WHITE);
}

static void g_search(cv_t *cv)
{
    ring(cv, 13.4f, 13.4f, 9.6f, 2.9f, 0, 0, WHITE);
    line(cv, 20.4f, 20.4f, 27, 27, 3.4f, WHITE);
}

static void g_control(cv_t *cv)
{
    rrect(cv, 3, 4.6f, 26, 10, 5, WHITE);
    rrect(cv, 5.5f, 7.1f, 21, 5, 2.5f, erase());
    circle(cv, 9.6f, 9.6f, 3.6f, WHITE);
    rrect(cv, 3, 17.4f, 26, 10, 5, WHITE);
    rrect(cv, 5.5f, 19.9f, 21, 5, 2.5f, erase());
    circle(cv, 22.4f, 22.4f, 3.6f, WHITE);
}

static void g_airdrop(cv_t *cv)
{
    circle(cv, 16, 17.5f, 3.2f, WHITE);
    ring(cv, 16, 17.5f, 8.8f, 2.6f, 35, 145, WHITE);
    ring(cv, 16, 17.5f, 14.6f, 2.6f, 40, 140, WHITE);
}

static void g_recents(cv_t *cv)
{
    ring(cv, 16, 16, 13.4f, 2.6f, 0, 0, WHITE);
    line(cv, 16, 16.4f, 16, 8.2f, 2.6f, WHITE);
    line(cv, 16, 16.4f, 21.2f, 19.6f, 2.6f, WHITE);
}

static void g_desktop(cv_t *cv)
{
    rrect(cv, 2.5f, 4.5f, 27, 18.6f, 2.8f, WHITE);
    rrect(cv, 5.1f, 7.1f, 21.8f, 13.4f, 1, erase());
    line(cv, 16, 23, 16, 26.6f, 2.6f, WHITE);
    line(cv, 10, 27.4f, 22, 27.4f, 2.6f, WHITE);
}

static void g_apps(cv_t *cv)
{
    line(cv, 8, 26, 16, 6.5f, 2.8f, WHITE);
    line(cv, 16, 6.5f, 24, 26, 2.8f, WHITE);
    line(cv, 5.5f, 19.4f, 26.5f, 19.4f, 2.8f, WHITE);
}

static void g_document(cv_t *cv)
{
    const float page[] = { 6, 2.5f, 19, 2.5f, 26, 9.5f, 26, 29.5f, 6, 29.5f };
    poly(cv, page, 5, WHITE);
    const float in[] = { 8.5f, 5, 17.9f, 5, 23.5f, 10.6f, 23.5f, 27, 8.5f, 27 };
    poly(cv, in, 5, erase());
    line(cv, 18.3f, 3.6f, 18.3f, 10.3f, 2.2f, WHITE);
    line(cv, 18.3f, 10.3f, 25, 10.3f, 2.2f, WHITE);
}

static void g_photo(cv_t *cv)
{
    rrect(cv, 2.5f, 5, 27, 22, 3, WHITE);
    rrect(cv, 5, 7.5f, 22, 17, 1.2f, erase());
    const float hill[] = { 5, 24.5f, 13, 14.5f, 18.5f, 20.5f, 21.5f, 17.5f, 27, 24.5f };
    poly(cv, hill, 5, WHITE);
    circle(cv, 20.5f, 11.8f, 2.4f, WHITE);
}

static void g_downloads(cv_t *cv)
{
    ring(cv, 16, 16, 13.6f, 2.6f, 0, 0, WHITE);
    line(cv, 16, 8.4f, 16, 22.4f, 2.6f, WHITE);
    line(cv, 10.6f, 17.4f, 16, 22.8f, 2.6f, WHITE);
    line(cv, 21.4f, 17.4f, 16, 22.8f, 2.6f, WHITE);
}

static void cloud(cv_t *cv, float k, paint_t p)
{
    circle(cv, 10.4f, 18.4f, 6.4f - k, p);
    circle(cv, 17.6f, 14.2f, 8.2f - k, p);
    circle(cv, 24.2f, 19.2f, 5.6f - k, p);
    rrect(cv, 4.0f + k, 17.8f + k, 25.8f - 2 * k, 7.2f - 2 * k, 3.6f - k, p);
}

static void g_cloud(cv_t *cv)
{
    cloud(cv, 0, WHITE);
    cloud(cv, 2.5f, erase());
    rrect(cv, 8, 14, 17, 8.5f, 3, erase());
}

static void g_disk(cv_t *cv)
{
    rrect(cv, 2.5f, 8.5f, 27, 15, 3.6f, WHITE);
    rrect(cv, 5.1f, 11.1f, 21.8f, 9.8f, 1.4f, erase());
    circle(cv, 23.2f, 16, 1.7f, WHITE);
}

static void g_trash(cv_t *cv)
{
    rrect(cv, 11.6f, 2.8f, 8.8f, 6.4f, 2.2f, WHITE);
    rrect(cv, 14.0f, 5.2f, 4.0f, 4.0f, 0.6f, erase());
    line(cv, 4.5f, 8.2f, 27.5f, 8.2f, 2.6f, WHITE);
    const float body[] = { 6.8f, 10.4f, 25.2f, 10.4f, 23.6f, 29.4f, 8.4f, 29.4f };
    poly(cv, body, 4, WHITE);
    const float in[] = { 9.3f, 10.4f, 22.7f, 10.4f, 21.3f, 26.9f, 10.7f, 26.9f };
    poly(cv, in, 4, erase());
    line(cv, 13.6f, 14, 13.9f, 23.4f, 1.9f, WHITE);
    line(cv, 18.4f, 14, 18.1f, 23.4f, 1.9f, WHITE);
}

static void g_chevron_l(cv_t *cv)
{
    line(cv, 20.5f, 6, 10.5f, 16, 3.2f, WHITE);
    line(cv, 10.5f, 16, 20.5f, 26, 3.2f, WHITE);
}

static void g_chevron_r(cv_t *cv)
{
    line(cv, 11.5f, 6, 21.5f, 16, 3.2f, WHITE);
    line(cv, 21.5f, 16, 11.5f, 26, 3.2f, WHITE);
}

static void g_chevron_d(cv_t *cv)
{
    line(cv, 6.5f, 11.5f, 16, 21, 3.4f, WHITE);
    line(cv, 16, 21, 25.5f, 11.5f, 3.4f, WHITE);
}

static void g_grid(cv_t *cv)
{
    for (int i = 0; i < 4; i++)
        rrect(cv, 3.5f + (i % 2) * 13.5f, 3.5f + (i / 2) * 13.5f, 11.5f, 11.5f, 2.8f, WHITE);
}

static void g_list(cv_t *cv)
{
    for (int i = 0; i < 3; i++) {
        circle(cv, 5.5f, 8 + i * 8, 2.2f, WHITE);
        line(cv, 11.5f, 8 + i * 8, 28, 8 + i * 8, 2.6f, WHITE);
    }
}

static void g_columns(cv_t *cv)
{
    rrect(cv, 2.5f, 4.5f, 27, 23, 3.4f, WHITE);
    rrect(cv, 5, 7, 22, 18, 1.2f, erase());
    line(cv, 12.6f, 6, 12.6f, 26, 2.4f, WHITE);
    line(cv, 19.4f, 6, 19.4f, 26, 2.4f, WHITE);
}

static void g_gallery(cv_t *cv)
{
    rrect(cv, 2.5f, 3.5f, 27, 16, 3.4f, WHITE);
    rrect(cv, 5, 6, 22, 11, 1.2f, erase());
    for (int i = 0; i < 4; i++)
        rrect(cv, 2.5f + i * 7.05f, 22, 5.8f, 6, 1.6f, WHITE);
}

static void g_group(cv_t *cv)
{
    for (int i = 0; i < 6; i++)
        rrect(cv, 2.5f + (i % 3) * 9.4f, 6 + (i / 3) * 10.6f, 8, 9, 2, WHITE);
}

static void g_share(cv_t *cv)
{
    rrect(cv, 6, 11, 20, 18.6f, 3.4f, WHITE);
    rrect(cv, 8.5f, 13.5f, 15, 13.6f, 1.4f, erase());
    rrect(cv, 12.2f, 9, 7.6f, 6.6f, 0, erase());
    line(cv, 16, 3.4f, 16, 19.8f, 2.6f, WHITE);
    line(cv, 11.2f, 8.2f, 16, 3.4f, 2.6f, WHITE);
    line(cv, 20.8f, 8.2f, 16, 3.4f, 2.6f, WHITE);
}

static void g_tag(cv_t *cv)
{
    const float out[] = { 15.4f, 3, 29, 3, 29, 16.6f, 16.2f, 29.4f, 2.6f, 15.8f };
    poly(cv, out, 5, WHITE);
    const float in[] = { 16.4f, 5.5f, 26.5f, 5.5f, 26.5f, 15.6f, 16.2f, 25.9f, 6.1f, 15.8f };
    poly(cv, in, 5, erase());
    circle(cv, 21.8f, 10.2f, 2.0f, WHITE);
}

static void g_more(cv_t *cv)
{
    line(cv, 7.5f, 9, 14.5f, 16, 2.8f, WHITE);
    line(cv, 14.5f, 16, 7.5f, 23, 2.8f, WHITE);
    line(cv, 17, 9, 24, 16, 2.8f, WHITE);
    line(cv, 24, 16, 17, 23, 2.8f, WHITE);
}

static void g_arrow_r(cv_t *cv)
{
    ring(cv, 16, 16, 14.2f, 2.2f, 0, 0, WHITE);
    line(cv, 9.4f, 16, 22, 16, 2.6f, WHITE);
    line(cv, 16.6f, 10.4f, 22.2f, 16, 2.6f, WHITE);
    line(cv, 16.6f, 21.6f, 22.2f, 16, 2.6f, WHITE);
}

static void g_sleep(cv_t *cv)
{
    circle(cv, 15, 17, 11.4f, WHITE);
    circle(cv, 21.8f, 11.0f, 9.8f, erase());
}

static void g_restart(cv_t *cv)
{
    ring(cv, 16, 17, 11.4f, 2.9f, -100, -20, WHITE);
    const float head[] = { 13.4f, 0.8f, 21.2f, 5.9f, 13.4f, 11.0f };
    poly(cv, head, 3, WHITE);
}

static void g_power(cv_t *cv)
{
    ring(cv, 16, 17.4f, 11.6f, 2.9f, -128, -52, WHITE);
    line(cv, 16, 3.4f, 16, 15.2f, 2.9f, WHITE);
}

static void g_person(cv_t *cv)
{
    circle(cv, 16, 10.4f, 6.2f, WHITE);
    ellipse(cv, 16, 29, 11.8f, 9.2f, WHITE);
    rrect(cv, 0, 29, 32, 3, 0, erase());
}

static void g_tl_close(cv_t *cv)
{
    line(cv, 10, 10, 22, 22, 3.6f, WHITE);
    line(cv, 22, 10, 10, 22, 3.6f, WHITE);
}

static void g_tl_min(cv_t *cv) { line(cv, 8.5f, 16, 23.5f, 16, 3.8f, WHITE); }

static void g_tl_zoom(cv_t *cv)
{
    const float a[] = { 8.4f, 8.4f, 20.4f, 8.4f, 8.4f, 20.4f };
    const float b[] = { 23.6f, 23.6f, 11.6f, 23.6f, 23.6f, 11.6f };
    poly(cv, a, 3, WHITE);
    poly(cv, b, 3, WHITE);
}

static void g_check(cv_t *cv)
{
    line(cv, 7, 16.8f, 13, 22.8f, 3.8f, WHITE);
    line(cv, 13, 22.8f, 25, 9.2f, 3.8f, WHITE);
}

static void g_cmd(cv_t *cv)
{
    static const float cx[4] = { 8.5f, 23.5f, 8.5f, 23.5f }, cy[4] = { 8.5f, 8.5f, 23.5f, 23.5f };
    for (int i = 0; i < 4; i++)
        ring(cv, cx[i], cy[i], 5.2f, 2.4f, 0, 0, WHITE);
    line(cv, 11, 8.5f, 11, 23.5f, 2.4f, WHITE);
    line(cv, 21, 8.5f, 21, 23.5f, 2.4f, WHITE);
    line(cv, 8.5f, 11, 23.5f, 11, 2.4f, WHITE);
    line(cv, 8.5f, 21, 23.5f, 21, 2.4f, WHITE);
}

static void g_lock(cv_t *cv)
{
    ring_ex(cv, 16, 13, 7.2f, 7.4f, 2.6f, 0, -1, 0, 0, WHITE);
    line(cv, 8.4f, 13, 8.4f, 16, 2.6f, WHITE);
    line(cv, 23.6f, 13, 23.6f, 16, 2.6f, WHITE);
    rrect(cv, 5, 15, 22, 15, 3, WHITE);
}

static void g_reload(cv_t *cv)
{
    ring(cv, 16, 17, 10.8f, 2.8f, -80, 10, WHITE);
    const float head[] = { 21.6f, 1.6f, 27.6f, 8.8f, 18.8f, 10.8f };
    poly(cv, head, 3, WHITE);
}

static void g_home(cv_t *cv)
{
    const float roof[] = { 16, 3.5f, 30, 16, 26.5f, 16, 26.5f, 28.5f, 5.5f, 28.5f, 5.5f, 16, 2, 16 };
    poly(cv, roof, 7, WHITE);
    rrect(cv, 13, 19.5f, 6, 9, 1, erase());
}

static void g_close(cv_t *cv)
{
    line(cv, 8, 8, 24, 24, 3.2f, WHITE);
    line(cv, 24, 8, 8, 24, 3.2f, WHITE);
}

static void g_shield(cv_t *cv)
{
    const float out[] = { 16, 2.5f, 28, 7, 28, 15.5f, 16, 29.5f, 4, 15.5f, 4, 7 };
    poly(cv, out, 6, WHITE);
    const float in[] = { 16, 5.4f, 25.4f, 8.9f, 25.4f, 15.2f, 16, 26.2f, 6.6f, 15.2f, 6.6f, 8.9f };
    poly(cv, in, 6, erase());
    line(cv, 11, 16, 14.8f, 19.8f, 2.6f, WHITE);
    line(cv, 14.8f, 19.8f, 21.4f, 12.2f, 2.6f, WHITE);
}

static void g_globe(cv_t *cv)
{
    ring(cv, 16, 16, 13, 2.4f, 0, 0, WHITE);
    ring_ex(cv, 16, 16, 6.4f, 13, 2.2f, 0, 0, 0, 0, WHITE);
    line(cv, 3.5f, 16, 28.5f, 16, 2.2f, WHITE);
    line(cv, 16, 3.5f, 16, 28.5f, 2.2f, WHITE);
}

/* a glyph pressed into the front of a folder */
static void emboss(cv_t *cv, icon_fn glyph)
{
    int gs = cv->size * 11 / 32;
    if (gs < 7)
        return;
    cv_t g = { 0 };
    g.img = image_new(gs, gs);
    g.g = gfx_for_image(g.img);
    g.size = gs;
    glyph(&g);
    int x = (cv->size - gs) / 2, y = cv->size * 19 / 32 - gs / 2;
    gfx_blit_tint(&cv->g, x, y, g.img, RGBA(32, 110, 200, 140));
    image_free(g.img);
}

/* ---- documents, folders and devices ---- */

static void folder(cv_t *cv)
{
    rrect(cv, 2.6f, 6.0f, 26.8f, 22.6f, 2.4f, solid(RGBA(0, 0, 0, 26)));
    rrect(cv, 3.2f, 5.0f, 11.4f, 6.0f, 2.0f, solid(HEX(0x4D9FEB)));
    rrect(cv, 3.2f, 7.2f, 25.6f, 20.6f, 2.0f, vlin(7, 28, HEX(0x58A9EF), HEX(0x3A8BDD)));
    rrect(cv, 3.2f, 10.2f, 25.6f, 17.6f, 2.0f, vlin(10, 28, HEX(0x9AD2FD), HEX(0x6CB7F5)));
    line(cv, 4.6f, 10.8f, 27.4f, 10.8f, 0.6f, solid(RGBA(255, 255, 255, 150)));
}

static void draw_folder(cv_t *cv) { folder(cv); }

static void draw_documents(cv_t *cv)
{
    folder(cv);
    emboss(cv, g_document);
}

static void draw_folder_desktop(cv_t *cv)
{
    folder(cv);
    emboss(cv, g_desktop);
}

static void draw_folder_pictures(cv_t *cv)
{
    folder(cv);
    emboss(cv, g_photo);
}

static void draw_folder_downloads(cv_t *cv)
{
    folder(cv);
    emboss(cv, g_downloads);
}

static void draw_folder_apps(cv_t *cv)
{
    folder(cv);
    emboss(cv, g_apps);
}

static void doc_page(cv_t *cv)
{
    soft_shadow(cv, 16, 29.4f, 9);
    const float page[] = { 6.5f, 2.5f, 19.8f, 2.5f, 25.5f, 8.2f, 25.5f, 29, 6.5f, 29 };
    poly(cv, page, 5, solid(HEX(0xBDBFC6)));
    const float inner[] = { 6.5f + PX, 2.5f + PX, 19.8f - PX * 0.4f, 2.5f + PX, 25.5f - PX, 8.2f + PX * 0.4f,
                            25.5f - PX, 29 - PX, 6.5f + PX, 29 - PX };
    poly(cv, inner, 5, solid(HEX(0xFFFFFF)));
    const float fold[] = { 19.8f, 2.5f, 19.8f, 8.2f, 25.5f, 8.2f };
    poly(cv, fold, 3, vlin(2, 9, HEX(0xF4F4F6), HEX(0xD2D3D9)));
}

static void draw_textfile(cv_t *cv)
{
    doc_page(cv);
    for (int i = 0; i < 7; i++)
        line(cv, 9.6f, 11.5f + i * 2.4f, i == 6 ? 17 : 22.4f, 11.5f + i * 2.4f, 0.6f, solid(HEX(0xB2B5BC)));
}

static void landscape(cv_t *cv, float x, float y, float w, float h)
{
    rrect(cv, x, y, w, h, 0, vlin(y, y + h, HEX(0x5DB2FF), HEX(0xD4ECFF)));
    clip(cv, x, y, w, h);
    circle(cv, x + w * 0.76f, y + h * 0.28f, h * 0.16f, solid(HEX(0xFFD23A)));
    ellipse(cv, x + w * 0.3f, y + h * 1.05f, w * 0.62f, h * 0.55f, vlin(y + h * 0.5f, y + h, HEX(0x7CD65E), HEX(0x2E9B2E)));
    ellipse(cv, x + w * 0.95f, y + h * 1.1f, w * 0.5f, h * 0.45f, vlin(y + h * 0.6f, y + h, HEX(0x5CC145), HEX(0x1F7F28)));
    unclip(cv);
}

static void draw_imagefile(cv_t *cv)
{
    soft_shadow(cv, 16, 27.4f, 12);
    orrect(cv, 2.5f, 6, 27, 20.5f, 0.8f, HEX(0xC0C3CA), solid(HEX(0xFFFFFF)));
    landscape(cv, 4.2f, 7.7f, 23.6f, 17.1f);
}

static void draw_drive(cv_t *cv)
{
    soft_shadow(cv, 16, 25.6f, 13);
    const float top[] = { 5.4f, 8.8f, 26.6f, 8.8f, 29.2f, 13.6f, 2.8f, 13.6f };
    poly(cv, top, 4, solid(HEX(0xA1A1A7)));
    const float topin[] = { 5.4f + PX, 8.8f + PX, 26.6f - PX, 8.8f + PX, 29.2f - PX, 13.6f, 2.8f + PX, 13.6f };
    poly(cv, topin, 4, vlin(9, 13.6f, HEX(0xFAFAFB), HEX(0xE2E2E6)));
    orrect(cv, 2.8f, 13.2f, 26.4f, 11.4f, 1.4f, HEX(0x8E8E93), vlin(13, 24.6f, HEX(0xE8E8EC), HEX(0xB0B0B6)));
    line(cv, 5.6f, 19.6f, 17.6f, 19.6f, 0.7f, solid(RGBA(0, 0, 0, 45)));
    line(cv, 5.6f, 20.4f, 17.6f, 20.4f, 0.6f, solid(RGBA(255, 255, 255, 140)));
    circle(cv, 25.2f, 18.8f, 0.9f, solid(HEX(0x34C759)));
}

static void draw_cdrom(cv_t *cv)
{
    soft_shadow(cv, 16, 28.8f, 10);
    circle(cv, 16, 15.5f, 12.6f, solid(HEX(0x9AA3AF)));
    circle(cv, 16, 15.5f, 12.6f - PX, lin(6, 4, 26, 27, HEX(0xF4F7FB), HEX(0xB6C1D0)));
    ring(cv, 16, 15.5f, 10.4f, 1.6f, 0, 0, solid(RGBA(160, 120, 255, 70)));
    ring(cv, 16, 15.5f, 7.6f, 1.4f, 0, 0, solid(RGBA(90, 200, 255, 80)));
    circle(cv, 16, 15.5f, 3.6f, solid(HEX(0xD6DCE4)));
    circle(cv, 16, 15.5f, 1.9f, erase());
}

static void draw_computer(cv_t *cv)
{
    const float stand[] = { 12.6f, 22.6f, 19.4f, 22.6f, 20.8f, 28.2f, 11.2f, 28.2f };
    poly(cv, stand, 4, vlin(22, 28, HEX(0xCDCDD2), HEX(0x9A9AA0)));
    rrect(cv, 10.4f, 27.6f, 11.2f, 1.6f, 0.7f, solid(HEX(0x8E8E93)));
    orrect(cv, 2.2f, 4, 27.6f, 19.8f, 2.0f, HEX(0x98989E), vlin(4, 24, HEX(0xF5F5F7), HEX(0xD6D6DB)));
    rrect(cv, 2.2f + PX, 18.8f, 27.6f - 2 * PX, 5.0f - PX, 1.6f, vlin(18, 24, HEX(0xE6E6EA), HEX(0xC2C2C8)));
    float sx = 3.6f, sy = 5.4f, sw = 24.8f, sh = 12.8f;
    rrect(cv, sx, sy, sw, sh, 0.6f, vlin(sy, sy + sh, HEX(0x7CC7FF), HEX(0x2F7BE6)));
    clip(cv, sx, sy, sw, sh);
    ellipse(cv, 10, sy + sh + 3, 13, 7.4f, vlin(sy + 6, sy + sh, HEX(0x9BE07A), HEX(0x2F9A3A)));
    ellipse(cv, 24, sy + sh + 4, 10, 7, vlin(sy + 7, sy + sh, HEX(0x7ED568), HEX(0x22862A)));
    unclip(cv);
}

static void trash_can(cv_t *cv)
{
    soft_shadow(cv, 16, 28.6f, 8.8f);
    const float body[] = { 5.4f, 7.4f, 26.6f, 7.4f, 24.4f, 28, 7.6f, 28 };
    poly(cv, body, 4, solid(HEX(0x98989E)));
    const float in[] = { 5.4f + PX * 1.2f, 7.4f, 26.6f - PX * 1.2f, 7.4f, 24.4f - PX, 28 - PX, 7.6f + PX, 28 - PX };
    poly(cv, in, 4, lin(6, 0, 26, 0, HEX(0xF7F7F9), HEX(0xCFCFD4)));
    for (int i = 0; i < 6; i++) {
        float t = (i + 1) / 7.0f;
        float x0 = 5.4f + 21.2f * t, x1 = 7.6f + 16.8f * t;
        line(cv, x0, 10, x1, 26, 0.7f, solid(RGBA(0, 0, 0, 30)));
        line(cv, x0 + 0.7f, 10, x1 + 0.6f, 26, 0.5f, solid(RGBA(255, 255, 255, 170)));
    }
    ellipse(cv, 16, 7.4f, 11.0f, 2.5f, solid(HEX(0x8E8E93)));
    ellipse(cv, 16, 7.4f, 11.0f - PX, 2.5f - PX, solid(HEX(0xEDEDF0)));
}

static void draw_trash(cv_t *cv)
{
    trash_can(cv);
    ellipse(cv, 16, 7.6f, 9.6f, 1.6f, vlin(6, 9.2f, HEX(0x58585D), HEX(0x8A8A90)));
}

static void draw_trash_full(cv_t *cv)
{
    trash_can(cv);
    ellipse(cv, 16, 7.6f, 9.6f, 1.6f, solid(HEX(0x6E6E73)));
    ellipse_rot(cv, 11.6f, 5.8f, 4.6f, 3.0f, -22, solid(HEX(0xBFC2C9)));
    ellipse_rot(cv, 11.6f, 5.6f, 4.6f - PX, 3.0f - PX, -22, solid(HEX(0xFFFFFF)));
    const float sheet[] = { 15.2f, 7.8f, 17.6f, 0.8f, 25.4f, 2.4f, 23.4f, 8.6f };
    poly(cv, sheet, 4, solid(HEX(0xBFC2C9)));
    const float sheet2[] = { 15.8f + PX, 7.6f, 18.0f, 1.4f, 24.6f, 2.8f, 22.8f, 8.2f };
    poly(cv, sheet2, 4, vlin(1, 8, HEX(0xFFFFFF), HEX(0xE8E8EC)));
    line(cv, 18.6f, 3.6f, 22.8f, 4.4f, 0.5f, solid(HEX(0xB4B6BD)));
    line(cv, 18.2f, 5.2f, 22.4f, 6.0f, 0.5f, solid(HEX(0xB4B6BD)));
}

/* ---- app icons ---- */

static void draw_finder(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0x8FDDFF), HEX(0x38A6F5));
    const float face[] = { 18.6f, 0, 32, 0, 32, 32, 16.4f, 32, 16.4f, 20.4f, 13.2f, 19.6f, 14.8f, 16.4f, 17.8f, 9.0f };
    poly(&l, face, 8, vlin(SQ_Y, SQ_Y + SQ_S, HEX(0x3A8CFB), HEX(0x1550D6)));
    rrect(&l, 9.3f, 8.6f, 2.4f, 6.0f, 1.2f, solid(HEX(0x10162A)));
    rrect(&l, 21.0f, 8.6f, 2.4f, 6.0f, 1.2f, solid(HEX(0x10162A)));
    ring_ex(&l, 16, 17.6f, 8.6f, 5.4f, 1.5f, 0, 1, 0, 0, solid(HEX(0x10162A)));
    squircle_merge(cv, &l);
}

static void draw_launchpad(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0x737378), HEX(0x2C2C2E));
    static const uint32_t c[9] = { 0xFF453A, 0xFF9F0A, 0xFFD60A, 0x32D74B, 0x64D2FF, 0x0A84FF, 0xBF5AF2, 0xFF375F, 0xE5E5EA };
    for (int i = 0; i < 9; i++) {
        float x = 7.2f + (i % 3) * 6.4f, y = 6.6f + (i / 3) * 6.4f;
        rrect(&l, x, y, 5.2f, 5.2f, 1.5f, vlin(y, y + 5.2f, color_shade(HEX(c[i]), 118), HEX(c[i])));
    }
    squircle_merge(cv, &l);
}

static void draw_terminal(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0xF4F4F6), HEX(0xB4B4BA));
    rrect(&l, 5.4f, 5.0f, 21.2f, 19.4f, 2.2f, solid(HEX(0x56565A)));
    rrect(&l, 5.9f, 5.5f, 20.2f, 18.4f, 1.8f, vlin(5, 24, HEX(0x2E2E30), HEX(0x111113)));
    line(&l, 9, 10.2f, 12.6f, 13.0f, 1.7f, solid(HEX(0xF2F2F2)));
    line(&l, 12.6f, 13.0f, 9, 15.8f, 1.7f, solid(HEX(0xF2F2F2)));
    line(&l, 14.2f, 16.2f, 19, 16.2f, 1.7f, solid(HEX(0xF2F2F2)));
    squircle_merge(cv, &l);
}

static void draw_notes(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0xFFFFFF), HEX(0xEEEEF0));
    rrect(&l, 0, 0, 32, 10.4f, 0, vlin(2, 10.4f, HEX(0xFFE57E), HEX(0xF8C134)));
    for (float x = 4.8f; x < 28; x += 2.2f)
        circle(&l, x, 11.8f, 0.45f, solid(HEX(0xB9B9BE)));
    for (int i = 0; i < 4; i++)
        line(&l, 3, 15.6f + i * 3.6f, 29, 15.6f + i * 3.6f, 0.6f, solid(HEX(0xD6D6DA)));
    squircle_merge(cv, &l);
}

static void draw_calculator(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0x5C5C61), HEX(0x28282B));
    rrect(&l, 15.6f, 6.2f, 9.2f, 3.2f, 1.6f, solid(RGBA(255, 255, 255, 235)));
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) {
            uint32_t col = c == 3 ? HEX(0xFF9F0A) : r == 0 ? HEX(0xBDBDC2) : HEX(0x7C7C82);
            float cx = 8.6f + c * 4.9f, cy = 14.0f + r * 4.9f;
            circle(&l, cx, cy, 2.1f, vlin(cy - 2, cy + 2, color_shade(col, 115), col));
        }
    squircle_merge(cv, &l);
}

static void draw_paint(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0xFFFFFF), HEX(0xD6E4F4));
    ellipse_rot(&l, 15, 16.4f, 10.8f, 8.0f, -14, solid(HEX(0xA27C4B)));
    ellipse_rot(&l, 15, 16.4f, 10.8f - PX, 8.0f - PX, -14, vlin(8, 25, HEX(0xFBE6C0), HEX(0xE0B97C)));
    ellipse(&l, 10.4f, 20.4f, 2.0f, 1.6f, solid(HEX(0xE6EEF8)));
    static const uint32_t blobs[] = { 0xFF453A, 0xFFD60A, 0x32D74B, 0x0A84FF, 0xBF5AF2 };
    static const float bx[] = { 8.6f, 12.4f, 17.4f, 21.6f, 20.0f }, by[] = { 15.2f, 11.8f, 11.0f, 13.8f, 19.0f };
    for (int i = 0; i < 5; i++)
        circle(&l, bx[i], by[i], 1.9f, rad(bx[i] - 0.6f, by[i] - 0.6f, 2.3f, color_shade(HEX(blobs[i]), 140), HEX(blobs[i])));
    line(&l, 17.6f, 24.6f, 26.6f, 6.6f, 1.9f, lin(17, 25, 27, 7, HEX(0xB0702E), HEX(0x6E4219)));
    line(&l, 16.4f, 26.8f, 17.9f, 24.0f, 2.5f, solid(HEX(0xAEB6BF)));
    circle(&l, 16.0f, 27.4f, 1.3f, solid(HEX(0x1E3A8A)));
    squircle_merge(cv, &l);
}

static void draw_preview(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0xEFF5FC), HEX(0xB6CAE2));
    const float back[] = { 7.4f, 8.4f, 23.4f, 5.4f, 26.2f, 19.8f, 10.2f, 22.8f };
    poly(&l, back, 4, solid(HEX(0xD0D6DE)));
    rrect(&l, 5.6f, 8.6f, 19.8f, 15.4f, 0.8f, solid(HEX(0x98A1AD)));
    rrect(&l, 5.6f + PX, 8.6f + PX, 19.8f - 2 * PX, 15.4f - 2 * PX, 0.6f, solid(HEX(0xFFFFFF)));
    landscape(&l, 7.0f, 10.0f, 17.0f, 12.6f);
    circle(&l, 22.4f, 21.4f, 3.0f, solid(RGBA(210, 236, 255, 170)));
    ring(&l, 22.4f, 21.4f, 4.2f, 1.4f, 0, 0, solid(HEX(0x3A3A3C)));
    line(&l, 25.4f, 24.4f, 28.2f, 27.2f, 2.1f, solid(HEX(0x3A3A3C)));
    squircle_merge(cv, &l);
}

static void mine(cv_t *cv, float cx, float cy, float r)
{
    for (int i = 0; i < 4; i++) {
        float a = i * PI / 4;
        float dx = fcos(a) * r * 1.5f, dy = fsin(a) * r * 1.5f;
        line(cv, cx - dx, cy - dy, cx + dx, cy + dy, r * 0.32f, solid(HEX(0x111111)));
    }
    circle(cv, cx, cy, r, rad(cx - r * 0.3f, cy - r * 0.3f, r * 1.3f, HEX(0x505050), HEX(0x0A0A0A)));
    circle(cv, cx - r * 0.35f, cy - r * 0.35f, r * 0.28f, solid(HEX(0xFFFFFF)));
}

static void draw_mines(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0xE6E9EE), HEX(0xA6ADB8));
    for (int i = 1; i < 5; i++) {
        line(&l, 3 + i * 5.2f, 0, 3 + i * 5.2f, 32, 0.5f, solid(RGBA(255, 255, 255, 130)));
        line(&l, 0, 2.4f + i * 5.2f, 32, 2.4f + i * 5.2f, 0.5f, solid(RGBA(255, 255, 255, 130)));
    }
    mine(&l, 16, 15.4f, 6.4f);
    squircle_merge(cv, &l);
}

static void draw_activity(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0x3A3A3C), HEX(0x0E0E10));
    for (int i = 1; i < 6; i++)
        line(&l, 0, 2.4f + i * 4.4f, 32, 2.4f + i * 4.4f, 0.45f, solid(RGBA(76, 217, 100, 64)));
    static const float gx[] = { 3, 9, 11.5f, 14, 16.5f, 19, 22, 29 }, gy[] = { 17, 17, 9.5f, 23, 12, 18.5f, 15.5f, 15.5f };
    for (int i = 0; i < 7; i++)
        line(&l, gx[i], gy[i], gx[i + 1], gy[i + 1], 1.5f, solid(HEX(0x4CD964)));
    squircle_merge(cv, &l);
}

static void gear(cv_t *cv, float cx, float cy, float r)
{
    paint_t metal = vlin(cy - r, cy + r, HEX(0xF7F7F9), HEX(0xB4B4BA));
    for (int k = 0; k < 12; k++) {
        float a = k * PI / 6 + PI / 12;
        float c = fcos(a), s = fsin(a);
        line(cv, cx + c * r * 0.72f, cy + s * r * 0.72f, cx + c * r * 1.02f, cy + s * r * 1.02f, r * 0.3f, metal);
    }
    circle(cv, cx, cy, r * 0.86f, metal);
    circle(cv, cx, cy, r * 0.58f, vlin(cy - r, cy + r, HEX(0x5E5E63), HEX(0x8E8E93)));
    circle(cv, cx, cy, r * 0.38f, vlin(cy - r, cy + r, HEX(0xE8E8EC), HEX(0xA6A6AC)));
}

static void draw_sysprefs(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0x98989E), HEX(0x46464A));
    gear(&l, 16, 15.4f, 9.6f);
    squircle_merge(cv, &l);
}

static void draw_spotlight(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0xFBFBFD), HEX(0xC6CAD2));
    circle(&l, 14, 13.6f, 5.0f, solid(RGBA(10, 132, 255, 40)));
    ring(&l, 14, 13.6f, 6.8f, 2.2f, 0, 0, solid(HEX(0x3A3A3C)));
    line(&l, 19.0f, 18.6f, 24.2f, 23.8f, 2.8f, solid(HEX(0x3A3A3C)));
    squircle_merge(cv, &l);
}

static void draw_sysinfo(cv_t *cv)
{
    cv_t l = layer_new(cv);
    backdrop(&l, HEX(0x6E6CF0), HEX(0x3533A0));
    for (int i = 0; i < 4; i++) {
        float p = 10.2f + i * 3.9f;
        line(&l, p, 5.6f, p, 25.2f, 1.1f, solid(HEX(0xD8D8E0)));
        line(&l, 6.2f, p - 0.6f, 25.8f, p - 0.6f, 1.1f, solid(HEX(0xD8D8E0)));
    }
    rrect(&l, 8.6f, 8.0f, 14.8f, 14.8f, 2, vlin(8, 23, HEX(0x4A4A4E), HEX(0x1C1C1E)));
    rrect(&l, 12.2f, 11.6f, 7.6f, 7.6f, 1.2f, vlin(11, 19, HEX(0x8E8E93), HEX(0x5A5A5F)));
    squircle_merge(cv, &l);
}

static void draw_kepler(cv_t *cv)
{
    /* orbit behind the planet */
    ring_ex(cv, 16, 16.5f, 15, 5.2f, 1.6f, -18, -1, 0, 0, lin(2, 0, 30, 0, HEX(0xFFE08A), HEX(0xF08C12)));
    circle(cv, 16, 16, 10, rad(12.5f, 11.5f, 14, HEX(0xC9FF9E), HEX(0x168A36)));
    clip(cv, 6, 6, 20, 20);
    ellipse_rot(cv, 18.5f, 19, 6, 3.4f, -25, solid(RGBA(255, 255, 255, 70)));
    ellipse_rot(cv, 10.5f, 15, 3, 5, 20, solid(RGBA(255, 255, 255, 60)));
    unclip(cv);
    ellipse(cv, 13, 11, 5, 3.2f, rad(13, 11, 5, RGBA(255, 255, 255, 170), RGBA(255, 255, 255, 0)));
    /* orbit in front */
    ring_ex(cv, 16, 16.5f, 15, 5.2f, 1.6f, -18, 1, 0, 0, lin(2, 0, 30, 0, HEX(0xFFE89C), HEX(0xF59A1B)));
    circle(cv, 27.4f, 9.2f, 2.1f, rad(26.8f, 8.6f, 2.6f, HEX(0xFFFFFF), HEX(0x9FD4FF)));
}

static void draw_guest(cv_t *cv)
{
    cv_t l = layer_new(cv);
    rrect(&l, 0, 0, 32, 32, 0, vlin(0, 32, HEX(0xBDBDC2), HEX(0x8A8A90)));
    circle(&l, 16, 12.4f, 5.8f, solid(HEX(0xF4F4F6)));
    ellipse(&l, 16, 29.4f, 11, 9.2f, solid(HEX(0xF4F4F6)));
    shape_t m = { 0 };
    m.kind = SH_ELLIPSE;
    m.cx = U(cv, 16);
    m.cy = U(cv, 16);
    m.rx = m.ry = U(cv, 16);
    m.cosa = 1024;
    layer_merge(cv, &l, &m);
}

/* ---- alert badges ---- */

static void flat_badge(cv_t *cv, uint32_t top, uint32_t bottom)
{
    soft_shadow(cv, 16, 29.6f, 10);
    circle(cv, 16, 15.6f, 13.6f, vlin(2, 29, top, bottom));
}

static void draw_help(cv_t *cv)
{
    flat_badge(cv, HEX(0x5AAEFF), HEX(0x0A64DE));
    glyph_text(cv, "?", HEX(0xFFFFFF));
}

static void draw_info(cv_t *cv)
{
    flat_badge(cv, HEX(0x5AAEFF), HEX(0x0A64DE));
    rrect(cv, 14.4f, 13.4f, 3.2f, 10.2f, 1.6f, solid(HEX(0xFFFFFF)));
    circle(cv, 16, 9.2f, 2, solid(HEX(0xFFFFFF)));
}

static void draw_error(cv_t *cv)
{
    flat_badge(cv, HEX(0xFF7A70), HEX(0xD92D20));
    line(cv, 11.2f, 10.8f, 20.8f, 20.4f, 3, solid(HEX(0xFFFFFF)));
    line(cv, 20.8f, 10.8f, 11.2f, 20.4f, 3, solid(HEX(0xFFFFFF)));
}

static void draw_warning(cv_t *cv)
{
    soft_shadow(cv, 16, 29.6f, 12);
    line(cv, 16, 4.6f, 4.4f, 25.6f, 4.6f, solid(HEX(0xE8A300)));
    line(cv, 16, 4.6f, 27.6f, 25.6f, 4.6f, solid(HEX(0xE8A300)));
    line(cv, 4.4f, 25.6f, 27.6f, 25.6f, 4.6f, solid(HEX(0xE8A300)));
    const float tri[] = { 16, 4.6f, 27.6f, 25.6f, 4.4f, 25.6f };
    poly(cv, tri, 3, vlin(3, 28, HEX(0xFFDD55), HEX(0xF5B400)));
    rrect(cv, 14.6f, 10.6f, 2.8f, 9.4f, 1.4f, solid(HEX(0x2A2A2A)));
    circle(cv, 16, 23.0f, 1.7f, solid(HEX(0x2A2A2A)));
}

/* ---- the old Luna pieces some screens still ask for ---- */

static void tile(cv_t *cv, uint32_t light, uint32_t dark, uint32_t edge)
{
    orrect(cv, 1, 1, 30, 30, 7, edge, lin(4, 2, 26, 30, light, dark));
}

static void draw_power(cv_t *cv)
{
    tile(cv, HEX(0xFF8A70), HEX(0xD92D20), HEX(0xB02418));
    g_power(cv);
}

static void draw_standby(cv_t *cv)
{
    tile(cv, HEX(0xFFD86A), HEX(0xE89B00), HEX(0xB57A00));
    g_sleep(cv);
}

static void draw_restart(cv_t *cv)
{
    tile(cv, HEX(0x7EDB6E), HEX(0x28A745), HEX(0x1E7E34));
    g_restart(cv);
}

static void draw_logoff(cv_t *cv)
{
    tile(cv, HEX(0x8AC4FF), HEX(0x0A64DE), HEX(0x0850B0));
    g_person(cv);
}

static void draw_up(cv_t *cv)
{
    folder(cv);
    emboss(cv, g_chevron_l);
}

static void arrow_badge(cv_t *cv, int dir)
{
    flat_badge(cv, HEX(0xF2F2F4), HEX(0xD1D1D6));
    float s = dir > 0 ? 1 : -1;
    line(cv, 16 - 6 * s, 16, 16 + 3 * s, 16, 3.0f, solid(HEX(0x3A3A3C)));
    const float head[] = { 16 + 8.5f * s, 16, 16 + 1.5f * s, 9, 16 + 1.5f * s, 23 };
    poly(cv, head, 3, solid(HEX(0x3A3A3C)));
}

static void draw_back(cv_t *cv) { arrow_badge(cv, -1); }
static void draw_forward(cv_t *cv) { arrow_badge(cv, 1); }

static const icon_fn icon_fns[ICON_COUNT] = {
    [ICON_COMPUTER] = draw_computer,
    [ICON_DOCUMENTS] = draw_documents,
    [ICON_RECYCLE] = draw_trash,
    [ICON_CMD] = draw_terminal,
    [ICON_BRADY] = draw_help,
    [ICON_NOTEPAD] = draw_notes,
    [ICON_CALC] = draw_calculator,
    [ICON_PAINT] = draw_paint,
    [ICON_MINES] = draw_mines,
    [ICON_TASKMGR] = draw_activity,
    [ICON_PICTURES] = draw_preview,
    [ICON_HELP] = draw_help,
    [ICON_DISPLAY] = draw_sysprefs,
    [ICON_RUN] = draw_spotlight,
    [ICON_LOGOFF] = draw_logoff,
    [ICON_POWER] = draw_power,
    [ICON_FOLDER] = draw_folder,
    [ICON_TEXTFILE] = draw_textfile,
    [ICON_DRIVE] = draw_drive,
    [ICON_CDROM] = draw_cdrom,
    [ICON_KEPLER] = draw_kepler,
    [ICON_ALLPROGS] = draw_launchpad,
    [ICON_STANDBY] = draw_standby,
    [ICON_RESTART] = draw_restart,
    [ICON_SWITCHUSER] = draw_logoff,
    [ICON_SPEAKER] = g_speaker,
    [ICON_NETWORK] = g_wifi,
    [ICON_SHOWDESKTOP] = draw_folder_desktop,
    [ICON_INFO] = draw_info,
    [ICON_WARNING] = draw_warning,
    [ICON_ERROR] = draw_error,
    [ICON_BACK] = draw_back,
    [ICON_FORWARD] = draw_forward,
    [ICON_UP] = draw_up,
    [ICON_SYSINFO] = draw_sysinfo,
    [ICON_FINDER] = draw_finder,
    [ICON_TRASH_FULL] = draw_trash_full,
    [ICON_IMAGEFILE] = draw_imagefile,
    [ICON_FOLDER_DESKTOP] = draw_folder_desktop,
    [ICON_FOLDER_PICTURES] = draw_folder_pictures,
    [ICON_FOLDER_DOWNLOADS] = draw_folder_downloads,
    [ICON_FOLDER_APPS] = draw_folder_apps,
    [ICON_GUEST] = draw_guest,
    [GLYPH_KEPLER] = g_kepler,
    [GLYPH_WIFI] = g_wifi,
    [GLYPH_SPEAKER] = g_speaker,
    [GLYPH_SEARCH] = g_search,
    [GLYPH_CONTROL] = g_control,
    [GLYPH_AIRDROP] = g_airdrop,
    [GLYPH_RECENTS] = g_recents,
    [GLYPH_DESKTOP] = g_desktop,
    [GLYPH_APPS] = g_apps,
    [GLYPH_DOCUMENT] = g_document,
    [GLYPH_DOWNLOADS] = g_downloads,
    [GLYPH_CLOUD] = g_cloud,
    [GLYPH_DISK] = g_disk,
    [GLYPH_TRASH] = g_trash,
    [GLYPH_CHEVRON_L] = g_chevron_l,
    [GLYPH_CHEVRON_R] = g_chevron_r,
    [GLYPH_GRID] = g_grid,
    [GLYPH_LIST] = g_list,
    [GLYPH_COLUMNS] = g_columns,
    [GLYPH_GALLERY] = g_gallery,
    [GLYPH_GROUP] = g_group,
    [GLYPH_SHARE] = g_share,
    [GLYPH_TAG] = g_tag,
    [GLYPH_MORE] = g_more,
    [GLYPH_ARROW_R] = g_arrow_r,
    [GLYPH_SLEEP] = g_sleep,
    [GLYPH_RESTART] = g_restart,
    [GLYPH_POWER] = g_power,
    [GLYPH_PERSON] = g_person,
    [GLYPH_TL_CLOSE] = g_tl_close,
    [GLYPH_TL_MIN] = g_tl_min,
    [GLYPH_TL_ZOOM] = g_tl_zoom,
    [GLYPH_CHECK] = g_check,
    [GLYPH_CHEVRON_D] = g_chevron_d,
    [GLYPH_CMD] = g_cmd,
    [GLYPH_LOCK] = g_lock,
    [GLYPH_RELOAD] = g_reload,
    [GLYPH_HOME] = g_home,
    [GLYPH_CLOSE] = g_close,
    [GLYPH_SHIELD] = g_shield,
    [GLYPH_GLOBE] = g_globe,
};

/* ---- cache ------------------------------------------------------------------------- */

static const int sizes[] = { 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128 };
#define N_SIZES ((int)ARRAY_LEN(sizes))
static image_t *cache[ICON_COUNT][N_SIZES];

static int size_slot(int size)
{
    for (int i = 0; i < N_SIZES; i++)
        if (sizes[i] == size)
            return i;
    return -1;
}

static const image_t *pictures[ICON_COUNT];
static bool picture_plain[ICON_COUNT]; /* has its own shape: drawn as it is */

void icons_set_image(int id, const image_t *src)
{
    if (id >= 0 && id < ICON_COUNT)
        pictures[id] = src;
}

void icons_set_image_plain(int id, const image_t *src)
{
    if (id >= 0 && id < ICON_COUNT) {
        pictures[id] = src;
        picture_plain[id] = true;
    }
}

/* scaled with premultiplied alpha so transparent edges don't darken */
static image_t *resize_alpha(const image_t *src, int w, int h)
{
    image_t *pm = image_new(src->w, src->h);
    for (int i = 0; i < src->w * src->h; i++) {
        uint32_t c = src->px[i], a = c >> 24;
        pm->px[i] = a << 24 | (((c >> 16) & 255) * a / 255) << 16 | (((c >> 8) & 255) * a / 255) << 8 | (c & 255) * a / 255;
    }
    image_t *out = image_resize(pm, w, h);
    image_free(pm);
    for (int i = 0; i < w * h; i++) {
        uint32_t c = out->px[i], a = c >> 24;
        if (!a) {
            out->px[i] = 0;
            continue;
        }
        uint32_t r = MIN(255u, ((c >> 16) & 255) * 255 / a), g = MIN(255u, ((c >> 8) & 255) * 255 / a),
                 b = MIN(255u, (c & 255) * 255 / a);
        out->px[i] = a << 24 | r << 16 | g << 8 | b;
    }
    return out;
}

/* the apps' icons are white: colour drained, lightened, detail kept in soft greys */
static bool whitened(int id)
{
    switch (id) {
    case ICON_CMD:
    case ICON_NOTEPAD:
    case ICON_CALC:
    case ICON_PAINT:
    case ICON_MINES:
    case ICON_TASKMGR:
    case ICON_PICTURES:
    case ICON_DISPLAY:
    case ICON_ALLPROGS:
    case ICON_SYSINFO:
        return true;
    }
    return false;
}

static int luma(uint32_t c) { return ((int)((c >> 16) & 255) * 77 + (int)((c >> 8) & 255) * 151 + (int)(c & 255) * 28) >> 8; }

static void whiten(image_t *img)
{
    /* a dark icon (Terminal, Calculator...) is turned inside out first, so every
     * tile comes out white with its drawing in soft greys */
    uint64_t sum = 0, weight = 0;
    for (int i = 0; i < img->w * img->h; i++) {
        uint32_t a = img->px[i] >> 24;
        sum += (uint64_t)luma(img->px[i]) * a;
        weight += a;
    }
    bool dark = weight && sum / weight < 140;
    for (int i = 0; i < img->w * img->h; i++) {
        uint32_t c = img->px[i], a = c >> 24;
        int lum = luma(c);
        if (dark)
            lum = 255 - lum;
        int v = 255 - (255 - lum) * 62 / 100;
        img->px[i] = a << 24 | (uint32_t)v << 16 | (uint32_t)v << 8 | (uint32_t)v;
    }
}

/* a picture scaled onto the app-icon squircle */
static void draw_picture(cv_t *cv, const image_t *src)
{
    cv_t l = layer_new(cv);
    int x0 = (int)(SQ_X * cv->size / 32), y0 = (int)(SQ_Y * cv->size / 32);
    int side = (int)(SQ_S * cv->size / 32 + 1.999f);
    image_t *scaled = image_resize(src, side, side);
    gfx_blit(&l.g, x0, y0, scaled);
    image_free(scaled);
    /* a faint rim so a dark picture still reads as an icon */
    rrect(&l, SQ_X, SQ_Y, SQ_S, 2.4f, 0, vlin(SQ_Y, SQ_Y + 2.4f, RGBA(255, 255, 255, 40), RGBA(255, 255, 255, 0)));
    squircle_merge(cv, &l);
}

static image_t *render(int id, int size)
{
    cv_t cv = { 0 };
    cv.img = image_new(size, size);
    cv.g = gfx_for_image(cv.img);
    cv.size = size;
    if (pictures[id] && picture_plain[id]) {
        image_free(cv.img);
        return resize_alpha(pictures[id], size, size);
    }
    if (pictures[id])
        draw_picture(&cv, pictures[id]);
    else if (icon_fns[id])
        icon_fns[id](&cv);
    if (whitened(id))
        whiten(cv.img);
    return cv.img;
}

const image_t *icon_get(int id, int size)
{
    if (id < 0 || id >= ICON_COUNT)
        id = ICON_TEXTFILE;
    int slot = size_slot(size);
    if (slot < 0)
        return render(id, size); /* uncached (callers keep it) */
    if (!cache[id][slot])
        cache[id][slot] = render(id, size);
    return cache[id][slot];
}

void icons_prerender(void (*progress)(int done, int total))
{
    static const struct {
        int id, size;
    } common[] = {
        { ICON_FINDER, 48 },   { ICON_ALLPROGS, 48 }, { ICON_BROWSER, 48 }, { ICON_CMD, 48 },     { ICON_NOTEPAD, 48 },
        { ICON_CALC, 48 },     { ICON_PAINT, 48 },    { ICON_PICTURES, 48 }, { ICON_MINES, 48 },
        { ICON_TASKMGR, 48 },  { ICON_DISPLAY, 48 },  { ICON_RECYCLE, 48 }, { ICON_DRIVE, 64 },
        { GLYPH_KEPLER, 16 },  { GLYPH_WIFI, 16 },    { GLYPH_SEARCH, 14 }, { GLYPH_CONTROL, 16 },
        { GLYPH_TL_CLOSE, 12 }, { GLYPH_TL_MIN, 12 }, { GLYPH_TL_ZOOM, 12 }, { ICON_KEPLER, 64 },
    };
    int n = ARRAY_LEN(common);
    for (int i = 0; i < n; i++) {
        icon_get(common[i].id, common[i].size);
        if (progress)
            progress(i + 1, n);
    }
}

/* ---- user picture ---------------------------------------------------------------- */

static image_t *user_pic[N_SIZES];
static image_t *user_src;

void icons_set_user_picture(const image_t *wall)
{
    /* the ladybug flying in the middle of the wallpaper (1920x1200 picture, cropped to the screen) */
    int cx = wall->w / 2 - wall->h * 25 / 1200, cy = wall->h * 495 / 1200, r = wall->h * 105 / 1200;
    int side = 2 * r;
    image_free(user_src);
    user_src = image_new(side, side);
    for (int y = 0; y < side; y++)
        for (int x = 0; x < side; x++) {
            int sx = CLAMP(cx - r + x, 0, wall->w - 1), sy = CLAMP(cy - r + y, 0, wall->h - 1);
            user_src->px[y * side + x] = wall->px[sy * wall->w + sx];
        }
}

const image_t *icon_user_picture(int size)
{
    int slot = size_slot(size);
    if (slot < 0)
        slot = 7;
    if (!user_pic[slot] && user_src) {
        int s = sizes[slot];
        image_t *img = image_resize(user_src, s, s);
        image_t *mask = image_new(s, s);
        gfx_t mg = gfx_for_image(mask);
        gfx_circle(&mg, s / 2, s / 2, s / 2, HEX(0xFFFFFF));
        for (int i = 0; i < s * s; i++)
            img->px[i] = (img->px[i] & 0xFFFFFF) | (mask->px[i] & 0xFF000000u);
        image_free(mask);
        user_pic[slot] = img;
    }
    return user_pic[slot];
}

/* ---- cursors ------------------------------------------------------------------------ */

/* black with a white outline, like the Mac pointer */
static const char *arrow_cursor[] = {
    "o",
    "oo",
    "o#o",
    "o##o",
    "o###o",
    "o####o",
    "o#####o",
    "o######o",
    "o#######o",
    "o########o",
    "o#########o",
    "o######oooo",
    "o###o##o",
    "o##oo##o",
    "o#o  o##o",
    "oo   o##o",
    "o     o##o",
    "      o##o",
    "       oo",
};

static const char *hand_cursor[] = {
    "     XX",
    "    X..X",
    "    X..X",
    "    X..X",
    "    X..XXX",
    "    X..X..XXX",
    "    X..X..X..XX",
    "XXX X..X..X..X.X",
    "X..XX........X..X",
    "X...X...........X",
    " X..............X",
    "  X.............X",
    "  X............X",
    "   X...........X",
    "    X.........X",
    "    X.........X",
    "     X.......X",
    "     X.......X",
    "     XXXXXXXXX",
};

/* 'X' and '#' are black, '.' and 'o' white; everything gets a soft shadow */
static void draw_pattern(gfx_t *g, const char **rows, int n, int x, int y)
{
    for (int j = 0; j < n; j++)
        for (int i = 0; rows[j][i]; i++)
            if (rows[j][i] != ' ') {
                gfx_pixel(g, x + i + 1, y + j + 2, RGBA(0, 0, 0, 40));
                gfx_pixel(g, x + i, y + j + 1, RGBA(0, 0, 0, 30));
            }
    for (int j = 0; j < n; j++)
        for (int i = 0; rows[j][i]; i++) {
            char c = rows[j][i];
            if (c == 'X' || c == '#')
                gfx_pixel(g, x + i, y + j, HEX(0x000000));
            else if (c == '.' || c == 'o')
                gfx_pixel(g, x + i, y + j, HEX(0xFFFFFF));
        }
}

/* a black double arrow with a white outline, along (dx, dy) */
static void double_arrow(gfx_t *g, int x, int y, int dx, int dy)
{
    for (int pass = 0; pass < 2; pass++) {
        uint32_t c = pass ? HEX(0x000000) : HEX(0xFFFFFF);
        int w = pass ? 0 : 1;
#define DOT(px, py) gfx_fill(g, (px) - w, (py) - w, 1 + 2 * w, 1 + 2 * w, c)
        for (int t = -7; t <= 7; t++)
            DOT(x + t * dx, y + t * dy);
        for (int e = -1; e <= 1; e += 2) {
            int tx = x + e * 7 * dx, ty = y + e * 7 * dy;
            for (int k = 1; k <= 4; k++) {
                if (dx && dy) {
                    DOT(tx - e * k * dx, ty);
                    DOT(tx, ty - e * k * dy);
                } else {
                    DOT(tx - e * k * dx - k * dy, ty - e * k * dy - k * dx);
                    DOT(tx - e * k * dx + k * dy, ty - e * k * dy + k * dx);
                }
            }
        }
#undef DOT
    }
}

rect_t cursor_bounds(int cursor, int x, int y)
{
    switch (cursor) {
    case CURSOR_ARROW: return rect_make(x, y, 13, 21);
    case CURSOR_HAND: return rect_make(x - 6, y, 20, 22);
    case CURSOR_BUSY: return rect_make(x, y, 30, 24);
    default: return rect_make(x - 12, y - 12, 25, 25);
    }
}

void cursor_draw(gfx_t *g, int cursor, int x, int y)
{
    switch (cursor) {
    case CURSOR_ARROW:
        draw_pattern(g, arrow_cursor, ARRAY_LEN(arrow_cursor), x, y);
        break;
    case CURSOR_HAND:
        draw_pattern(g, hand_cursor, ARRAY_LEN(hand_cursor), x - 5, y);
        break;
    case CURSOR_BUSY:
        draw_pattern(g, arrow_cursor, ARRAY_LEN(arrow_cursor), x, y);
        {
            /* the spinning wheel, frozen */
            static const uint32_t wheel[6] = { 0xFF453A, 0xFF9F0A, 0xFFD60A, 0x32D74B, 0x0A84FF, 0xBF5AF2 };
            gfx_circle(g, x + 20, y + 15, 7, HEX(0x3A3A3C));
            for (int k = 0; k < 6; k++)
                gfx_circle(g, x + 20 + (k % 3 - 1) * 2, y + 15 + (k / 3) * 3 - 2, 3, HEX(wheel[k]));
        }
        break;
    case CURSOR_IBEAM:
        for (int pass = 0; pass < 2; pass++) {
            uint32_t c = pass ? HEX(0x000000) : HEX(0xFFFFFF);
            int o = pass ? 0 : 1;
            gfx_fill(g, x - o, y - 7 - o, 1 + 2 * o, 15 + 2 * o, c);
            gfx_fill(g, x - 3 - o, y - 8 - o, 3 + 2 * o, 1 + 2 * o, c);
            gfx_fill(g, x + 1 - o, y - 8 - o, 3 + 2 * o, 1 + 2 * o, c);
            gfx_fill(g, x - 3 - o, y + 8 - o, 3 + 2 * o, 1 + 2 * o, c);
            gfx_fill(g, x + 1 - o, y + 8 - o, 3 + 2 * o, 1 + 2 * o, c);
        }
        break;
    case CURSOR_SIZE_WE:
        double_arrow(g, x, y, 1, 0);
        break;
    case CURSOR_SIZE_NS:
        double_arrow(g, x, y, 0, 1);
        break;
    case CURSOR_SIZE_NWSE:
        double_arrow(g, x, y, 1, 1);
        break;
    case CURSOR_SIZE_NESW:
        double_arrow(g, x, y, 1, -1);
        break;
    case CURSOR_MOVE:
        double_arrow(g, x, y, 1, 0);
        double_arrow(g, x, y, 0, 1);
        break;
    }
}
