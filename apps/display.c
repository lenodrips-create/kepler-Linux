/* Display Properties: pick the desktop background. */

#include "apps.h"

static const char *const dp_tabs[] = { "Themes", "Desktop", "Screen Saver", "Appearance", "Settings" };

static const struct {
    const char *name;
    int style;
} backgrounds[] = {
    { "(None)", 3 },
    { "Azure", 2 },
    { "Kepler Earth", 0 },
    { "Kepler Night", 1 },
};

static const uint32_t solid_colors[] = { 0x3A6EA5, 0x004E98, 0x008080, 0x2F6B2F, 0x5A2D82, 0x000000, 0x6D6D6D, 0x8B1A1A };

#define PREV_W 156
#define PREV_H 98

typedef struct {
    int tab;
    int sel; /* index into backgrounds */
    int color_idx;
    image_t *thumbs[4];
    button_t items[4];
    buttons_t btns;
} display_t;

static image_t *thumb(display_t *d, int i)
{
    if (backgrounds[i].style == 3) {
        image_free(d->thumbs[i]);
        d->thumbs[i] = NULL;
    }
    if (!d->thumbs[i])
        d->thumbs[i] = desktop_render_background(backgrounds[i].style, HEX(solid_colors[d->color_idx]), PREV_W, PREV_H);
    return d->thumbs[i];
}

static void monitor(gfx_t *g, int x, int y, const image_t *screen)
{
    /* bezel, stand and screen */
    gfx_round_rect_vgrad(g, x + PREV_W / 2 - 30, y + PREV_H + 22, 60, 8, 3, HEX(0xD9D7CF), HEX(0x9C998D), CORNER_ALL);
    gfx_fill(g, x + PREV_W / 2 - 12, y + PREV_H + 14, 24, 9, HEX(0xB4B1A6));
    gfx_round_rect(g, x - 12, y - 12, PREV_W + 24, PREV_H + 28, 6, HEX(0x6E6B61), CORNER_ALL);
    gfx_round_rect_vgrad(g, x - 11, y - 11, PREV_W + 22, PREV_H + 26, 5, HEX(0xF3F2EC), HEX(0xC9C6BA), CORNER_ALL);
    gfx_fill(g, x - 2, y - 2, PREV_W + 4, PREV_H + 4, HEX(0x5D5A51));
    if (screen)
        gfx_blit(g, x, y, screen);
    else
        gfx_fill(g, x, y, PREV_W, PREV_H, HEX(0x000000));
    gfx_circle(g, x + PREV_W + 4, y + PREV_H + 9, 2, HEX(0x37C837));
}

static rect_t list_rect(int cw) { return rect_make(20, 200, cw - 160, 120); }
static rect_t color_rect(int cw) { return rect_make(cw - 120, 216, 90, 24); }

static void paint_desktop_tab(display_t *d, gfx_t *g, int x, int y, int w, int cw)
{
    const image_t *preview = thumb(d, d->sel);
    monitor(g, x + (w - PREV_W) / 2, y + 30, preview);
    gfx_text(g, &font_ui, 20, 182, "Background:", HEX(0x000000));
    rect_t lr = list_rect(cw);
    draw_textbox(g, lr.x, lr.y, lr.w, lr.h);
    for (int i = 0; i < ARRAY_LEN(backgrounds); i++) {
        int ry = lr.y + 3 + i * 20;
        bool sel = i == d->sel;
        if (sel)
            gfx_fill(g, lr.x + 2, ry, lr.w - 4, 19, COL_SELECT);
        gfx_blit_alpha(g, lr.x + 6, ry + 1, icon_get(i == 0 ? ICON_DISPLAY : ICON_PICTURES, 16), 255);
        gfx_text(g, &font_ui, lr.x + 28, ry + 3, backgrounds[i].name, sel ? HEX(0xFFFFFF) : HEX(0x000000));
    }
    gfx_text(g, &font_ui, cw - 120, 200, "Color:", HEX(0x000000));
    rect_t cr = color_rect(cw);
    draw_button_face(g, cr.x, cr.y, cr.w, cr.h, 0);
    gfx_fill(g, cr.x + 6, cr.y + 5, cr.w - 30, cr.h - 10, HEX(solid_colors[d->color_idx]));
    gfx_rect(g, cr.x + 6, cr.y + 5, cr.w - 30, cr.h - 10, HEX(0x000000));
    for (int k = 0; k < 4; k++)
        gfx_hline(g, cr.x + cr.w - 18 + k, cr.y + 10 + k, 7 - 2 * k, HEX(0x000000));
    draw_button(g, 20, 332, 150, 23, "Customize Desktop...", BTN_DISABLED);
}

