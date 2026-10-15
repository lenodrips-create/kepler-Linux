/* Text drawing with the pre-rendered antialiased fonts. */

#include "font.h"

static const glyph_t *glyph(const font_t *f, char c)
{
    unsigned char u = (unsigned char)c;
    if (u < 32 || u > 126)
        u = '?';
    const glyph_t *g = &f->glyphs[u - 32];
    if (!g->adv && u != ' ')
        g = &f->glyphs['?' - 32];
    return g;
}

int char_width(const font_t *f, char c) { return glyph(f, c)->adv; }

int text_width_n(const font_t *f, const char *s, int n)
{
    int w = 0;
    for (int i = 0; i < n && s[i]; i++)
        w += glyph(f, s[i])->adv;
    return w;
}

int text_width(const font_t *f, const char *s) { return text_width_n(f, s, 0x7FFFFFFF); }

static void draw_glyph(gfx_t *g, const font_t *f, const glyph_t *gl, int x, int y, uint32_t c)
{
    int gx = x + gl->xoff + g->ox, gy = y + gl->yoff + g->oy;
    rect_t r = rect_intersect(g->clip, rect_make(gx, gy, gl->w, gl->h));
    if (rect_empty(r))
        return;
    uint32_t ca = ALPHA(c);
    const uint8_t *bits = f->bits + gl->off;
    for (int j = r.y; j < r.y + r.h; j++) {
        const uint8_t *src = bits + (j - gy) * gl->w;
        uint32_t *dst = &g->px[j * g->stride];
        for (int i = r.x; i < r.x + r.w; i++) {
            uint32_t cov = src[i - gx];
            if (!cov)
                continue;
            uint32_t a = (cov * ca + 127) / 255;
            uint32_t d = dst[i];
            if (g->argb) {
                /* rare: text into a transparent image */
                gfx_t tmp = gfx_for_buffer(&dst[i], 1, 1, 1);
                tmp.argb = true;
                gfx_pixel(&tmp, 0, 0, WITH_ALPHA(c, a));
                continue;
            }
            uint32_t ia = 255 - a;
            uint32_t rb = (c & 0xFF00FF) * a + (d & 0xFF00FF) * ia + 0x800080;
            rb = ((rb + ((rb >> 8) & 0xFF00FF)) >> 8) & 0xFF00FF;
            uint32_t gg = (c & 0x00FF00) * a + (d & 0x00FF00) * ia + 0x008000;
            gg = ((gg + ((gg >> 8) & 0x00FF00)) >> 8) & 0x00FF00;
            dst[i] = 0xFF000000u | rb | gg;
        }
    }
}

int gfx_text_n(gfx_t *g, const font_t *f, int x, int y, const char *s, int n, uint32_t c)
{
    for (int i = 0; i < n && s[i]; i++) {
        const glyph_t *gl = glyph(f, s[i]);
        if (gl->w)
            draw_glyph(g, f, gl, x, y, c);
        x += gl->adv;
    }
    return x;
}

int gfx_text(gfx_t *g, const font_t *f, int x, int y, const char *s, uint32_t c)
{
    return gfx_text_n(g, f, x, y, s, 0x7FFFFFFF, c);
}

void gfx_text_shadow(gfx_t *g, const font_t *f, int x, int y, const char *s, uint32_t c, uint32_t shadow)
{
    gfx_text(g, f, x + 1, y + 1, s, shadow);
    gfx_text(g, f, x, y, s, c);
}

void gfx_text_centered(gfx_t *g, const font_t *f, int x, int y, int w, const char *s, uint32_t c)
{
    gfx_text(g, f, x + (w - text_width(f, s)) / 2, y, s, c);
}

void gfx_text_clipped(gfx_t *g, const font_t *f, int x, int y, int max_w, const char *s, uint32_t c)
{
    if (text_width(f, s) <= max_w) {
        gfx_text(g, f, x, y, s, c);
        return;
    }
    int dots = text_width(f, "...");
    int n = 0, w = 0;
    while (s[n] && w + char_width(f, s[n]) + dots <= max_w)
        w += char_width(f, s[n++]);
    x = gfx_text_n(g, f, x, y, s, n, c);
    gfx_text(g, f, x, y, "...", c);
}

int gfx_text_wrapped(gfx_t *g, const font_t *f, int x, int y, int max_w, const char *s, uint32_t c, int line_h)
{
    int top = y;
    while (*s) {
        /* take as many words as fit */
        int n = 0, last_space = -1, w = 0;
        while (s[n] && s[n] != '\n') {
            int cw = char_width(f, s[n]);
            if (w + cw > max_w && n > 0)
                break;
            if (s[n] == ' ')
                last_space = n;
            w += cw;
            n++;
        }
        int take = n;
        if (s[n] && s[n] != '\n' && last_space > 0)
            take = last_space;
        if (g)
            gfx_text_n(g, f, x, y, s, take, c);
        y += line_h;
        s += take;
        while (*s == ' ')
            s++;
        if (*s == '\n')
            s++;
    }
    return y - top;
}
