/* Paint: pencil, brush, airbrush, eraser, fill, colour picker, line and shape tools. */

#include "apps.h"

#define TOOLBOX_W 58
#define PALETTE_H 50
#define STATUS_H  22
#define CANVAS_X  (TOOLBOX_W + 6)
#define CANVAS_Y  (MENUBAR_H + 6)

enum { T_PENCIL, T_BRUSH, T_AIR, T_ERASER, T_FILL, T_PICK, T_LINE, T_RECT, T_ELLIPSE, T_RRECT, T_COUNT };

static const char *tool_names[T_COUNT] = { "Pencil", "Brush", "Airbrush", "Eraser", "Fill With Color", "Pick Color",
                                           "Line", "Rectangle", "Ellipse", "Rounded Rectangle" };

static const uint32_t palette[28] = {
    0x000000, 0x808080, 0x800000, 0x808000, 0x008000, 0x008080, 0x000080, 0x800080, 0x808040, 0x004040,
    0x0080FF, 0x004080, 0x8000FF, 0x804000, 0xFFFFFF, 0xC0C0C0, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF,
    0x0000FF, 0xFF00FF, 0xFFFF80, 0x00FF80, 0x80FFFF, 0x8080FF, 0xFF0080, 0xFF8040,
};

typedef struct {
    image_t *canvas;
    int tool, size;
    uint32_t fg, bg;
    bool drawing;
    int button;
    int sx, sy, lx, ly, cx, cy; /* start, last and current canvas points */
    int mouse_x, mouse_y;
    bool in_canvas;
    menubar_t mb;
} paint_t;

/* ---- geometry ------------------------------------------------------------------------------------------- */

static rect_t tool_rect(int i) { return rect_make(4 + (i % 2) * 25, MENUBAR_H + 6 + (i / 2) * 25, 25, 25); }
static rect_t options_rect(void) { return rect_make(7, MENUBAR_H + 6 + 5 * 25 + 8, 44, 66); }
static rect_t size_rect(int i) { return rect_make(9, MENUBAR_H + 6 + 5 * 25 + 12 + i * 20, 40, 18); }

static rect_t swatch_rect(window_t *w, int i)
{
    rect_t c = wm_client_rect(w);
    int y0 = c.h - STATUS_H - PALETTE_H + 9;
    return rect_make(46 + (i % 14) * 16, y0 + (i / 14) * 16, 16, 16);
}

static void canvas_origin(int *x, int *y)
{
    *x = CANVAS_X + 4;
    *y = CANVAS_Y + 4;
}

/* ---- drawing on the canvas -------------------------------------------------------------------------------- */

static gfx_t canvas_gfx(paint_t *p)
{
    gfx_t g = gfx_for_image(p->canvas);
    g.argb = false;
    return g;
}

static void stamp_line(paint_t *p, gfx_t *g, int x0, int y0, int x1, int y1, uint32_t c)
{
    switch (p->tool) {
    case T_PENCIL: gfx_line(g, x0, y0, x1, y1, c); break;
    case T_BRUSH: gfx_thick_line(g, x0, y0, x1, y1, p->size, c); break;
    case T_ERASER: {
        int r = 3 + p->size * 2;
        int dx = ABS(x1 - x0), dy = ABS(y1 - y0), steps = MAX(dx, dy);
        for (int i = 0; i <= steps; i++) {
            int x = x0 + (steps ? (x1 - x0) * i / steps : 0), y = y0 + (steps ? (y1 - y0) * i / steps : 0);
            gfx_fill(g, x - r, y - r, 2 * r, 2 * r, p->bg);
        }
        break;
    }
    case T_LINE: gfx_thick_line(g, x0, y0, x1, y1, p->size - 1, c); break;
    }
}

static void spray(paint_t *p, gfx_t *g, int x, int y, uint32_t c)
{
    int r = 4 + p->size * 4;
    for (int i = 0; i < 14; i++) {
        int dx = (int)(krand() % (2 * r + 1)) - r, dy = (int)(krand() % (2 * r + 1)) - r;
        if (dx * dx + dy * dy <= r * r)
            gfx_pixel(g, x + dx, y + dy, c);
    }
}

