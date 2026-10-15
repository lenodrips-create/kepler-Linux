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

static void fill(cv_t *cv, shape_t *s, paint_t p)
{
    int px0 = MAX(0, s->bx0 >> 3), py0 = MAX(0, s->by0 >> 3);
    int px1 = MIN(cv->size - 1, (s->bx1 >> 3) + 1), py1 = MIN(cv->size - 1, (s->by1 >> 3) + 1);
    for (int py = py0; py <= py1; py++)
        for (int px = px0; px <= px1; px++) {
            int n = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    int sx = px * 8 + 1 + 2 * i, sy = py * 8 + 1 + 2 * j;
                    if (cv->clip && (sx < cv->cx0 || sx >= cv->cx1 || sy < cv->cy0 || sy >= cv->cy1))
                        continue;
                    if (inside(s, sx, sy))
                        n++;
                }
            if (!n)
                continue;
            int cov = n * 255 / 16;
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

static void ocircle(cv_t *cv, float cx, float cy, float r, uint32_t outline, paint_t p)
{
    circle(cv, cx, cy, r, solid(outline));
    circle(cv, cx, cy, r - PX, p);
}

static void glyph_text(cv_t *cv, const char *s, uint32_t c)
{
    const font_t *f = cv->size >= 40 ? &font_big_bold : cv->size >= 28 ? &font_big_bold
                    : cv->size >= 20 ? &font_heading : &font_ui_bold;
    int w = text_width(f, s);
    gfx_text(&cv->g, f, (cv->size - w) / 2, (cv->size - f->height) / 2 + (cv->size >= 28 ? 0 : 1), s, c);
}

/* ---- icon artwork ------------------------------------------------------------------ */

static void shadow(cv_t *cv, float cx, float cy, float rx)
{
    ellipse(cv, cx, cy, rx, 1.8f, rad(cx, cy, rx, RGBA(0, 0, 0, 70), RGBA(0, 0, 0, 0)));
}

static void monitor(cv_t *cv, float x, float y, float w, float h)
{
    /* stand */
    rrect(cv, x + w / 2 - 3, y + h - 1, 6, 4.5f, 0, lin(0, 0, 32, 0, HEX(0x9AA3AD), HEX(0x6F7884)));
    orrect(cv, x + w / 2 - 8, y + h + 2.5f, 16, 3.5f, 1.5f, HEX(0x58616C), vlin(y + h + 2, y + h + 6, HEX(0xEEF0F2), HEX(0xA6AEB8)));
    /* case */
    orrect(cv, x, y, w, h, 2.5f, HEX(0x4C5662), vlin(y, y + h, HEX(0xF8F9FA), HEX(0xBCC3CB)));
    rrect(cv, x + 2.4f, y + 2.4f, w - 4.8f, h - 5.6f, 1, solid(HEX(0x343C46)));
}

static void draw_computer(cv_t *cv)
{
    shadow(cv, 16, 29.6f, 11);
    monitor(cv, 2, 2.5f, 28, 21);
    float sx = 5.4f, sy = 5.4f, sw = 21.2f, sh = 13.2f;
    rrect(cv, sx, sy, sw, sh, 0.5f, vlin(sy, sy + sh, HEX(0x8AD0FF), HEX(0x1E63D6)));
    clip(cv, sx, sy, sw, sh);
    ellipse(cv, 11, sy + sh + 3, 14, 8, vlin(sy + 7, sy + sh, HEX(0x8BE36A), HEX(0x2C9A2C)));
    ellipse(cv, 26, sy + sh + 4, 10, 7, vlin(sy + 8, sy + sh, HEX(0x6FD456), HEX(0x22862A)));
    poly(cv, (const float[]){ sx, sy, sx + sw * 0.7f, sy, sx, sy + sh * 0.9f }, 3, solid(RGBA(255, 255, 255, 55)));
    unclip(cv);
    circle(cv, 25.6f, 21.4f, 0.9f, solid(HEX(0x37D637)));
}

static void folder_back(cv_t *cv, float y)
{
    orrect(cv, 2.5f, y, 12, 6, 1.5f, HEX(0xB8861B), solid(HEX(0xE9B23A)));
    orrect(cv, 2.5f, y + 2.5f, 27, 21, 1.5f, HEX(0xB8861B), vlin(y, y + 22, HEX(0xF5CD62), HEX(0xE0A52C)));
}

static void folder_front(cv_t *cv, float y)
{
    const float front[] = { 1.5f, y + 8, 30.5f, y + 8, 29, y + 23.5f, 3, y + 23.5f };
    poly(cv, front, 4, solid(HEX(0xBE8D22)));
    const float inner[] = { 1.5f + PX * 1.2f, y + 8 + PX, 30.5f - PX * 1.2f, y + 8 + PX, 29 - PX, y + 23.5f - PX,
                            3 + PX, y + 23.5f - PX };
    poly(cv, inner, 4, vlin(y + 8, y + 23, HEX(0xFFE8A0), HEX(0xF3BF46)));
    line(cv, 3, y + 9.6f, 29, y + 9.6f, 0.8f, solid(RGBA(255, 255, 255, 140)));
}

static void draw_folder(cv_t *cv)
{
    shadow(cv, 16, 29.8f, 12);
    folder_back(cv, 4.5f);
    folder_front(cv, 4.5f);
}

static void paper(cv_t *cv, float x, float y, float w, float h)
{
    orrect(cv, x, y, w, h, 0.6f, HEX(0x9AA3AD), vlin(y, y + h, HEX(0xFFFFFF), HEX(0xEDF1F5)));
    for (float ly = y + 3; ly < y + h - 2; ly += 2.6f)
        line(cv, x + 2.5f, ly, x + w - 2.5f, ly, 0.7f, solid(HEX(0xA9C3E3)));
}

static void draw_documents(cv_t *cv)
{
    shadow(cv, 16, 29.8f, 12);
    folder_back(cv, 4.5f);
    paper(cv, 8.5f, 2.5f, 17, 16);
    paper(cv, 6, 4.5f, 17, 16);
    folder_front(cv, 4.5f);
}

static void draw_recycle(cv_t *cv)
{
    shadow(cv, 16, 29.8f, 9.5f);
    const float body[] = { 6, 8, 26, 8, 23.6f, 29, 8.4f, 29 };
    poly(cv, body, 4, solid(HEX(0x5F7569)));
    const float inner[] = { 6 + PX * 1.3f, 8, 26 - PX * 1.3f, 8, 23.6f - PX, 29 - PX, 8.4f + PX, 29 - PX };
    poly(cv, inner, 4, lin(6, 0, 26, 0, HEX(0xF4FAF7), HEX(0x9FB8AB)));
    for (int i = 0; i < 4; i++) {
        float x0 = 10 + i * 4, x1 = 10.8f + i * 3.5f;
        line(cv, x0, 11, x1, 27, 0.7f, solid(RGBA(70, 100, 85, 90)));
    }
    ellipse(cv, 16, 8, 10.6f, 2.6f, solid(HEX(0x5F7569)));
    ellipse(cv, 16, 8, 10.6f - PX, 2.6f - PX, solid(HEX(0xE2EDE7)));
    ellipse(cv, 16, 8, 8.6f, 1.6f, solid(HEX(0x86A193)));
    /* recycling triangle */
    const float tri[] = { 16, 12.2f, 21.8f, 22.4f, 10.2f, 22.4f };
    poly(cv, tri, 3, solid(HEX(0x1F9A35)));
    const float hole[] = { 16, 15.6f, 19.2f, 20.8f, 12.8f, 20.8f };
    poly(cv, hole, 3, lin(6, 0, 26, 0, HEX(0xEDF6F1), HEX(0xB6CBBF)));
    line(cv, 14.2f, 13.6f, 12.4f, 16.4f, 1.6f, solid(HEX(0x5FD46F)));
}

static void window_frame(cv_t *cv, float x, float y, float w, float h, uint32_t body)
{
    orrect(cv, x, y, w, h, 1.5f, HEX(0x26344F), solid(body));
    rrect(cv, x + PX, y + PX, w - 2 * PX, 4.6f, 1, vlin(y, y + 5, HEX(0x5C9BFA), HEX(0x1D57D6)));
}

static void draw_cmd(cv_t *cv)
{
    shadow(cv, 16, 29.6f, 12);
    window_frame(cv, 2, 4, 28, 24, HEX(0x101010));
    line(cv, 6, 13, 10, 15.8f, 1.6f, solid(HEX(0xD8D8D8)));
    line(cv, 10, 15.8f, 6, 18.6f, 1.6f, solid(HEX(0xD8D8D8)));
    line(cv, 12, 19.4f, 17.5f, 19.4f, 1.6f, solid(HEX(0xD8D8D8)));
}

static void draw_notepad(cv_t *cv)
{
    shadow(cv, 16, 29.8f, 10.5f);
    orrect(cv, 6, 3.5f, 21, 25.5f, 1, HEX(0x6E7B8B), vlin(3, 29, HEX(0xFFFFFF), HEX(0xE6ECF3)));
    rrect(cv, 6 + PX, 3.5f + PX, 21 - 2 * PX, 4.5f, 0.8f, vlin(3, 8, HEX(0x9CCBFA), HEX(0x3A79D2)));
    for (int i = 0; i < 5; i++)
        rrect(cv, 8.6f + i * 4, 1.6f, 1.6f, 4, 0.8f, solid(HEX(0x3A424C)));
    for (int i = 0; i < 6; i++)
        line(cv, 9, 11.5f + i * 3, i == 5 ? 18 : 24, 11.5f + i * 3, 0.8f, solid(HEX(0x7FA7D9)));
}

static void draw_calc(cv_t *cv)
{
    shadow(cv, 16, 29.8f, 10.5f);
    orrect(cv, 5, 2, 22, 27.5f, 2.5f, HEX(0x47505B), vlin(2, 30, HEX(0xF3F5F7), HEX(0xB3BCC6)));
    orrect(cv, 7.6f, 4.6f, 16.8f, 6.2f, 1, HEX(0x5D7A57), vlin(4, 11, HEX(0xDDF2D6), HEX(0xB8D9AE)));
    line(cv, 16.5f, 7.7f, 22, 7.7f, 1.4f, solid(HEX(0x2F3F2C)));
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            uint32_t col = c == 3 ? HEX(0xF08A4B) : HEX(0xFFFFFF);
            orrect(cv, 7.6f + c * 4.3f, 12.8f + r * 4, 3.4f, 3.1f, 0.8f, HEX(0x6D7783), solid(col));
        }
}

