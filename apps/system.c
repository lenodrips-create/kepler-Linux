/* About KeplerOS and System Properties. */

#include "apps.h"

static void commas(uint32_t v, char *out, int n)
{
    char tmp[16];
    ksnprintf(tmp, sizeof tmp, "%u", v);
    int len = strlen(tmp), o = 0;
    for (int i = 0; i < len && o < n - 1; i++) {
        out[o++] = tmp[i];
        if ((len - i - 1) % 3 == 0 && i != len - 1 && o < n - 1)
            out[o++] = ',';
    }
    out[o] = 0;
}

static void paint_logo_banner(gfx_t *g, int w, int h)
{
    gfx_vgrad(g, 0, 0, w, h, HEX(0xFFFFFF), HEX(0xE6EEFC));
    gfx_blit_alpha(g, 22, (h - 64) / 2, icon_get(ICON_KEPLER, 64), 255);
    int x = 100, y = (h - font_logo_small.height) / 2 - 6;
    x = gfx_text(g, &font_logo_small, x, y, "Kepler", HEX(0x1D4FB8));
    gfx_text(g, &font_logo_small, x + 2, y, "OS", HEX(0xF08C12));
    gfx_text(g, &font_ui, 102, y + font_logo_small.height, "a small operating system written in C", HEX(0x5A6B85));
    gfx_hgrad(g, 0, h - 4, w / 2, 4, HEX(0x2F64DA), HEX(0x5BB0F5));
    gfx_hgrad(g, w / 2, h - 4, w - w / 2, 4, HEX(0x5BB0F5), HEX(0x6CC04A));
}

/* ---- About ------------------------------------------------------------------------------------------- */

typedef struct {
    button_t items[1];
    buttons_t btns;
} about_t;

static void about_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    about_t *a = w->data;
    paint_logo_banner(g, cw, 92);
    int x = 22, y = 108;
    gfx_text(g, &font_ui, x, y, "KeplerOS", HEX(0x000000));
    y += 15;
    gfx_text(g, &font_ui, x, y, "Version " KEPLER_VERSION " (Build " KEPLER_BUILD ")", HEX(0x000000));
    y += 15;
    gfx_text(g, &font_ui, x, y, "Copyright (C) 2026 The KeplerOS Project", HEX(0x000000));
    y += 26;
    gfx_text(g, &font_ui, x, y, "Created by Lennon Helman with the help of Claude Code.", HEX(0x000000));
    y += 26;
    gfx_text(g, &font_ui, x, y, "This product is licensed to:", HEX(0x000000));
    y += 17;
    gfx_text(g, &font_ui_bold, x + 30, y, g_user_name, HEX(0x000000));
    y += 26;
    gfx_hline(g, x, y, cw - 2 * x, HEX(0xACA899));
    gfx_hline(g, x, y + 1, cw - 2 * x, HEX(0xFFFFFF));
    y += 12;
    char mem[32], line[96];
    commas(g_sys.mem_kb, mem, sizeof mem);
    ksnprintf(line, sizeof line, "Physical memory available to KeplerOS:  %s KB", mem);
    gfx_text(g, &font_ui, x, y, line, HEX(0x000000));
    buttons_paint(&a->btns, g);
}

static void about_mouse(window_t *w, int kind, int x, int y, int button)
{
    about_t *a = w->data;
    if (buttons_mouse(&a->btns, w, kind, x, y) == 1)
        wm_close(w);
}

static void about_key(window_t *w, const input_event_t *ev)
{
    if (ev->pressed && (ev->key == KEY_ENTER || ev->key == KEY_ESC))
        wm_close(w);
}

static void free_data(window_t *w) { kfree(w->data); }

static const window_ops_t about_ops = { about_paint, about_key, about_mouse, NULL, free_data, NULL };

window_t *about_open(void)
{
    window_t *w = wm_find(&about_ops);
    if (w) {
        wm_focus(w);
        return w;
    }
    about_t *a = kzalloc(sizeof *a);
    int cw = 430, ch = 336;
    a->items[0] = (button_t){ cw - 97, ch - 37, 75, 23, "OK", 1, BTN_DEFAULT };
    a->btns = (buttons_t){ a->items, 1, -1, -1 };
    w = wm_create("About KeplerOS", ICON_KEPLER, cw, ch, &about_ops, a, WF_DIALOG);
    wm_center(w);
    return w;
}