static void ellipse_outline(gfx_t *g, int x0, int y0, int x1, int y1, int t, uint32_t c)
{
    if (x0 > x1) {
        int s = x0;
        x0 = x1;
        x1 = s;
    }
    if (y0 > y1) {
        int s = y0;
        y0 = y1;
        y1 = s;
    }
    int cx2 = x0 + x1, cy2 = y0 + y1; /* doubled centre */
    int64_t a = x1 - x0, b = y1 - y0;
    if (a < 1 || b < 1) {
        gfx_line(g, x0, y0, x1, y1, c);
        return;
    }
    /* test every pixel in the box against the doubled-coordinate ellipse band */
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int64_t dx = 2 * x - cx2, dy = 2 * y - cy2;
            int64_t outer = dx * dx * b * b + dy * dy * a * a;
            int64_t ra = a - 2 * t, rb = b - 2 * t;
            bool in_outer = outer <= a * a * b * b;
            bool in_inner = ra > 0 && rb > 0 && dx * dx * rb * rb + dy * dy * ra * ra <= ra * ra * rb * rb;
            if (in_outer && !in_inner)
                gfx_pixel(g, x, y, c);
        }
}

static void draw_shape(paint_t *p, gfx_t *g, int x0, int y0, int x1, int y1, uint32_t c)
{
    int t = p->size;
    int lx = MIN(x0, x1), ly = MIN(y0, y1), w = ABS(x1 - x0) + 1, h = ABS(y1 - y0) + 1;
    switch (p->tool) {
    case T_LINE: gfx_thick_line(g, x0, y0, x1, y1, t - 1, c); break;
    case T_RECT:
        for (int i = 0; i < t && w - 2 * i > 0 && h - 2 * i > 0; i++)
            gfx_rect(g, lx + i, ly + i, w - 2 * i, h - 2 * i, c);
        break;
    case T_ELLIPSE: ellipse_outline(g, x0, y0, x1, y1, t, c); break;
    case T_RRECT:
        for (int i = 0; i < t && w - 2 * i > 2 && h - 2 * i > 2; i++)
            gfx_round_frame(g, lx + i, ly + i, w - 2 * i, h - 2 * i, MIN(12, MIN(w, h) / 3), c, CORNER_ALL);
        break;
    }
}

static void flood_fill(image_t *img, int x, int y, uint32_t c)
{
    uint32_t target = img->px[y * img->w + x];
    c |= 0xFF000000u;
    if (target == c)
        return;
    int cap = 4096, n = 0;
    int *stack = kmalloc(cap * sizeof(int) * 2);
    stack[n++] = x;
    stack[n++] = y;
    while (n) {
        int sy = stack[--n], sx = stack[--n];
        int lx = sx;
        while (lx > 0 && img->px[sy * img->w + lx - 1] == target)
            lx--;
        bool up = false, down = false;
        for (int i = lx; i < img->w && img->px[sy * img->w + i] == target; i++) {
            img->px[sy * img->w + i] = c;
            for (int d = -1; d <= 1; d += 2) {
                int ny = sy + d;
                if (ny < 0 || ny >= img->h)
                    continue;
                bool match = img->px[ny * img->w + i] == target;
                bool *flag = d < 0 ? &up : &down;
                if (match && !*flag) {
                    if (n + 2 > cap * 2) {
                        cap *= 2;
                        stack = krealloc(stack, cap * sizeof(int) * 2);
                    }
                    stack[n++] = i;
                    stack[n++] = ny;
                    *flag = true;
                } else if (!match) {
                    *flag = false;
                }
            }
        }
    }
    kfree(stack);
}

/* ---- painting ----------------------------------------------------------------------------------------------- */

