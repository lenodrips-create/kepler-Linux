#pragma once

#include "gfx.h"

typedef struct {
    uint8_t w, h;
    int8_t xoff, yoff; /* from the pen position and the top of the line */
    uint8_t adv;
    uint32_t off; /* into the coverage bitmap */
} glyph_t;

typedef struct {
    int height, ascent;
    const glyph_t *glyphs; /* ASCII 32..126 */
    const uint8_t *bits;
} font_t;

extern const font_t font_ui, font_ui_bold, font_title, font_start, font_mono;
extern const font_t font_heading, font_big, font_big_bold, font_logo, font_logo_small;

int text_width(const font_t *f, const char *s);
int text_width_n(const font_t *f, const char *s, int n);
int char_width(const font_t *f, char c);

/* y is the top of the line box; the return value is the pen position afterwards */
int gfx_text(gfx_t *g, const font_t *f, int x, int y, const char *s, uint32_t c);
int gfx_text_n(gfx_t *g, const font_t *f, int x, int y, const char *s, int n, uint32_t c);
void gfx_text_shadow(gfx_t *g, const font_t *f, int x, int y, const char *s, uint32_t c, uint32_t shadow);
void gfx_text_centered(gfx_t *g, const font_t *f, int x, int y, int w, const char *s, uint32_t c);
/* draws s, cut with "..." so it fits in max_w */
void gfx_text_clipped(gfx_t *g, const font_t *f, int x, int y, int max_w, const char *s, uint32_t c);
/* word-wrapped paragraph; returns the height used */
int gfx_text_wrapped(gfx_t *g, const font_t *f, int x, int y, int max_w, const char *s, uint32_t c, int line_h);
