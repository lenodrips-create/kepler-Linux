/* Welcome: the screen that greets you after signing in to Kepler Linux. Five
 * pages (Welcome, First Steps, Documentation, Help, Contribute) on the frosted
 * Kepler background, and "Show this dialog at startup". */

#include "apps.h"

#define WIN_W   980
#define WIN_H   527
#define SIDE_W  171
#define ROW_Y0  10
#define ROW_H   52
#define BAR_Y   478
#define TEXT_X  203
#define TEXT_W  740
#define ICON_SZ 34

extern const uint32_t welcome_bg_argb[];
extern const uint8_t welcome_icons_a8[];
static const image_t bg_img = { (uint32_t *)welcome_bg_argb, WIN_W, WIN_H };

/* a paragraph; one that starts with "@cmd|" is a link that runs cmd */
typedef struct {
    const char *title;
    const char *nav;
    const char *paras[9];
} page_t;

static const page_t pages[] = {
    { "Welcome to Kepler Linux", "Welcome",
      { "Welcome to your new operating system!",
        "This welcome screen will guide you through your first steps, show you how to find help and where to get "
        "more information about Kepler Linux.",
        "On behalf of the development team and everyone involved in the project, we'd like to thank you for "
        "choosing Kepler Linux.\nWe hope you'll enjoy using it as much as we enjoy working on it.",
        "Have a great time and don't hesitate to send us your feedback." } },
    { "First Steps", "First Steps",
      { "Here are a few things to try first. Click one to open it.",
        "@kepler|Browse the web with Kepler",
        "@brady|Learn the Linux terminal with BradyBuddy",
        "@terminal|Open the Terminal and type help",
        "@finder|Look around your files in Finder",
        "@display|Change the look in System Preferences",
        "Tip: press Super+Space (the Windows key and Space) to search for apps and files." } },
    { "Documentation", "Documentation",
      { "Kepler Linux is KeplerOS on top of Ubuntu 24.04 LTS and the Linux kernel. The desktop, the dock and the "
        "apps are KeplerOS; underneath is a full Linux system.",
        "Shortcuts:  Super+Space search   Alt+Tab switch windows   Super+W close a window",
        "In the Terminal, type help for the KeplerOS commands, or add --help to any Linux command. Install "
        "programs with:  sudo apt install <name>",
        "The live disc runs from memory: files and programs you add are gone after a restart." } },
    { "Help", "Help",
      { "Stuck? These can help:",
        "@brady|BradyBuddy: lessons, a command finder and a quiz",
        "@kepler https://help.ubuntu.com|Ubuntu's documentation (it applies to Kepler Linux too)",
        "@taskmgr|Activity Monitor: see what is running",
        "Every Linux command explains itself: try  ls --help  in the Terminal." } },
    { "Contribute", "Contribute",
      { "Kepler Linux is open source, and everyone is welcome to help: report bugs, suggest ideas, improve the "
        "code or the documentation.",
        "@kepler https://github.com/lenodrips-create/kepler-Linux|Kepler Linux on GitHub",
        "@kepler https://github.com/lenodrips-create/kepler|The Kepler browser on GitHub",
        "Found a bug or have an idea? Open an issue on GitHub and tell us about it." } },
};

typedef struct {
    int page, hover;
    bool show_at_startup;
    image_t icons[5];
    struct {
        rect_t r;
        const char *cmd;
        int cmd_len;
    } links[8];
    int n_links;
} welcome_t;

bool welcome_at_startup(void)
{
#ifdef KEPLER_HOSTED
    return host_get_flag("welcome", true);
#else
    return true;
#endif
}

static rect_t row_rect(int i) { return rect_make(1, ROW_Y0 + i * ROW_H, SIDE_W - 4, ROW_H - 3); }
static rect_t check_rect(void) { return rect_make(712, BAR_Y + 14, 20, 20); }