static void tool_glyph(gfx_t *g, int t, int x, int y)
{
    uint32_t k = HEX(0x000000);
    switch (t) {
    case T_PENCIL:
        gfx_thick_line(g, x + 5, y + 18, x + 17, y + 6, 1, HEX(0xE8B848));
        gfx_line(g, x + 4, y + 19, x + 6, y + 17, k);
        gfx_thick_line(g, x + 16, y + 7, x + 18, y + 5, 1, HEX(0xE08080));
        break;
    case T_BRUSH:
        gfx_thick_line(g, x + 10, y + 13, x + 18, y + 5, 1, HEX(0x8B5A2B));
        gfx_fill(g, x + 6, y + 13, 5, 5, HEX(0x3050C0));
        break;
    case T_AIR:
        gfx_fill(g, x + 8, y + 9, 7, 10, HEX(0x707880));
        gfx_fill(g, x + 10, y + 6, 3, 3, HEX(0x404040));
        for (int i = 0; i < 6; i++)
            gfx_pixel(g, x + 15 + (i % 3) * 2, y + 4 + (i / 3) * 2, HEX(0x3050C0));
        break;
    case T_ERASER:
        gfx_fill(g, x + 6, y + 8, 12, 9, HEX(0xF2D2E0));
        gfx_rect(g, x + 6, y + 8, 12, 9, k);
        gfx_fill(g, x + 7, y + 14, 10, 2, HEX(0xD0A0B8));
        break;
    case T_FILL:
        gfx_fill(g, x + 7, y + 8, 9, 9, HEX(0xC8C8C8));
        gfx_rect(g, x + 7, y + 8, 9, 9, k);
        gfx_fill(g, x + 15, y + 9, 3, 8, HEX(0x3050C0));
        break;
    case T_PICK:
        gfx_thick_line(g, x + 7, y + 18, x + 15, y + 10, 1, HEX(0x707880));
        gfx_fill(g, x + 15, y + 5, 4, 5, k);
        break;
    case T_LINE: gfx_thick_line(g, x + 5, y + 18, x + 19, y + 6, 0, k); break;
    case T_RECT: gfx_rect(g, x + 5, y + 7, 15, 11, k); break;
    case T_ELLIPSE: ellipse_outline(g, x + 5, y + 7, x + 19, y + 18, 1, k); break;
    case T_RRECT: gfx_round_frame(g, x + 5, y + 7, 15, 12, 4, k, CORNER_ALL); break;
    }
}