static void draw_paint(cv_t *cv)
{
    shadow(cv, 15, 29.6f, 12);
    ellipse_rot(cv, 14.5f, 17, 13, 9.5f, -12, solid(HEX(0x9A7B4F)));
    ellipse_rot(cv, 14.5f, 17, 13 - PX, 9.5f - PX, -12, vlin(8, 27, HEX(0xFDEFD2), HEX(0xE0BF8A)));
    ellipse(cv, 9.6f, 21.8f, 2.4f, 1.9f, erase());
    static const uint32_t blobs[] = { 0xE53935, 0xFDD835, 0x43A047, 0x1E88E5, 0x8E24AA };
    static const float bx[] = { 7.6f, 12.4f, 18, 22.6f, 20.4f }, by[] = { 15.6f, 11.6f, 10.6f, 14.4f, 20.6f };
    for (int i = 0; i < 5; i++)
        circle(cv, bx[i], by[i], 2.1f, rad(bx[i] - 0.7f, by[i] - 0.7f, 2.6f, color_shade(HEX(blobs[i]), 140), HEX(blobs[i])));
    line(cv, 17, 26, 29, 4, 2.2f, lin(17, 26, 29, 4, HEX(0xB0702E), HEX(0x6E4219)));
    line(cv, 15.6f, 28.4f, 17.6f, 24.8f, 2.8f, solid(HEX(0xAEB6BF)));
    circle(cv, 15, 29.2f, 1.6f, solid(HEX(0x1E3A8A)));
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
    shadow(cv, 16, 29.8f, 10);
    mine(cv, 16, 15.5f, 8);
}