static void paint_settings_tab(display_t *d, gfx_t *g, int x, int y, int w, int cw)
{
    image_t *cur = desktop_render_background(desktop_background_style(), desktop_background_color(), PREV_W, PREV_H);
    monitor(g, x + (w - PREV_W) / 2, y + 30, cur);
    image_free(cur);
    gfx_text(g, &font_ui, 20, 190, "Display:", HEX(0x000000));
    gfx_text(g, &font_ui, 20, 206, "Plug and Play Monitor on ", HEX(0x000000));
    gfx_text(g, &font_ui, 20 + text_width(&font_ui, "Plug and Play Monitor on "), 206, g_sys.video, HEX(0x000000));
    draw_groupbox(g, 20, 230, (cw - 50) / 2, 80, "Screen resolution");
    gfx_text(g, &font_ui, 32, 252, "Less", HEX(0x000000));
    gfx_text(g, &font_ui, 20 + (cw - 50) / 2 - 36, 252, "More", HEX(0x000000));
    gfx_hline(g, 64, 258, (cw - 50) / 2 - 110, HEX(0x9C9C9C));
    gfx_fill(g, 64 + (cw - 50) / 2 - 130, 250, 8, 16, HEX(0x2F64DA));
    char res[48];
    ksnprintf(res, sizeof res, "%d by %d pixels", g_sys.screen_w, g_sys.screen_h);
    gfx_text_centered(g, &font_ui, 20, 280, (cw - 50) / 2, res, HEX(0x000000));
    int gx = 30 + (cw - 50) / 2;
    draw_groupbox(g, gx, 230, (cw - 50) / 2, 80, "Color quality");
    draw_textbox(g, gx + 12, 254, (cw - 50) / 2 - 24, 20);
    gfx_text(g, &font_ui, gx + 17, 258, "Highest (32 bit)", HEX(0x000000));
}

static void paint_other_tab(display_t *d, gfx_t *g, int x, int y, int w, int tab)
{
    if (tab == 2) {
        monitor(g, x + (w - PREV_W) / 2, y + 30, NULL);
        gfx_text_centered(g, &font_ui, x, y + 30 + PREV_H / 2 - 6, w, "KeplerOS", HEX(0x58C4FF));
        gfx_text(g, &font_ui, 20, 210, "Screen saver:", HEX(0x000000));
        draw_textbox(g, 20, 228, 200, 20);
        gfx_text(g, &font_ui, 26, 232, "(None)", HEX(0x000000));
        return;
    }
    const char *lines[3];
    if (tab == 0) {
        lines[0] = "A theme is a background plus a set of sounds, icons";
        lines[1] = "and other elements to help you personalize your computer.";
        lines[2] = "Theme:  Kepler Luna (current)";
    } else {
        lines[0] = "Windows and buttons:  Luna style";
        lines[1] = "Color scheme:  Default (blue)";
        lines[2] = "Font size:  Normal";
    }
    image_t *cur = desktop_render_background(desktop_background_style(), desktop_background_color(), PREV_W, PREV_H);
    monitor(g, x + (w - PREV_W) / 2, y + 30, cur);
    image_free(cur);
    for (int i = 0; i < 3; i++)
        gfx_text(g, &font_ui, 24, 200 + i * 20, lines[i], HEX(0x000000));
}

