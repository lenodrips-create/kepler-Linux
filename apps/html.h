#pragma once

/* A small HTML engine for the Kepler browser: a forgiving parser that turns a
 * page into a flat list of styled items, and a layout pass that wraps them
 * into positioned fragments for a given width. No CSS or JavaScript. */

#include "ui.h"

enum { IT_TEXT, IT_BREAK, IT_BLOCK, IT_HR, IT_BULLET, IT_INPUT, IT_BUTTON, IT_IMAGE };

#define ST_BOLD   0x01
#define ST_ITALIC 0x02
#define ST_MONO   0x04
#define ST_PRE    0x08
#define ST_SMALL  0x10
#define ST_QUOTE  0x20

typedef struct {
    uint8_t kind;
    uint8_t style;
    uint8_t heading; /* 1..6, 0 for body text */
    uint8_t indent;  /* nesting of lists and quotes */
    int16_t link;    /* index into links, -1 if none */
    int16_t field;   /* index into fields for IT_INPUT / IT_BUTTON */
    int text, len;   /* into the text pool; IT_BULLET: the marker; IT_IMAGE: the alt text */
    int margin;      /* IT_BLOCK: space above, px */
} item_t;

enum { F_TEXT, F_HIDDEN, F_SUBMIT, F_PASSWORD };

typedef struct {
    int form;
    int type;
    char name[48];
    char value[256];
    int width;
} field_t;

typedef struct {
    char action[512];
} form_t;

typedef struct {
    item_t *items;
    int n_items, cap_items;
    char *pool;
    int pool_len, pool_cap;
    int *links; /* pool offsets of resolved URLs */
    int n_links, cap_links;
    field_t *fields;
    int n_fields, cap_fields;
    form_t *forms;
    int n_forms, cap_forms;
    char title[160];
    char url[1100];     /* the page's own address (after <base>) */
    char refresh[1100]; /* <meta http-equiv=refresh> target, if any */
} doc_t;

doc_t *html_parse(const char *src, int len, const char *url);
/* plain text, shown as preformatted */
doc_t *html_plain(const char *src, int len, const char *url);
void html_free(doc_t *d);
const char *html_link(const doc_t *d, int link);
/* the URL a form submits to, with its fields as a query string */
void html_form_url(const doc_t *d, int form, char *out, int n);

/* ---- layout ---- */

typedef struct {
    int x, y, w, h;
    int item;
    int start, len; /* text slice in the pool */
    const font_t *font;
    uint32_t color;
    int16_t link;
    uint8_t kind;
} frag_t;

typedef struct {
    frag_t *frags;
    int n, cap;
    int height;
    int width;
} layout_t;

void html_layout(const doc_t *d, layout_t *l, int width);
void html_layout_free(layout_t *l);
void html_paint(const doc_t *d, const layout_t *l, gfx_t *g, int scroll, int view_h, int hover_link, int focus_field,
                bool caret_on);