static void wl_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    welcome_t *d = w->data;
    gfx_blit(g, 0, 0, &bg_img);

    /* the sidebar */
    for (int i = 0; i < ARRAY_LEN(pages); i++) {
        rect_t r = row_rect(i);
        bool sel = i == d->page;
        if (sel) {
            gfx_round_rect_vgrad(g, r.x, r.y, r.w, r.h, 3, HEX(0x8EDDF0), HEX(0x34A9CF), CORNER_ALL);
            gfx_hline(g, r.x + 2, r.y + 1, r.w - 4, RGBA(255, 255, 255, 170));
            gfx_round_frame(g, r.x, r.y, r.w, r.h, 3, RGBA(20, 120, 160, 120), CORNER_ALL);
        } else if (i == d->hover) {
            gfx_round_rect(g, r.x, r.y, r.w, r.h, 3, RGBA(120, 210, 235, 70), CORNER_ALL);
        }
        if (i + 1 < ARRAY_LEN(pages))
            gfx_hline(g, 0, r.y + ROW_H - 2, SIDE_W - 2, RGBA(120, 140, 160, 90));
        gfx_blit_tint(g, r.x + 10, r.y + (r.h - ICON_SZ) / 2, &d->icons[i], sel ? HEX(0xFFFFFF) : HEX(0x2A2A2E));
        gfx_text(g, &font_mono, r.x + 52, r.y + (r.h - font_mono.height) / 2, pages[i].nav,
                 sel ? HEX(0xFFFFFF) : HEX(0x1A1A1E));
    }

    /* the page */
    const page_t *p = &pages[d->page];
    gfx_text(g, &font_big_bold, TEXT_X, 36, p->title, HEX(0x111114));
    int y = 92;
    d->n_links = 0;
    for (int i = 0; i < ARRAY_LEN(p->paras) && p->paras[i]; i++) {
        const char *s = p->paras[i];
        if (s[0] == '@') {
            const char *bar = strchr(s, '|');
            const char *label = bar + 1;
            int k = d->n_links;
            bool hot = d->hover == 100 + k;
            int lw = text_width(&font_mono, label);
            gfx_circle(g, TEXT_X + 6, y + 9, 4, HEX(0x2A9FCB));
            gfx_text(g, &font_mono, TEXT_X + 20, y, label, hot ? HEX(0x0B6E99) : HEX(0x1478A8));
            if (hot)
                gfx_hline(g, TEXT_X + 20, y + font_mono.height, lw, HEX(0x0B6E99));
            if (k < ARRAY_LEN(d->links)) {
                d->links[k].r = rect_make(TEXT_X, y - 3, lw + 26, font_mono.height + 6);
                d->links[k].cmd = s + 1;
                d->links[k].cmd_len = bar - s - 1;
                d->n_links++;
            }
            y += 30;
        } else {
            y += gfx_text_wrapped(g, &font_mono, TEXT_X, y, d->page == 0 || y < 380 ? TEXT_W : TEXT_W - 240, s,
                                  HEX(0x18181C), 25) + 18;
        }
    }

    /* the bottom bar */
    rect_t c = check_rect();
    if (d->show_at_startup) {
        gfx_round_rect_vgrad(g, c.x, c.y, c.w, c.h, 3, HEX(0x5CC3E6), HEX(0x2486B4), CORNER_ALL);
        gfx_thick_line(g, c.x + 5, c.y + 10, c.x + 9, c.y + 14, 1, HEX(0xFFFFFF));
        gfx_thick_line(g, c.x + 9, c.y + 14, c.x + 15, c.y + 6, 1, HEX(0xFFFFFF));
    } else {
        gfx_round_rect(g, c.x, c.y, c.w, c.h, 3, HEX(0xFFFFFF), CORNER_ALL);
        gfx_round_frame(g, c.x, c.y, c.w, c.h, 3, HEX(0x6E8A9A), CORNER_ALL);
    }
    gfx_text(g, &font_mono, c.x + 30, c.y + 1, "Show this dialog at startup", HEX(0x18181C));
    (void)cw;
    (void)ch;
}