static void draw_taskmgr(cv_t *cv)
{
    shadow(cv, 16, 29.6f, 11);
    monitor(cv, 2, 2.5f, 28, 21);
    rrect(cv, 5.4f, 5.4f, 21.2f, 13.2f, 0.5f, solid(HEX(0x000000)));
    for (int i = 1; i < 4; i++)
        line(cv, 5.6f, 5.4f + i * 3.3f, 26.4f, 5.4f + i * 3.3f, 0.5f, solid(HEX(0x0B5A0B)));
    static const float gx[] = { 6, 9.5f, 12.5f, 15.5f, 18.5f, 21.5f, 25.5f }, gy[] = { 16, 13, 15, 8.5f, 12, 9.5f, 11 };
    for (int i = 0; i < 6; i++)
        line(cv, gx[i], gy[i], gx[i + 1], gy[i + 1], 1.2f, solid(HEX(0x39F039)));
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

static void draw_pictures(cv_t *cv)
{
    shadow(cv, 16, 29.6f, 12);
    orrect(cv, 2.5f, 4.5f, 27, 22, 1, HEX(0x6B6B6B), solid(HEX(0xFFFFFF)));
    landscape(cv, 4.6f, 6.6f, 22.8f, 17.8f);
}

static void badge(cv_t *cv, uint32_t light, uint32_t dark, uint32_t edge)
{
    ocircle(cv, 16, 16, 14, edge, lin(10, 3, 22, 29, light, dark));
    ellipse(cv, 16, 9.5f, 9.5f, 6, vlin(3, 15, RGBA(255, 255, 255, 150), RGBA(255, 255, 255, 0)));
}

static void draw_help(cv_t *cv)
{
    badge(cv, HEX(0x7FBFFF), HEX(0x1A58D0), HEX(0x1747A6));
    glyph_text(cv, "?", HEX(0xFFFFFF));
}

static void draw_info(cv_t *cv)
{
    badge(cv, HEX(0x7FBFFF), HEX(0x1A58D0), HEX(0x1747A6));
    rrect(cv, 14.4f, 13.5f, 3.2f, 10.5f, 0.8f, solid(HEX(0xFFFFFF)));
    circle(cv, 16, 9.5f, 2, solid(HEX(0xFFFFFF)));
}

static void draw_error(cv_t *cv)
{
    badge(cv, HEX(0xFF8A70), HEX(0xD02A0E), HEX(0x9E1C06));
    line(cv, 11, 11, 21, 21, 3, solid(HEX(0xFFFFFF)));
    line(cv, 21, 11, 11, 21, 3, solid(HEX(0xFFFFFF)));
}

static void draw_warning(cv_t *cv)
{
    const float tri[] = { 16, 2.5f, 30.5f, 28.5f, 1.5f, 28.5f };
    poly(cv, tri, 3, solid(HEX(0xA67C00)));
    const float in[] = { 16, 2.5f + PX * 2.2f, 30.5f - PX * 1.8f, 28.5f - PX, 1.5f + PX * 1.8f, 28.5f - PX };
    poly(cv, in, 3, vlin(3, 29, HEX(0xFFE77A), HEX(0xF2B705)));
    rrect(cv, 14.6f, 10, 2.8f, 11, 1.2f, solid(HEX(0x1A1A1A)));
    circle(cv, 16, 24.4f, 1.7f, solid(HEX(0x1A1A1A)));
}

static void draw_display(cv_t *cv)
{
    shadow(cv, 16, 29.6f, 11);
    monitor(cv, 2, 2.5f, 28, 21);
    static const uint32_t sw[] = { 0xE53935, 0xFDD835, 0x43A047, 0x1E88E5 };
    rrect(cv, 5.4f, 5.4f, 21.2f, 13.2f, 0.5f, solid(HEX(0xFFFFFF)));
    for (int i = 0; i < 4; i++)
        rrect(cv, 6.6f + (i % 2) * 10, 6.5f + (i / 2) * 6, 8.8f, 5, 0.6f, solid(HEX(sw[i])));
}

static void draw_run(cv_t *cv)
{
    shadow(cv, 16, 29.6f, 12);
    window_frame(cv, 6, 3, 23, 19, HEX(0xFFFFFF));
    line(cv, 9, 12, 25, 12, 0.8f, solid(HEX(0xB8C4D2)));
    line(cv, 9, 15, 22, 15, 0.8f, solid(HEX(0xB8C4D2)));
    /* green arrow coming in from the lower left */
    line(cv, 3, 27, 12, 18, 3, solid(HEX(0x1E7A1E)));
    line(cv, 3.4f, 26.6f, 11.6f, 18.4f, 1.8f, solid(HEX(0x5FD35F)));
    const float head[] = { 15.5f, 14.5f, 15.5f, 23, 7, 14.5f };
    poly(cv, head, 3, solid(HEX(0x1E7A1E)));
    const float head2[] = { 14.4f, 16, 14.4f, 21, 9.4f, 16 };
    poly(cv, head2, 3, solid(HEX(0x5FD35F)));
}

static void tile(cv_t *cv, uint32_t light, uint32_t dark, uint32_t edge)
{
    orrect(cv, 1, 1, 30, 30, 5, edge, lin(4, 2, 26, 30, light, dark));
    rrect(cv, 2.5f, 2.5f, 27, 12, 4, vlin(2, 15, RGBA(255, 255, 255, 120), RGBA(255, 255, 255, 0)));
}

static void power_glyph(cv_t *cv, float cx, float cy, float r, float w)
{
    ring(cv, cx, cy, r, w, -125, -55, solid(HEX(0xFFFFFF)));
    line(cv, cx, cy - r - 0.6f, cx, cy - 1, w, solid(HEX(0xFFFFFF)));
}

static void draw_logoff(cv_t *cv)
{
    tile(cv, HEX(0xFFD86A), HEX(0xE07F00), HEX(0xA85E00));
    ring(cv, 11, 16, 5.2f, 2.4f, 0, 0, solid(HEX(0xFFFFFF)));
    line(cv, 15.6f, 16, 26, 16, 2.6f, solid(HEX(0xFFFFFF)));
    line(cv, 22, 16, 22, 20.5f, 2.2f, solid(HEX(0xFFFFFF)));
    line(cv, 25.4f, 16, 25.4f, 19.5f, 2.2f, solid(HEX(0xFFFFFF)));
}

static void draw_power(cv_t *cv)
{
    tile(cv, HEX(0xFF9474), HEX(0xC92D0D), HEX(0x8F1E05));
    power_glyph(cv, 16, 17, 8.4f, 2.8f);
}

static void draw_standby(cv_t *cv)
{
    tile(cv, HEX(0xFFE16E), HEX(0xE09A00), HEX(0xA06A00));
    power_glyph(cv, 16, 17, 8.4f, 2.8f);
}

static void draw_restart(cv_t *cv)
{
    tile(cv, HEX(0x8EE36A), HEX(0x2C9A20), HEX(0x1C6A12));
    ring(cv, 16, 16.5f, 8.4f, 2.8f, -80, -15, solid(HEX(0xFFFFFF)));
    const float head[] = { 19.2f, 4.6f, 25.6f, 9.6f, 18.2f, 12.6f };
    poly(cv, head, 3, solid(HEX(0xFFFFFF)));
}

static void person(cv_t *cv, float cx, float cy, float s, uint32_t c)
{
    circle(cv, cx, cy - 4.2f * s, 3.2f * s, solid(c));
    rrect(cv, cx - 5 * s, cy, 10 * s, 8 * s, 4.5f * s, solid(c));
}

static void draw_switchuser(cv_t *cv)
{
    tile(cv, HEX(0x8EE36A), HEX(0x2C9A20), HEX(0x1C6A12));
    person(cv, 12, 14, 0.9f, RGBA(255, 255, 255, 180));
    person(cv, 19, 17, 1.0f, HEX(0xFFFFFF));
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

static void arrow_badge(cv_t *cv, int dir)
{
    ocircle(cv, 16, 16, 13.5f, HEX(0x1C6E1C), lin(10, 3, 22, 29, HEX(0x8BE36B), HEX(0x2A9128)));
    ellipse(cv, 16, 10, 9, 5.5f, vlin(3, 15, RGBA(255, 255, 255, 140), RGBA(255, 255, 255, 0)));
    float s = dir > 0 ? 1 : -1;
    line(cv, 16 - 6 * s, 16, 16 + 3 * s, 16, 3.4f, solid(HEX(0xFFFFFF)));
    const float head[] = { 16 + 8.5f * s, 16, 16 + 1.5f * s, 9, 16 + 1.5f * s, 23 };
    poly(cv, head, 3, solid(HEX(0xFFFFFF)));
}

static void draw_allprogs(cv_t *cv) { arrow_badge(cv, 1); }
static void draw_back(cv_t *cv) { arrow_badge(cv, -1); }
static void draw_forward(cv_t *cv) { arrow_badge(cv, 1); }

static void draw_up(cv_t *cv)
{
    draw_folder(cv);
    line(cv, 16, 26, 16, 14, 3.6f, solid(HEX(0x1C6E1C)));
    line(cv, 16, 25.4f, 16, 14.6f, 2, solid(HEX(0x5FD35F)));
    const float head[] = { 16, 7.5f, 23, 15.5f, 9, 15.5f };
    poly(cv, head, 3, solid(HEX(0x1C6E1C)));
    const float head2[] = { 16, 9.6f, 20.6f, 14.6f, 11.4f, 14.6f };
    poly(cv, head2, 3, solid(HEX(0x5FD35F)));
}

static void draw_textfile(cv_t *cv)
{
    shadow(cv, 16, 29.8f, 10);
    const float page[] = { 6.5f, 2.5f, 20.5f, 2.5f, 26.5f, 8.5f, 26.5f, 29, 6.5f, 29 };
    poly(cv, page, 5, solid(HEX(0x7C8794)));
    const float inner[] = { 6.5f + PX, 2.5f + PX, 20.5f - PX * 0.4f, 2.5f + PX, 26.5f - PX, 8.5f + PX * 0.4f,
                            26.5f - PX, 29 - PX, 6.5f + PX, 29 - PX };
    poly(cv, inner, 5, vlin(2, 29, HEX(0xFFFFFF), HEX(0xEEF2F6)));
    const float fold[] = { 20.5f, 2.5f, 20.5f, 8.5f, 26.5f, 8.5f };
    poly(cv, fold, 3, vlin(2, 9, HEX(0xF4F6F8), HEX(0xC5CDD6)));
    for (int i = 0; i < 6; i++)
        line(cv, 9.5f, 12 + i * 2.8f, i == 5 ? 18 : 23.5f, 12 + i * 2.8f, 0.8f, solid(HEX(0x98A4B2)));
}

static void drive_box(cv_t *cv, float y)
{
    shadow(cv, 16, y + 13.5f, 13);
    orrect(cv, 2.5f, y, 27, 12, 1.6f, HEX(0x56606B), vlin(y, y + 12, HEX(0xF7F9FB), HEX(0xAAB3BD)));
    rrect(cv, 2.5f + PX, y + 7.6f, 27 - 2 * PX, 4.4f - PX, 1, vlin(y + 7, y + 12, HEX(0x9AA4AF), HEX(0x6F7A86)));
    circle(cv, 25.2f, y + 9.8f, 1, solid(HEX(0x37E037)));
}

static void draw_drive(cv_t *cv)
{
    drive_box(cv, 12);
    line(cv, 5, 14.5f, 18, 14.5f, 0.8f, solid(RGBA(255, 255, 255, 200)));
}

static void draw_cdrom(cv_t *cv)
{
    circle(cv, 16, 11.5f, 9.5f, lin(8, 3, 24, 20, HEX(0xF2F6FB), HEX(0xA8B7CB)));
    ring(cv, 16, 11.5f, 7.6f, 1.2f, 0, 0, solid(RGBA(140, 120, 255, 70)));
    ring(cv, 16, 11.5f, 5.2f, 1.0f, 0, 0, solid(RGBA(90, 200, 255, 70)));
    circle(cv, 16, 11.5f, 2.2f, erase());
    drive_box(cv, 16);
}

static void draw_speaker(cv_t *cv)
{
    const float body[] = { 3, 12, 9, 12, 16, 5, 16, 27, 9, 20, 3, 20 };
    poly(cv, body, 6, solid(HEX(0x4A5562)));
    const float in[] = { 3 + PX, 12 + PX, 9 + PX * 0.5f, 12 + PX, 16 - PX, 5 + PX * 2, 16 - PX, 27 - PX * 2,
                         9 + PX * 0.5f, 20 - PX, 3 + PX, 20 - PX };
    poly(cv, in, 6, lin(3, 0, 16, 0, HEX(0xFFFFFF), HEX(0xC9D2DC)));
    ring(cv, 16, 16, 7, 2, 60, 300, solid(HEX(0xFFFFFF)));
    ring(cv, 16, 16, 12, 2, 55, 305, solid(HEX(0xFFFFFF)));
}

static void draw_network(cv_t *cv)
{
    orrect(cv, 2, 4, 17, 13, 1.5f, HEX(0x2D3A4A), vlin(4, 17, HEX(0xF5F7FA), HEX(0xB8C1CB)));
    rrect(cv, 4.5f, 6.5f, 12, 7.5f, 0.5f, vlin(6, 14, HEX(0x8AD0FF), HEX(0x2A6FDA)));
    orrect(cv, 13, 13, 17, 13, 1.5f, HEX(0x2D3A4A), vlin(13, 26, HEX(0xF5F7FA), HEX(0xB8C1CB)));
    rrect(cv, 15.5f, 15.5f, 12, 7.5f, 0.5f, vlin(15, 23, HEX(0x8AD0FF), HEX(0x2A6FDA)));
    rrect(cv, 18, 26, 7, 3, 0.5f, solid(HEX(0x5A6572)));
}

static void draw_showdesktop(cv_t *cv)
{
    orrect(cv, 2, 5, 28, 21, 1.5f, HEX(0x3D4A5A), solid(HEX(0xFFFFFF)));
    landscape(cv, 3.5f, 6.5f, 25, 18);
    line(cv, 21, 27, 29, 19, 2.4f, solid(HEX(0xF1C232)));
    line(cv, 20, 28, 21.4f, 26.6f, 2.4f, solid(HEX(0x333333)));
}

static void draw_sysinfo(cv_t *cv)
{
    draw_computer(cv);
    ocircle(cv, 24, 23, 7, HEX(0x1747A6), lin(18, 16, 30, 30, HEX(0x7FBFFF), HEX(0x1A58D0)));
    rrect(cv, 23, 22, 2.2f, 5.5f, 0.6f, solid(HEX(0xFFFFFF)));
    circle(cv, 24.1f, 19.6f, 1.3f, solid(HEX(0xFFFFFF)));
}

typedef void (*icon_fn)(cv_t *cv);

static const icon_fn icon_fns[ICON_COUNT] = {
    [ICON_COMPUTER] = draw_computer,
    [ICON_DOCUMENTS] = draw_documents,
    [ICON_RECYCLE] = draw_recycle,
    [ICON_CMD] = draw_cmd,
    [ICON_NOTEPAD] = draw_notepad,
    [ICON_CALC] = draw_calc,
    [ICON_PAINT] = draw_paint,
    [ICON_MINES] = draw_mines,
    [ICON_TASKMGR] = draw_taskmgr,
    [ICON_PICTURES] = draw_pictures,
    [ICON_HELP] = draw_help,
    [ICON_DISPLAY] = draw_display,
    [ICON_RUN] = draw_run,
    [ICON_LOGOFF] = draw_logoff,
    [ICON_POWER] = draw_power,
    [ICON_FOLDER] = draw_folder,
    [ICON_TEXTFILE] = draw_textfile,
    [ICON_DRIVE] = draw_drive,
    [ICON_CDROM] = draw_cdrom,
    [ICON_KEPLER] = draw_kepler,
    [ICON_ALLPROGS] = draw_allprogs,
    [ICON_STANDBY] = draw_standby,
    [ICON_RESTART] = draw_restart,
    [ICON_SWITCHUSER] = draw_switchuser,
    [ICON_SPEAKER] = draw_speaker,
    [ICON_NETWORK] = draw_network,
    [ICON_SHOWDESKTOP] = draw_showdesktop,
    [ICON_INFO] = draw_info,
    [ICON_WARNING] = draw_warning,
    [ICON_ERROR] = draw_error,
    [ICON_BACK] = draw_back,
    [ICON_FORWARD] = draw_forward,
    [ICON_UP] = draw_up,
    [ICON_SYSINFO] = draw_sysinfo,
};

/* ---- cache ------------------------------------------------------------------------- */

static const int sizes[] = { 16, 24, 32, 48, 64 };
static image_t *cache[ICON_COUNT][5];

static int size_slot(int size)
{
    for (int i = 0; i < ARRAY_LEN(sizes); i++)
        if (sizes[i] == size)
            return i;
    return -1;
}

static image_t *render(int id, int size)
{
    cv_t cv = { 0 };
    cv.img = image_new(size, size);
    cv.g = gfx_for_image(cv.img);
    cv.size = size;
    if (icon_fns[id])
        icon_fns[id](&cv);
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
        { ICON_COMPUTER, 32 }, { ICON_DOCUMENTS, 32 }, { ICON_RECYCLE, 32 }, { ICON_CMD, 32 },
        { ICON_NOTEPAD, 32 }, { ICON_KEPLER, 16 }, { ICON_KEPLER, 32 }, { ICON_SPEAKER, 16 },
        { ICON_NETWORK, 16 }, { ICON_CMD, 16 }, { ICON_NOTEPAD, 16 }, { ICON_SHOWDESKTOP, 16 },
        { ICON_COMPUTER, 16 }, { ICON_KEPLER, 64 }, { ICON_POWER, 32 },
    };
    int n = ARRAY_LEN(common);
    for (int i = 0; i < n; i++) {
        icon_get(common[i].id, common[i].size);
        if (progress)
            progress(i + 1, n);
    }
}

/* ---- user picture ---------------------------------------------------------------- */

static image_t *user_pic[5];
static image_t *user_src;

void icons_set_user_picture(const image_t *wall)
{
    /* the green globe in the right half of the wallpaper */
    int cx = wall->w * 1022 / 1280, cy = wall->h * 300 / 800, r = wall->h * 230 / 800;
    int side = 2 * r;
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
        slot = 2;
    if (!user_pic[slot] && user_src) {
        image_t *img = image_resize(user_src, sizes[slot], sizes[slot]);
        /* round the corners */
        gfx_t g = gfx_for_image(img);
        image_t *mask = image_new(img->w, img->h);
        gfx_t mg = gfx_for_image(mask);
        gfx_round_rect(&mg, 0, 0, img->w, img->h, img->w / 8, HEX(0xFFFFFF), CORNER_ALL);
        for (int i = 0; i < img->w * img->h; i++)
            img->px[i] = (img->px[i] & 0xFFFFFF) | (mask->px[i] & 0xFF000000u);
        image_free(mask);
        (void)g;
        user_pic[slot] = img;
    }
    return user_pic[slot];
}

/* ---- cursors ------------------------------------------------------------------------ */

static const char *arrow_cursor[] = {
    "X",
    "XX",
    "X.X",
    "X..X",
    "X...X",
    "X....X",
    "X.....X",
    "X......X",
    "X.......X",
    "X........X",
    "X.........X",
    "X..........X",
    "X......XXXXX",
    "X...X..X",
    "X..XX..X",
    "X.X  X..X",
    "XX   X..X",
    "X     X..X",
    "      X..X",
    "       XX",
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

static void draw_pattern(gfx_t *g, const char **rows, int n, int x, int y)
{
    for (int j = 0; j < n; j++)
        for (int i = 0; rows[j][i]; i++) {
            char c = rows[j][i];
            if (c == 'X')
                gfx_pixel(g, x + i + 1, y + j + 1, RGBA(0, 0, 0, 60));
        }
    for (int j = 0; j < n; j++)
        for (int i = 0; rows[j][i]; i++) {
            char c = rows[j][i];
            if (c == 'X')
                gfx_pixel(g, x + i, y + j, HEX(0x000000));
            else if (c == '.')
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
    case CURSOR_ARROW: return rect_make(x, y, 14, 22);
    case CURSOR_HAND: return rect_make(x - 6, y, 20, 21);
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
        gfx_circle(g, x + 20, y + 14, 7, HEX(0x1E5BD8));
        gfx_circle(g, x + 21, y + 15, 5, HEX(0xFFFFFF));
        gfx_circle(g, x + 22, y + 16, 3, HEX(0x6CC04A));
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