static void pt_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    paint_t *p = w->data;
    menubar_paint(&p->mb, g, cw);

    /* tool box */
    gfx_fill(g, 0, MENUBAR_H, TOOLBOX_W, ch - MENUBAR_H - STATUS_H, COL_FACE);
    for (int i = 0; i < T_COUNT; i++) {
        rect_t r = tool_rect(i);
        bool sel = p->tool == i;
        if (sel) {
            gfx_fill(g, r.x, r.y, r.w, r.h, HEX(0xFFFFFF));
            draw_sunken(g, r.x, r.y, r.w, r.h);
        } else {
            draw_raised(g, r.x, r.y, r.w, r.h);
        }
        tool_glyph(g, i, r.x, r.y);
    }
    rect_t o = options_rect();
    gfx_fill(g, o.x, o.y, o.w, o.h, COL_FACE);
    draw_sunken(g, o.x, o.y, o.w, o.h);
    for (int i = 0; i < 3; i++) {
        rect_t r = size_rect(i);
        bool sel = p->size == i + 1;
        if (sel)
            gfx_fill(g, r.x, r.y, r.w, r.h, COL_SELECT);
        int th = 1 + i * 2;
        gfx_fill(g, r.x + 6, r.y + r.h / 2 - th / 2, r.w - 12, th, sel ? HEX(0xFFFFFF) : HEX(0x000000));
    }

    /* workspace and canvas */
    int wx = TOOLBOX_W, wy = MENUBAR_H, ww = cw - TOOLBOX_W, wh = ch - MENUBAR_H - PALETTE_H - STATUS_H;
    gfx_t ws = gfx_sub(g, wx, wy, ww, wh);
    gfx_fill(&ws, 0, 0, ww, wh, HEX(0x808080));
    int ox, oy;
    canvas_origin(&ox, &oy);
    gfx_t cg = gfx_sub(g, ox, oy, MIN(p->canvas->w, cw - ox), MIN(p->canvas->h, wh - (oy - wy)));
    gfx_blit(&cg, 0, 0, p->canvas);
    if (p->drawing && (p->tool == T_LINE || p->tool == T_RECT || p->tool == T_ELLIPSE || p->tool == T_RRECT))
        draw_shape(p, &cg, p->sx, p->sy, p->cx, p->cy, p->button == MOUSE_RIGHT ? p->bg : p->fg);
    /* resize handles */
    int hx = ox + p->canvas->w, hy = oy + p->canvas->h;
    gfx_t hg = gfx_sub(g, wx, wy, ww, wh);
    gfx_fill(&hg, hx - wx, hy - wy, 3, 3, HEX(0x0A246A));
    gfx_fill(&hg, hx - wx, (oy + hy) / 2 - wy, 3, 3, HEX(0x0A246A));
    gfx_fill(&hg, (ox + hx) / 2 - wx, hy - wy, 3, 3, HEX(0x0A246A));

    /* palette */
    int py = ch - STATUS_H - PALETTE_H;
    gfx_fill(g, 0, py, cw, PALETTE_H, COL_FACE);
    gfx_hline(g, 0, py, cw, HEX(0xFFFFFF));
    gfx_fill(g, 6, py + 7, 32, 34, HEX(0xFFFFFF));
    draw_sunken(g, 6, py + 7, 32, 34);
    gfx_fill(g, 19, py + 21, 14, 14, p->bg);
    draw_raised(g, 18, py + 20, 16, 16);
    gfx_fill(g, 11, py + 12, 14, 14, p->fg);
    draw_raised(g, 10, py + 11, 16, 16);
    for (int i = 0; i < 28; i++) {
        rect_t r = swatch_rect(w, i);
        gfx_fill(g, r.x + 1, r.y + 1, r.w - 2, r.h - 2, HEX(palette[i]));
        draw_sunken(g, r.x, r.y, r.w, r.h);
    }

    /* status bar */
    char status[80];
    if (p->in_canvas)
        ksnprintf(status, sizeof status, "%s", tool_names[p->tool]);
    else
        strlcpy(status, "For Help, click Help Topics on the Help Menu.", sizeof status);
    draw_statusbar(g, ch - STATUS_H, cw, status);
    if (p->in_canvas) {
        char pos[32];
        ksnprintf(pos, sizeof pos, "%d,%d", p->mouse_x, p->mouse_y);
        gfx_vline(g, cw - 170, ch - STATUS_H + 3, STATUS_H - 5, HEX(0xD8D2BD));
        gfx_text(g, &font_ui, cw - 160, ch - STATUS_H + 5, pos, HEX(0x000000));
    }
}

/* ---- menus ---------------------------------------------------------------------------------------------- */

enum { PM_NEW = 1, PM_EXIT, PM_CLEAR, PM_INVERT, PM_FLIPH, PM_FLIPV, PM_ABOUT };