/* ---- System Properties --------------------------------------------------------------------------------- */

static const char *const sp_tabs[] = { "General", "Computer Name", "Hardware", "Advanced" };

typedef struct {
    int tab;
    button_t items[3];
    buttons_t btns;
} sysprops_t;

static void sp_general(gfx_t *g, int x, int y, int w)
{
    gfx_blit_alpha(g, x + 30, y + 26, icon_get(ICON_KEPLER, 64), 255);
    int lx = x + 26, ly = y + 100;
    int tx = gfx_text(g, &font_logo_small, lx, ly, "Kepler", HEX(0x1D4FB8));
    gfx_text(g, &font_logo_small, tx + 2, ly, "OS", HEX(0xF08C12));

    int cx = x + 196, cy = y + 24;
    char line[96];
    const struct {
        const char *text;
        bool head;
    } rows[] = {
        { "System:", true },
        { "KeplerOS", false },
        { "Version " KEPLER_VERSION, false },
        { "Build " KEPLER_BUILD, false },
        { "", false },
        { "Registered to:", true },
        { NULL, false },
        { "", false },
        { "Computer:", true },
    };
    for (int i = 0; i < ARRAY_LEN(rows); i++) {
        const char *t = rows[i].text ? rows[i].text : g_user_name;
        gfx_text(g, &font_ui, rows[i].head ? cx : cx + 18, cy, t, HEX(0x000000));
        cy += 15;
    }
    gfx_text_clipped(g, &font_ui, cx + 18, cy, w - (cx - x) - 26, g_sys.cpu_brand, HEX(0x000000));
    cy += 15;
    ksnprintf(line, sizeof line, "%u MHz", g_sys.cpu_mhz);
    gfx_text(g, &font_ui, cx + 18, cy, line, HEX(0x000000));
    cy += 15;
    ksnprintf(line, sizeof line, "%u MB of RAM", g_sys.mem_kb / 1024);
    gfx_text(g, &font_ui, cx + 18, cy, line, HEX(0x000000));
    cy += 15;
    ksnprintf(line, sizeof line, "%dx%d, 32-bit colour", g_sys.screen_w, g_sys.screen_h);
    gfx_text(g, &font_ui, cx + 18, cy, line, HEX(0x000000));
}

static void sp_name(gfx_t *g, int x, int y, int w)
{
    gfx_blit_alpha(g, x + 16, y + 18, icon_get(ICON_COMPUTER, 32), 255);
    gfx_text_wrapped(g, &font_ui, x + 60, y + 22, w - 80,
                     "KeplerOS uses the following information to identify your computer on the network.", HEX(0x000000), 15);
    gfx_text(g, &font_ui, x + 16, y + 80, "Computer description:", HEX(0x000000));
    draw_textbox(g, x + 150, y + 76, w - 170, 20);
    gfx_text(g, &font_ui, x + 155, y + 80, "Kepler's computer", HEX(0x000000));
    gfx_text(g, &font_ui, x + 16, y + 116, "Full computer name:", HEX(0x000000));
    gfx_text(g, &font_ui, x + 150, y + 116, "keplerpc.", HEX(0x000000));
    gfx_text(g, &font_ui, x + 16, y + 140, "Workgroup:", HEX(0x000000));
    gfx_text(g, &font_ui, x + 150, y + 140, "KEPLERNET", HEX(0x000000));
}

static void sp_hardware(gfx_t *g, int x, int y, int w, int h)
{
    draw_groupbox(g, x + 10, y + 10, w - 20, h - 20, "Devices");
    int ly = y + 32;
    static const char *fixed[] = { "PS/2 keyboard (IRQ 1)", "PS/2 mouse with wheel (IRQ 12)", "8254 interval timer at 1000 Hz",
                                   "CMOS real-time clock" };
    for (int i = 0; i < ARRAY_LEN(fixed); i++) {
        gfx_blit_alpha(g, x + 22, ly - 1, icon_get(ICON_COMPUTER, 16), 255);
        gfx_text(g, &font_ui, x + 44, ly, fixed[i], HEX(0x000000));
        ly += 18;
    }
    gfx_blit_alpha(g, x + 22, ly - 1, icon_get(ICON_DISPLAY, 16), 255);
    gfx_text(g, &font_ui, x + 44, ly, g_sys.video, HEX(0x000000));
    ly += 26;
    gfx_text(g, &font_ui_bold, x + 22, ly, "PCI bus", HEX(0x000000));
    ly += 18;
    char lines[16][64];
    int n = pci_list(lines, 16);
    for (int i = 0; i < n && ly < y + h - 24; i++) {
        gfx_text(g, &font_ui, x + 30, ly, lines[i], HEX(0x000000));
        ly += 16;
    }
}

