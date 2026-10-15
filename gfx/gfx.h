#pragma once

#include "kernel.h"

#define RGB(r, g, b)     (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define HEX(v)           (0xFF000000u | (uint32_t)(v))
#define ALPHA(c)         ((c) >> 24)
#define WITH_ALPHA(c, a) (((c) & 0x00FFFFFFu) | ((uint32_t)(a) << 24))

typedef struct {
    int x, y, w, h;
} rect_t;

/* an ARGB image; stride equals width */
typedef struct {
    uint32_t *px;
    int w, h;
} image_t;

/* a drawing context: target pixels, translation and an absolute clip */
typedef struct {
    uint32_t *px;
    int w, h, stride;
    int ox, oy;
    rect_t clip;
    bool argb; /* target keeps an alpha channel (icons, sprites) */
} gfx_t;

static inline rect_t rect_make(int x, int y, int w, int h) { return (rect_t){ x, y, w, h }; }
rect_t rect_intersect(rect_t a, rect_t b);
rect_t rect_union(rect_t a, rect_t b);
bool rect_empty(rect_t r);
bool rect_contains(rect_t r, int x, int y);

uint32_t color_lerp(uint32_t a, uint32_t b, int t); /* t = 0..256 */
uint32_t color_shade(uint32_t c, int pct);          /* pct < 100 darkens, > 100 lightens */

image_t *image_new(int w, int h);
void image_free(image_t *img);
gfx_t gfx_for_image(image_t *img);
gfx_t gfx_for_buffer(uint32_t *px, int w, int h, int stride);
gfx_t gfx_sub(const gfx_t *g, int x, int y, int w, int h);

void gfx_pixel(gfx_t *g, int x, int y, uint32_t c);
void gfx_fill(gfx_t *g, int x, int y, int w, int h, uint32_t c);
void gfx_clear(gfx_t *g, uint32_t c);
void gfx_hline(gfx_t *g, int x, int y, int w, uint32_t c);
void gfx_vline(gfx_t *g, int x, int y, int h, uint32_t c);
void gfx_rect(gfx_t *g, int x, int y, int w, int h, uint32_t c);
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, uint32_t c);
void gfx_thick_line(gfx_t *g, int x0, int y0, int x1, int y1, int r, uint32_t c);
void gfx_vgrad(gfx_t *g, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_hgrad(gfx_t *g, int x, int y, int w, int h, uint32_t left, uint32_t right);
void gfx_vgrad_stops(gfx_t *g, int x, int y, int w, int h, const uint32_t *colors, const int *stops, int n);

/* rounded rectangles; corners: bit0 TL, bit1 TR, bit2 BL, bit3 BR */
#define CORNER_ALL 15
#define CORNER_TOP 3
#define CORNER_BOTTOM 12
void gfx_round_rect(gfx_t *g, int x, int y, int w, int h, int r, uint32_t c, int corners);
void gfx_round_rect_vgrad(gfx_t *g, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom, int corners);
void gfx_round_rect_stops(gfx_t *g, int x, int y, int w, int h, int r, const uint32_t *colors, const int *stops, int n,
                          int corners);
uint32_t gradient_at(const uint32_t *colors, const int *stops, int n, int pos);
void gfx_round_frame(gfx_t *g, int x, int y, int w, int h, int r, uint32_t c, int corners);

void gfx_circle(gfx_t *g, int cx, int cy, int r, uint32_t c);

void gfx_blit(gfx_t *g, int x, int y, const image_t *img);
void gfx_blit_alpha(gfx_t *g, int x, int y, const image_t *img, int opacity);
void gfx_blit_region(gfx_t *g, int x, int y, const image_t *img, int sx, int sy, int w, int h);
void gfx_blit_scaled(gfx_t *g, int x, int y, int w, int h, const image_t *img);
void gfx_tint(gfx_t *g, int x, int y, int w, int h, uint32_t c, int a);
void gfx_desaturate(gfx_t *g, int x, int y, int w, int h, int amount, int darken);

/* image processing used for the wallpaper */
image_t *image_resize(const image_t *src, int w, int h);
image_t *image_cover(const image_t *src, int w, int h);
void image_sharpen(image_t *img, int amount);