static const menu_item_t file_menu[] = {
    { "New", PM_NEW, 0, 0, NULL, 0, "Ctrl+N" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Exit", PM_EXIT, 0, 0, NULL, 0, "Alt+F4" },
};
static const menu_item_t edit_menu[] = {
    { "Undo", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+Z" },
    { "Clear Image", PM_CLEAR, 0, 0, NULL, 0, "Ctrl+Shft+N" },
};
static const menu_item_t view_menu[] = {
    { "Tool Box", 0, MI_CHECK, 0, NULL, 0, "Ctrl+T" },
    { "Color Box", 0, MI_CHECK, 0, NULL, 0, "Ctrl+L" },
    { "Status Bar", 0, MI_CHECK, 0, NULL, 0, NULL },
};
static const menu_item_t image_menu[] = {
    { "Flip Horizontal", PM_FLIPH, 0, 0, NULL, 0, NULL },
    { "Flip Vertical", PM_FLIPV, 0, 0, NULL, 0, NULL },
    { "Invert Colors", PM_INVERT, 0, 0, NULL, 0, "Ctrl+I" },
    { "Clear Image", PM_CLEAR, 0, 0, NULL, 0, NULL },
};
static const menu_item_t colors_menu[] = {
    { "Edit Colors...", 0, MI_DISABLED, 0, NULL, 0, NULL },
};
static const menu_item_t help_menu[] = {
    { "About Paint", PM_ABOUT, 0, 0, NULL, 0, NULL },
};
static const menubar_entry_t bar[] = {
    { "File", file_menu, ARRAY_LEN(file_menu) },     { "Edit", edit_menu, ARRAY_LEN(edit_menu) },
    { "View", view_menu, ARRAY_LEN(view_menu) },     { "Image", image_menu, ARRAY_LEN(image_menu) },
    { "Colors", colors_menu, ARRAY_LEN(colors_menu) }, { "Help", help_menu, ARRAY_LEN(help_menu) },
};

static void command(int id, void *ctx)
{
    window_t *w = ctx;
    if (!wm_alive(w))
        return;
    paint_t *p = w->data;
    image_t *c = p->canvas;
    switch (id) {
    case PM_NEW:
    case PM_CLEAR: memset32(c->px, p->bg | 0xFF000000u, (size_t)c->w * c->h); break;
    case PM_EXIT: wm_close(w); return;
    case PM_INVERT:
        for (int i = 0; i < c->w * c->h; i++)
            c->px[i] ^= 0x00FFFFFF;
        break;
    case PM_FLIPH:
        for (int y = 0; y < c->h; y++)
            for (int x = 0; x < c->w / 2; x++) {
                uint32_t t = c->px[y * c->w + x];
                c->px[y * c->w + x] = c->px[y * c->w + c->w - 1 - x];
                c->px[y * c->w + c->w - 1 - x] = t;
            }
        break;
    case PM_FLIPV:
        for (int y = 0; y < c->h / 2; y++)
            for (int x = 0; x < c->w; x++) {
                uint32_t t = c->px[y * c->w + x];
                c->px[y * c->w + x] = c->px[(c->h - 1 - y) * c->w + x];
                c->px[(c->h - 1 - y) * c->w + x] = t;
            }
        break;
    case PM_ABOUT: msgbox(w, "About Paint", "KeplerOS Paint\nVersion " KEPLER_VERSION, ICON_PAINT); return;
    }
    wm_invalidate_client(w);
}

/* ---- input ----------------------------------------------------------------------------------------------- */

static void pt_mouse(window_t *w, int kind, int x, int y, int button)
{
    paint_t *p = w->data;
    if (!p->drawing && menubar_mouse(&p->mb, w, kind, x, y, command, w))
        return;
    int ox, oy;
    canvas_origin(&ox, &oy);
    int px = x - ox, py = y - oy;
    bool inside = px >= 0 && py >= 0 && px < p->canvas->w && py < p->canvas->h;
    bool was = p->in_canvas;
    p->in_canvas = inside || p->drawing;
    p->mouse_x = px;
    p->mouse_y = py;
    w->cursor = inside ? CURSOR_IBEAM : CURSOR_ARROW;
    if (inside && (p->tool == T_FILL || p->tool == T_PICK))
        w->cursor = CURSOR_HAND;
    rect_t c = wm_client_rect(w);
    if (kind == MS_MOVE && (was != p->in_canvas || p->in_canvas))
        wm_invalidate_client_rect(w, 0, c.h - STATUS_H, c.w, STATUS_H);

    if (kind == MS_DOWN && !p->drawing) {
        for (int i = 0; i < T_COUNT; i++)
            if (rect_contains(tool_rect(i), x, y)) {
                p->tool = i;
                wm_invalidate_client(w);
                return;
            }
        for (int i = 0; i < 3; i++)
            if (rect_contains(size_rect(i), x, y)) {
                p->size = i + 1;
                wm_invalidate_client(w);
                return;
            }
        for (int i = 0; i < 28; i++)
            if (rect_contains(swatch_rect(w, i), x, y)) {
                if (button == MOUSE_RIGHT)
                    p->bg = HEX(palette[i]);
                else
                    p->fg = HEX(palette[i]);
                wm_invalidate_client(w);
                return;
            }
        if (!inside)
            return;
        uint32_t col = button == MOUSE_RIGHT ? p->bg : p->fg;
        gfx_t g = canvas_gfx(p);
        if (p->tool == T_FILL) {
            flood_fill(p->canvas, px, py, col);
        } else if (p->tool == T_PICK) {
            uint32_t picked = p->canvas->px[py * p->canvas->w + px];
            if (button == MOUSE_RIGHT)
                p->bg = picked;
            else
                p->fg = picked;
        } else {
            p->drawing = true;
            p->button = button;
            p->sx = p->lx = p->cx = px;
            p->sy = p->ly = p->cy = py;
            if (p->tool == T_AIR)
                spray(p, &g, px, py, col);
            else if (p->tool <= T_ERASER)
                stamp_line(p, &g, px, py, px, py, col);
        }
        wm_invalidate_client(w);
        return;
    }
    if (!p->drawing)
        return;
    uint32_t col = p->button == MOUSE_RIGHT ? p->bg : p->fg;
    gfx_t g = canvas_gfx(p);
    if (kind == MS_MOVE) {
        p->cx = px;
        p->cy = py;
        if (p->tool <= T_ERASER && p->tool != T_AIR) {
            stamp_line(p, &g, p->lx, p->ly, px, py, col);
        } else if (p->tool == T_AIR) {
            spray(p, &g, px, py, col);
        }
        p->lx = px;
        p->ly = py;
        wm_invalidate_client(w);
    } else if (kind == MS_UP) {
        if (p->tool >= T_LINE)
            draw_shape(p, &g, p->sx, p->sy, px, py, col);
        p->drawing = false;
        wm_invalidate_client(w);
    }
}

static void pt_tick(window_t *w)
{
    paint_t *p = w->data;
    if (p->drawing && p->tool == T_AIR) {
        gfx_t g = canvas_gfx(p);
        spray(p, &g, p->cx, p->cy, p->button == MOUSE_RIGHT ? p->bg : p->fg);
        wm_invalidate_client(w);
    }
}

static void pt_key(window_t *w, const input_event_t *ev)
{
    if (!ev->pressed || !(ev->mods & MOD_CTRL))
        return;
    if (ktolower(ev->key) == 'n')
        command(PM_NEW, w);
    else if (ktolower(ev->key) == 'i')
        command(PM_INVERT, w);
}

static void pt_close(window_t *w)
{
    paint_t *p = w->data;
    image_free(p->canvas);
    kfree(p);
}

static const window_ops_t pt_ops = { pt_paint, pt_key, pt_mouse, pt_tick, pt_close, NULL };

window_t *paint_open(void)
{
    paint_t *p = kzalloc(sizeof *p);
    p->canvas = image_new(560, 360);
    memset32(p->canvas->px, 0xFFFFFFFF, 560 * 360);
    p->tool = T_BRUSH;
    p->size = 2;
    p->fg = HEX(0x000000);
    p->bg = HEX(0xFFFFFF);
    p->mb.entries = bar;
    p->mb.n = ARRAY_LEN(bar);
    p->mb.open = p->mb.hover = -1;
    window_t *w = wm_create("untitled - Paint", ICON_PAINT, CANVAS_X + 4 + 560 + 16, CANVAS_Y + 4 + 360 + 14 + PALETTE_H + STATUS_H,
                            &pt_ops, p, WF_RESIZABLE);
    w->bg = COL_FACE;
    w->min_w = 400;
    w->min_h = 300;
    wm_set_tick(w, 40);
    return w;
}