static int hit(welcome_t *d, int x, int y)
{
    for (int i = 0; i < ARRAY_LEN(pages); i++)
        if (rect_contains(row_rect(i), x, y))
            return i;
    for (int k = 0; k < d->n_links; k++)
        if (rect_contains(d->links[k].r, x, y))
            return 100 + k;
    rect_t c = check_rect();
    if (rect_contains(rect_make(c.x - 4, c.y - 4, 260, c.h + 8), x, y))
        return 50;
    return -1;
}

static void set_page(window_t *w, welcome_t *d, int p)
{
    d->page = CLAMP(p, 0, ARRAY_LEN(pages) - 1);
    d->hover = -1;
    wm_invalidate_client(w);
}

static void wl_mouse(window_t *w, int kind, int x, int y, int button)
{
    welcome_t *d = w->data;
    (void)button;
    int h = kind == MS_LEAVE ? -1 : hit(d, x, y);
    if (kind == MS_MOVE || kind == MS_LEAVE) {
        if (h != d->hover) {
            d->hover = h;
            ui_set_cursor(h >= 0 ? CURSOR_HAND : CURSOR_ARROW);
            wm_invalidate_client(w);
        }
        return;
    }
    if (kind != MS_DOWN || h < 0)
        return;
    if (h < ARRAY_LEN(pages)) {
        set_page(w, d, h);
    } else if (h == 50) {
        d->show_at_startup = !d->show_at_startup;
#ifdef KEPLER_HOSTED
        host_set_flag("welcome", d->show_at_startup);
#endif
        wm_invalidate_client(w);
    } else if (h >= 100) {
        char cmd[160];
        int n = MIN(d->links[h - 100].cmd_len, (int)sizeof cmd - 1);
        memcpy(cmd, d->links[h - 100].cmd, n);
        cmd[n] = 0;
        ui_set_cursor(CURSOR_ARROW);
        ui_launch(cmd);
    }
}

static void wl_key(window_t *w, const input_event_t *ev)
{
    welcome_t *d = w->data;
    if (!ev->pressed)
        return;
    if (ev->key == KEY_DOWN || ev->key == KEY_TAB)
        set_page(w, d, (d->page + 1) % ARRAY_LEN(pages));
    else if (ev->key == KEY_UP)
        set_page(w, d, (d->page + ARRAY_LEN(pages) - 1) % ARRAY_LEN(pages));
    else if (ev->key == KEY_ESC)
        wm_close(w);
}

static void wl_close(window_t *w)
{
    welcome_t *d = w->data;
    for (int i = 0; i < 5; i++)
        kfree(d->icons[i].px);
    kfree(d);
}

static const window_ops_t wl_ops = { wl_paint, wl_key, wl_mouse, NULL, wl_close, NULL };

window_t *welcome_open(void)
{
    window_t *w = wm_find(&wl_ops);
    if (w) {
        wm_focus(w);
        return w;
    }
    welcome_t *d = kzalloc(sizeof *d);
    d->hover = -1;
    d->show_at_startup = welcome_at_startup();
    /* the sidebar's icons: alpha masks, drawn in the row's text colour */
    for (int i = 0; i < 5; i++) {
        d->icons[i] = (image_t){ kmalloc(ICON_SZ * ICON_SZ * 4), ICON_SZ, ICON_SZ };
        for (int k = 0; k < ICON_SZ * ICON_SZ; k++)
            d->icons[i].px[k] = (uint32_t)welcome_icons_a8[i * ICON_SZ * ICON_SZ + k] << 24 | 0xFFFFFF;
    }
    w = wm_create("Welcome", ICON_INFO, WIN_W, WIN_H, &wl_ops, d, WF_NO_MAX);
    wm_set_app(w, "Welcome", NULL, NULL, NULL);
    wm_center(w);
    return w;
}