static void dp_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    display_t *d = w->data;
    int px = 7, py = 8, pw = cw - 14, ph = ch - 52;
    gfx_fill(g, px, py + 22, pw, ph - 22, HEX(0xFCFCFE));
    gfx_rect(g, px, py + 21, pw, ph - 21, HEX(0x919B9C));
    draw_tabs(g, px, py, pw, dp_tabs, ARRAY_LEN(dp_tabs), d->tab);
    switch (d->tab) {
    case 1: paint_desktop_tab(d, g, px, py + 22, pw, cw); break;
    case 4: paint_settings_tab(d, g, px, py + 22, pw, cw); break;
    default: paint_other_tab(d, g, px, py + 22, pw, d->tab); break;
    }
    buttons_paint(&d->btns, g);
}

static void apply(display_t *d)
{
    desktop_set_background(backgrounds[d->sel].style, HEX(solid_colors[d->color_idx]));
    d->items[2].flags = BTN_DISABLED;
}

static void dp_mouse(window_t *w, int kind, int x, int y, int button)
{
    display_t *d = w->data;
    rect_t c = wm_client_rect(w);
    if (kind == MS_DOWN) {
        int t = tabs_hit(7, 8, dp_tabs, ARRAY_LEN(dp_tabs), x, y);
        if (t >= 0) {
            d->tab = t;
            wm_invalidate_client(w);
            return;
        }
        if (d->tab == 1) {
            rect_t lr = list_rect(c.w);
            if (rect_contains(lr, x, y)) {
                int i = (y - lr.y - 3) / 20;
                if (i >= 0 && i < ARRAY_LEN(backgrounds)) {
                    d->sel = i;
                    d->items[2].flags = 0;
                    wm_invalidate_client(w);
                }
                return;
            }
            if (rect_contains(color_rect(c.w), x, y)) {
                d->color_idx = (d->color_idx + 1) % ARRAY_LEN(solid_colors);
                d->sel = 0;
                d->items[2].flags = 0;
                wm_invalidate_client(w);
                return;
            }
        }
    }
    int id = buttons_mouse(&d->btns, w, kind, x, y);
    if (id == 1) {
        apply(d);
        wm_close(w);
    } else if (id == 2) {
        wm_close(w);
    } else if (id == 3) {
        apply(d);
        wm_invalidate_client(w);
    }
}

static void dp_key(window_t *w, const input_event_t *ev)
{
    display_t *d = w->data;
    if (!ev->pressed)
        return;
    if (ev->key == KEY_ESC)
        wm_close(w);
    else if (ev->key == KEY_ENTER) {
        apply(d);
        wm_close(w);
    } else if (d->tab == 1 && (ev->key == KEY_UP || ev->key == KEY_DOWN)) {
        d->sel = CLAMP(d->sel + (ev->key == KEY_DOWN ? 1 : -1), 0, ARRAY_LEN(backgrounds) - 1);
        d->items[2].flags = 0;
        wm_invalidate_client(w);
    }
}

static void dp_close(window_t *w)
{
    display_t *d = w->data;
    for (int i = 0; i < 4; i++)
        image_free(d->thumbs[i]);
    kfree(d);
}

static const window_ops_t dp_ops = { dp_paint, dp_key, dp_mouse, NULL, dp_close, NULL };

window_t *display_open(void)
{
    window_t *w = wm_find(&dp_ops);
    if (w) {
        wm_focus(w);
        return w;
    }
    display_t *d = kzalloc(sizeof *d);
    d->tab = 1;
    for (int i = 0; i < ARRAY_LEN(backgrounds); i++)
        if (backgrounds[i].style == desktop_background_style())
            d->sel = i;
    for (int i = 0; i < ARRAY_LEN(solid_colors); i++)
        if (HEX(solid_colors[i]) == desktop_background_color())
            d->color_idx = i;
    int cw = 404, ch = 440;
    d->items[0] = (button_t){ cw - 252, ch - 35, 75, 23, "OK", 1, BTN_DEFAULT };
    d->items[1] = (button_t){ cw - 170, ch - 35, 75, 23, "Cancel", 2, 0 };
    d->items[2] = (button_t){ cw - 88, ch - 35, 75, 23, "Apply", 3, BTN_DISABLED };
    d->btns = (buttons_t){ d->items, 3, -1, -1 };
    w = wm_create("Display Properties", ICON_DISPLAY, cw, ch, &dp_ops, d, WF_DIALOG);
    return w;
}