static void sp_advanced(gfx_t *g, int x, int y, int w)
{
    char line[96];
    draw_groupbox(g, x + 10, y + 10, w - 20, 92, "Performance");
    gfx_text_wrapped(g, &font_ui, x + 22, y + 32, w - 44,
                     "Visual effects: antialiased fonts, smooth window corners and supersampled icons are always on.",
                     HEX(0x000000), 15);
    ksnprintf(line, sizeof line, "Boot to splash finished in %u ms.", g_sys.boot_ms);
    gfx_text(g, &font_ui, x + 22, y + 72, line, HEX(0x000000));
    draw_groupbox(g, x + 10, y + 114, w - 20, 92, "Memory");
    ksnprintf(line, sizeof line, "Kernel heap: %u KB used of %u KB", (unsigned)(heap_used() / 1024), (unsigned)(heap_total() / 1024));
    gfx_text(g, &font_ui, x + 22, y + 136, line, HEX(0x000000));
    draw_progress(g, x + 22, y + 156, w - 44, 18, (int)(heap_used() * 100 / heap_total()));
}

static void sp_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    sysprops_t *s = w->data;
    int px = 8, py = 8, pw = cw - 16, ph = ch - 56;
    draw_tabs(g, px, py, pw, sp_tabs, ARRAY_LEN(sp_tabs), s->tab);
    gfx_fill(g, px, py + 22, pw, ph - 22, HEX(0xFCFCFE));
    gfx_rect(g, px, py + 21, pw, ph - 21, HEX(0x919B9C));
    draw_tabs(g, px, py, pw, sp_tabs, ARRAY_LEN(sp_tabs), s->tab);
    gfx_t pg = gfx_sub(g, px + 1, py + 22, pw - 2, ph - 23);
    switch (s->tab) {
    case 0: sp_general(&pg, 0, 0, pw - 2); break;
    case 1: sp_name(&pg, 0, 0, pw - 2); break;
    case 2: sp_hardware(&pg, 0, 0, pw - 2, ph - 23); break;
    case 3: sp_advanced(&pg, 0, 0, pw - 2); break;
    }
    buttons_paint(&s->btns, g);
}

static void sp_mouse(window_t *w, int kind, int x, int y, int button)
{
    sysprops_t *s = w->data;
    if (kind == MS_DOWN) {
        int t = tabs_hit(8, 8, sp_tabs, ARRAY_LEN(sp_tabs), x, y);
        if (t >= 0) {
            s->tab = t;
            wm_invalidate_client(w);
            return;
        }
    }
    int id = buttons_mouse(&s->btns, w, kind, x, y);
    if (id == 1 || id == 2)
        wm_close(w);
}

static void sp_key(window_t *w, const input_event_t *ev)
{
    sysprops_t *s = w->data;
    if (!ev->pressed)
        return;
    if (ev->key == KEY_ENTER || ev->key == KEY_ESC)
        wm_close(w);
    else if (ev->key == KEY_TAB && (ev->mods & MOD_CTRL)) {
        s->tab = (s->tab + 1) % ARRAY_LEN(sp_tabs);
        wm_invalidate_client(w);
    }
}

static const window_ops_t sp_ops = { sp_paint, sp_key, sp_mouse, NULL, free_data, NULL };

window_t *sysprops_open(void)
{
    window_t *w = wm_find(&sp_ops);
    if (w) {
        wm_focus(w);
        return w;
    }
    sysprops_t *s = kzalloc(sizeof *s);
    int cw = 420, ch = 420;
    s->items[0] = (button_t){ cw - 252, ch - 35, 75, 23, "OK", 1, BTN_DEFAULT };
    s->items[1] = (button_t){ cw - 170, ch - 35, 75, 23, "Cancel", 2, 0 };
    s->items[2] = (button_t){ cw - 88, ch - 35, 75, 23, "Apply", 3, BTN_DISABLED };
    s->btns = (buttons_t){ s->items, 3, -1, -1 };
    w = wm_create("System Properties", ICON_SYSINFO, cw, ch, &sp_ops, s, WF_DIALOG);
    return w;
}
