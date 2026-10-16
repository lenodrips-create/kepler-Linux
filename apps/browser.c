/* Kepler: the web browser. A dark, space-themed window after the Kepler
 * desktop browser: a starfield start page with search and favourites, an
 * address bar that searches or navigates, and HTTPS pages laid out by html.c. */

#include "apps.h"
#include "html.h"
#include "http.h"
#include "jpeg.h"
#include "net.h"
#include "ramfs.h"

#define TOOLBAR_H  52
#define HIST_MAX   32
#define START_URL  "kepler://start"
#define SEARCH_URL "https://lite.duckduckgo.com/lite/?q="

/* the Kepler palette */
#define K_VOID   HEX(0x000000)
#define K_TEXT   HEX(0xECEBFF)
#define K_MUTED  HEX(0x8B8BA7)
#define K_VIOLET HEX(0x8B5CF6)
#define K_CYAN   HEX(0x22D3EE)
#define K_BAR    HEX(0x12121C)

typedef struct {
    char url[1100];
    int scroll;
} hist_t;

typedef struct {
    char url[1100]; /* what is shown */
    doc_t *doc;
    char *source; /* the page as it came, for View Source */
    int source_len;
    layout_t lay;
    int scroll;
    scrollbar_t sb;
    bool secure;

    http_t *req; /* what is loading */
    bool req_push;
    int progress;

    bool error_page;
    char error[200];
    char error_url[1100];

    textfield_t addr;
    bool addr_focus;
    bool addr_all; /* the whole address is selected: typing replaces it */
    textfield_t search; /* the start page's box */
    int focus_field;    /* a form field on the page, or -1 */

    int hover_link, press_link;
    int hover_tool, press_tool;
    int hover_tile, press_tile;
    bool hover_retry;

    hist_t hist[HIST_MAX];
    int hist_n, hist_pos;

    uint32_t anim;
    menubar_t mb;
} browser_t;

static const window_ops_t br_ops;

static const struct {
    const char *title;
    const char *url;
} tiles[] = {
    { "Wikipedia", "https://en.m.wikipedia.org/" },
    { "DuckDuckGo", "https://lite.duckduckgo.com/lite/" },
    { "Hacker News", "https://news.ycombinator.com/" },
    { "NPR News", "https://text.npr.org/" },
    { "CNN Lite", "https://lite.cnn.com/" },
    { "KeplerOS", "https://github.com/lenodrips-create/kepler-os-kernel" },
};

static bool is_start(browser_t *b) { return !strcmp(b->url, START_URL) && !b->error_page; }

/* ---- the start page logo, decoded once ---------------------------------------------- */

extern const uint8_t kepler_logo_jpg[], kepler_logo_jpg_end[];

static const image_t *logo(void)
{
    static image_t *img;
    static bool tried;
    if (!tried) {
        tried = true;
        image_t *src;
        if (jpeg_decode(kepler_logo_jpg, kepler_logo_jpg_end - kepler_logo_jpg, &src)) {
            img = image_resize(src, src->w * 172 / src->h, 172);
            image_free(src);
            /* the picture sits on black: turn brightness into coverage so the
             * starfield shows around and through it */
            for (int i = 0; i < img->w * img->h; i++) {
                uint32_t c = img->px[i];
                int r = (c >> 16) & 255, gr = (c >> 8) & 255, bl = c & 255;
                int a = MAX(r, MAX(gr, bl));
                img->px[i] = a < 8 ? 0 : RGBA(r * 255 / a, gr * 255 / a, bl * 255 / a, a);
            }
        }
    }
    return img;
}

/* ---- geometry -------------------------------------------------------------------------- */

enum { T_BACK, T_FWD, T_RELOAD, T_HOME, T_ADDR, N_TOOLS };

static rect_t tool_rect(int cw, int i)
{
    int y = (TOOLBAR_H - 28) / 2;
    if (i < T_ADDR)
        return rect_make(84 + i * 32, y, 30, 28);
    int x0 = 84 + 4 * 32 + 14, avail = cw - x0 - 20;
    int w = MIN(avail, 660);
    return rect_make(x0 + (avail - w) / 2, y - 1, w, 30);
}

static rect_t content_rect(int cw, int ch) { return rect_make(0, TOOLBAR_H, cw, ch - TOOLBAR_H); }

/* start page pieces, in content coordinates */
static int start_top(int h) { return MAX(18, h / 2 - 250); }

static rect_t search_rect(int w, int h)
{
    int sw = MIN(560, w - 80);
    return rect_make((w - sw) / 2, start_top(h) + 268, sw, 48);
}

static int tiles_per_row(int w) { return w >= 760 ? 6 : 3; }

static rect_t tile_rect(int w, int h, int i)
{
    int per = tiles_per_row(w), tw = 104, gap = 14;
    int row_w = per * tw + (per - 1) * gap;
    int x0 = (w - row_w) / 2, y0 = start_top(h) + 344;
    return rect_make(x0 + (i % per) * (tw + gap), y0 + (i / per) * (tw + gap), tw, tw);
}

static rect_t retry_rect(int w, int h) { return rect_make(w / 2 - 60, h / 2 + 70, 120, 32); }

/* ---- history and navigation -------------------------------------------------------------- */

static void cancel(browser_t *b)
{
    http_free(b->req);
    b->req = NULL;
    b->progress = 0;
}

static void history_push(browser_t *b, const char *url)
{
    if (b->hist_n && !strcmp(b->hist[b->hist_pos].url, url))
        return;
    if (b->hist_n)
        b->hist[b->hist_pos].scroll = b->scroll;
    int at = b->hist_n ? b->hist_pos + 1 : 0;
    if (at >= HIST_MAX) {
        memmove(&b->hist[0], &b->hist[1], sizeof b->hist[0] * (HIST_MAX - 1));
        at = HIST_MAX - 1;
    }
    strlcpy(b->hist[at].url, url, sizeof b->hist[at].url);
    b->hist[at].scroll = 0;
    b->hist_pos = at;
    b->hist_n = at + 1;
}

static void relayout(window_t *w, browser_t *b)
{
    rect_t c = wm_client_rect(w);
    if (!b->doc)
        return;
    html_layout(b->doc, &b->lay, c.w - SCROLLBAR_W);
    b->sb.total = b->lay.height;
    b->sb.visible = c.h - TOOLBAR_H;
    b->sb.pos = b->scroll;
    scrollbar_clamp(&b->sb);
    b->scroll = b->sb.pos;
}

static void show_title(window_t *w, browser_t *b)
{
    if (b->error_page)
        wm_set_title(w, "Failed to open page");
    else if (is_start(b))
        wm_set_title(w, "Start Page");
    else
        wm_set_title(w, b->doc && b->doc->title[0] ? b->doc->title : b->url);
}

static void navigate(window_t *w, browser_t *b, const char *url, bool push);

static void go_start(window_t *w, browser_t *b, bool push)
{
    cancel(b);
    html_free(b->doc);
    b->doc = NULL;
    html_layout_free(&b->lay);
    strlcpy(b->url, START_URL, sizeof b->url);
    b->error_page = false;
    b->secure = false;
    b->scroll = 0;
    b->focus_field = -1;
    textfield_set(&b->addr, "");
    textfield_set(&b->search, "");
    if (push)
        history_push(b, START_URL);
    show_title(w, b);
    wm_invalidate(w);
}

static void percent_decode(const char *s, char *out, int n)
{
    int o = 0;
    for (; *s && o < n - 1; s++) {
        if (*s == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], 0 };
            int v = 0;
            for (int i = 0; i < 2; i++) {
                char c = ktolower(hex[i]);
                v = v * 16 + (kisdigit(c) ? c - '0' : c - 'a' + 10);
            }
            out[o++] = (char)v;
            s += 2;
        } else {
            out[o++] = *s == '+' ? ' ' : *s;
        }
    }
    out[o] = 0;
}

static void navigate(window_t *w, browser_t *b, const char *url, bool push)
{
    if (!strcmp(url, START_URL)) {
        go_start(w, b, push);
        return;
    }
    /* DuckDuckGo wraps result links in a redirect page: go straight there */
    char direct[1100];
    const char *uddg = strstr(url, "duckduckgo.com/l/?");
    if (uddg && (uddg = strstr(uddg, "uddg="))) {
        char enc[1100];
        strlcpy(enc, uddg + 5, sizeof enc);
        char *amp = strchr(enc, '&');
        if (amp)
            *amp = 0;
        percent_decode(enc, direct, sizeof direct);
        url = direct;
    }
    cancel(b);
    b->req = http_get(url);
    b->req_push = push;
    b->progress = 6;
    b->addr_focus = false;
    textfield_set(&b->addr, url);
    wm_set_tick(w, 15);
    wm_invalidate(w);
}

/* the address bar takes the keyboard with its text selected, like Ctrl+L elsewhere */
static void focus_addr(window_t *w, browser_t *b)
{
    b->addr_focus = true;
    textfield_set(&b->addr, is_start(b) && !b->req ? "" : b->error_page ? b->error_url : b->url);
    b->addr_all = b->addr.text[0] != 0;
    wm_invalidate(w);
}

/* what the address bar or the search box means */
static void go_input(window_t *w, browser_t *b, const char *text)
{
    while (*text == ' ')
        text++;
    char t[512];
    strlcpy(t, text, sizeof t);
    int n = strlen(t);
    while (n && t[n - 1] == ' ')
        t[--n] = 0;
    if (!n)
        return;
    char url[1100];
    bool search = t[0] == '?';
    if (search) {
        char q[800];
        url_encode(t + 1 + (t[1] == ' '), q, sizeof q);
        ksnprintf(url, sizeof url, "%s%s", SEARCH_URL, q);
    } else if (strstr(t, "://") || !strncmp(t, "kepler:", 7)) {
        strlcpy(url, t, sizeof url);
    } else if (!strchr(t, ' ') && (strchr(t, '.') || !strncmp(t, "localhost", 9))) {
        ksnprintf(url, sizeof url, "https://%s", t);
    } else {
        char q[800];
        url_encode(t, q, sizeof q);
        ksnprintf(url, sizeof url, "%s%s", SEARCH_URL, q);
    }
    navigate(w, b, url, true);
}

static void loaded(window_t *w, browser_t *b)
{
    http_t *h = b->req;
    int len;
    const char *body = (const char *)http_body(h, &len);
    const char *type = http_type(h);
    doc_t *doc = NULL;
    bool html = !type[0] || strcasestr(type, "html") || strcasestr(type, "xml");
    bool text = (type[0] | 0x20) == 't' && (type[1] | 0x20) == 'e' && (type[2] | 0x20) == 'x' &&
                (type[3] | 0x20) == 't' && type[4] == '/';
    if (html)
        doc = html_parse(body ? body : "", len, http_url(h));
    else if (text)
        doc = html_plain(body ? body : "", len, http_url(h));
    if (!doc) {
        b->error_page = true;
        ksnprintf(b->error, sizeof b->error, "Kepler can't show this kind of file (%s).", type);
        strlcpy(b->error_url, http_url(h), sizeof b->error_url);
        cancel(b);
        show_title(w, b);
        wm_invalidate(w);
        return;
    }
    html_free(b->doc);
    b->doc = doc;
    kfree(b->source);
    b->source_len = MIN(len, 2 * 1024 * 1024);
    b->source = kmalloc(b->source_len + 1);
    if (b->source_len)
        memcpy(b->source, body, b->source_len);
    b->error_page = false;
    strlcpy(b->url, http_url(h), sizeof b->url);
    b->secure = http_secure(h);
    b->scroll = 0;
    b->focus_field = -1;
    b->hover_link = -1;
    if (b->req_push)
        history_push(b, b->url);
    else if (b->hist_n)
        b->scroll = b->hist[b->hist_pos].scroll;
    textfield_set(&b->addr, b->url);
    char refresh[1100];
    strlcpy(refresh, doc->refresh, sizeof refresh);
    cancel(b);
    relayout(w, b);
    show_title(w, b);
    wm_set_tick(w, 530);
    wm_invalidate(w);
    if (refresh[0] && strcmp(refresh, b->url))
        navigate(w, b, refresh, false);
}

static void failed(window_t *w, browser_t *b)
{
    b->error_page = true;
    strlcpy(b->error, http_error(b->req), sizeof b->error);
    strlcpy(b->error_url, http_url(b->req), sizeof b->error_url);
    textfield_set(&b->addr, b->error_url);
    cancel(b);
    show_title(w, b);
    wm_set_tick(w, 60);
    wm_invalidate(w);
}

static void go_back(window_t *w, browser_t *b)
{
    if (b->hist_pos <= 0)
        return;
    b->hist[b->hist_pos].scroll = b->scroll;
    b->hist_pos--;
    navigate(w, b, b->hist[b->hist_pos].url, false);
}

static void go_forward(window_t *w, browser_t *b)
{
    if (b->hist_pos + 1 >= b->hist_n)
        return;
    b->hist[b->hist_pos].scroll = b->scroll;
    b->hist_pos++;
    navigate(w, b, b->hist[b->hist_pos].url, false);
}

static void reload(window_t *w, browser_t *b)
{
    if (b->req) {
        cancel(b);
        wm_invalidate(w);
    } else if (b->error_page) {
        navigate(w, b, b->error_url, false);
    } else {
        navigate(w, b, b->url, false);
    }
}

/* ---- painting ---------------------------------------------------------------------------------- */

static uint32_t hue_rgb(int hue, int sat, int light)
{
    /* hsl with s and l in percent */
    int c = (100 - ABS(2 * light - 100)) * sat / 100; /* chroma, percent */
    int hp = hue % 360;
    int x = c * (60 - ABS(hp % 120 - 60)) / 60;
    int r = 0, g = 0, bl = 0;
    if (hp < 60)
        r = c, g = x;
    else if (hp < 120)
        r = x, g = c;
    else if (hp < 180)
        g = c, bl = x;
    else if (hp < 240)
        g = x, bl = c;
    else if (hp < 300)
        r = x, bl = c;
    else
        r = c, bl = x;
    int m = light - c / 2;
    return RGB(CLAMP((r + m) * 255 / 100, 0, 255), CLAMP((g + m) * 255 / 100, 0, 255), CLAMP((bl + m) * 255 / 100, 0, 255));
}

static void paint_toolbar(window_t *w, browser_t *b, gfx_t *g, int cw)
{
    bool act = wm_active() == w;
    gfx_vgrad(g, 0, 0, cw, TOOLBAR_H, act ? HEX(0x191926) : HEX(0x1D1D28), K_BAR);
    gfx_hline(g, 0, TOOLBAR_H - 1, cw, RGBA(255, 255, 255, 16));
    static const int glyphs[4] = { GLYPH_CHEVRON_L, GLYPH_CHEVRON_R, GLYPH_RELOAD, GLYPH_HOME };
    for (int i = 0; i < T_ADDR; i++) {
        rect_t r = tool_rect(cw, i);
        bool en = i == T_BACK ? b->hist_pos > 0 : i == T_FWD ? b->hist_pos + 1 < b->hist_n : true;
        if (en && b->hover_tool == i)
            gfx_round_rect(g, r.x, r.y, r.w, r.h, 6, RGBA(255, 255, 255, b->press_tool == i ? 34 : 20), CORNER_ALL);
        int glyph = i == T_RELOAD && b->req ? GLYPH_CLOSE : glyphs[i];
        int sz = i < T_RELOAD ? 14 : 16;
        gfx_blit_tint(g, r.x + (r.w - sz) / 2, r.y + (r.h - sz) / 2, icon_get(glyph, sz),
                      !en ? HEX(0x4A4A5E) : b->hover_tool == i ? HEX(0xFFFFFF) : HEX(0xB8B8D0));
    }

    rect_t a = tool_rect(cw, T_ADDR);
    gfx_round_rect(g, a.x, a.y, a.w, a.h, a.h / 2, HEX(0x1F1F2E), CORNER_ALL);
    if (b->addr_focus) {
        gfx_round_frame(g, a.x - 1, a.y - 1, a.w + 2, a.h + 2, a.h / 2 + 1, WITH_ALPHA(K_VIOLET, 230), CORNER_ALL);
        gfx_round_frame(g, a.x, a.y, a.w, a.h, a.h / 2, WITH_ALPHA(K_CYAN, 150), CORNER_ALL);
    } else {
        gfx_round_frame(g, a.x, a.y, a.w, a.h, a.h / 2, RGBA(255, 255, 255, b->hover_tool == T_ADDR ? 40 : 22), CORNER_ALL);
    }
    int ty = a.y + (a.h - font_menu.height) / 2;
    if (b->addr_focus) {
        gfx_t ag = gfx_sub(g, a.x + 16, a.y, a.w - 32, a.h);
        int cx = text_width_n(&font_menu, b->addr.text, b->addr.cursor);
        int off = MIN(0, (a.w - 40) - cx);
        if (b->addr_all)
            gfx_round_rect(&ag, off - 2, 6, text_width(&font_menu, b->addr.text) + 4, a.h - 12, 4, WITH_ALPHA(K_VIOLET, 150),
                           CORNER_ALL);
        gfx_text(&ag, &font_menu, off, ty - a.y, b->addr.text, K_TEXT);
        if (!b->addr_all && (timer_ms() / 530) % 2 == 0)
            gfx_fill(&ag, off + cx, 8, 1, a.h - 16, K_CYAN);
    } else if (is_start(b) && !b->req) {
        const char *ph = "Search or enter website name";
        int tw = text_width(&font_menu, ph);
        gfx_text(g, &font_menu, a.x + (a.w - tw) / 2, ty, ph, K_MUTED);
    } else {
        /* the host, centred, with a lock for a verified connection */
        url_t u;
        const char *shown = b->addr.text;
        char host[160];
        if (url_parse(b->addr.text, &u)) {
            strlcpy(host, !strncmp(u.host, "www.", 4) ? u.host + 4 : u.host, sizeof host);
            shown = host;
        }
        bool lock = (b->secure && !b->req && !b->error_page) || (b->req && !strncmp(b->addr.text, "https:", 6));
        int tw = MIN(text_width(&font_menu, shown), a.w - 60);
        int x = a.x + (a.w - tw) / 2 + (lock ? 9 : 0);
        if (lock)
            gfx_blit_tint(g, x - 18, a.y + (a.h - 12) / 2, icon_get(GLYPH_LOCK, 12), b->req ? K_MUTED : HEX(0x7CE3B0));
        gfx_text_clipped(g, &font_menu, x, ty, a.w - 60, shown, K_TEXT);
    }
    if (b->req) {
        int pw = cw * b->progress / 100;
        gfx_hgrad(g, 0, TOOLBAR_H - 2, pw, 2, K_VIOLET, K_CYAN);
    }
}

static void stars(gfx_t *g, int w, int h, uint32_t t)
{
    uint32_t seed = 0x4B45504C;
    int n = w * h / 3000;
    for (int i = 0; i < n; i++) {
        seed = seed * 1103515245 + 12345;
        int x = (seed >> 8) % MAX(1, w);
        seed = seed * 1103515245 + 12345;
        int y = (seed >> 8) % MAX(1, h);
        seed = seed * 1103515245 + 12345;
        int base = 60 + (seed >> 16) % 150;
        int phase = (int)((t / 12 + (seed >> 4) % 512) % 512);
        int tw = phase < 256 ? phase : 511 - phase;
        int a = CLAMP(base - 40 + tw * 80 / 255, 20, 255);
        uint32_t tint = (seed >> 24) % 5 == 0 ? HEX(0xC9B8FF) : (seed >> 24) % 5 == 1 ? HEX(0xB8F0FF) : HEX(0xFFFFFF);
        gfx_fill(g, x, y, 1, 1, WITH_ALPHA(tint, a));
        if (base > 190) {
            gfx_fill(g, x - 1, y, 3, 1, WITH_ALPHA(tint, a / 3));
            gfx_fill(g, x, y - 1, 1, 3, WITH_ALPHA(tint, a / 3));
        }
    }
    /* a shooting star every few seconds */
    uint32_t cycle = t % 6000;
    if (cycle < 900) {
        int sx = w * 3 / 4 - (int)cycle * w / 1500, sy = h / 8 + (int)cycle * h / 3000;
        for (int k = 0; k < 70; k++) {
            int a = 220 - k * 3;
            if (a <= 0)
                break;
            gfx_fill(g, sx + k * 2, sy - k, 2, 1, RGBA(255, 255, 255, a));
        }
    }
}

/* a pill filled left to right from one colour to another */
static void grad_pill(gfx_t *g, rect_t r, uint32_t c0, uint32_t c1)
{
    const int strips = 24;
    for (int k = 0; k < strips; k++) {
        int x0 = r.w * k / strips, x1 = r.w * (k + 1) / strips;
        gfx_t strip = gfx_sub(g, r.x + x0, r.y, x1 - x0, r.h);
        gfx_round_rect(&strip, -x0, 0, r.w, r.h, r.h / 2, color_lerp(c0, c1, k * 256 / (strips - 1)), CORNER_ALL);
    }
}

static void paint_start(window_t *w, browser_t *b, gfx_t *g, int cw, int ch)
{
    gfx_fill(g, 0, 0, cw, ch, K_VOID);
    stars(g, cw, ch, timer_ms() - b->anim);
    int top = start_top(ch);
    const image_t *lg = logo();
    if (lg)
        gfx_blit_alpha(g, (cw - lg->w) / 2, top - 4, lg, 255);

    /* K E P L E R */
    const char *title = "KEPLER";
    int sp = 16, tw = -sp;
    for (const char *c = title; *c; c++)
        tw += char_width(&font_display, *c) + sp;
    int x = (cw - tw) / 2, ty = top + 176;
    for (const char *c = title; *c; c++) {
        char s[2] = { *c, 0 };
        gfx_text(g, &font_display, x, ty, s, HEX(0xFFFFFF));
        x += char_width(&font_display, *c) + sp;
    }
    datetime_t dt;
    rtc_read(&dt);
    const char *part = dt.hour < 5 ? "Stargazing late" : dt.hour < 12 ? "Good morning" : dt.hour < 18 ? "Good afternoon" : "Good evening";
    char greet[64];
    ksnprintf(greet, sizeof greet, "%s, ", part);
    int gw = text_width(&font_menu, greet) + text_width(&font_menu_bold, "explorer");
    int gx = gfx_text(g, &font_menu, (cw - gw) / 2, ty + 44, greet, K_MUTED);
    gfx_text(g, &font_menu_bold, gx, ty + 44, "explorer", K_TEXT);

    rect_t s = search_rect(cw, ch);
    bool focus = !b->addr_focus;
    if (focus) {
        for (int k = 3; k >= 1; k--)
            gfx_round_frame(g, s.x - k, s.y - k, s.w + 2 * k, s.h + 2 * k, s.h / 2 + k, WITH_ALPHA(K_VIOLET, 40 * (4 - k)), CORNER_ALL);
    }
    gfx_round_rect(g, s.x, s.y, s.w, s.h, s.h / 2, HEX(0x0C0C16), CORNER_ALL);
    if (focus) {
        /* a violet-to-cyan ring, drawn as narrow clipped strips */
        const int strips = 40;
        for (int k = 0; k < strips; k++) {
            int x0 = s.w * k / strips, x1 = s.w * (k + 1) / strips;
            gfx_t strip = gfx_sub(g, s.x + x0, s.y, x1 - x0, s.h);
            uint32_t c = color_lerp(K_VIOLET, K_CYAN, k * 256 / (strips - 1));
            gfx_round_frame(&strip, -x0, 0, s.w, s.h, s.h / 2, WITH_ALPHA(c, 230), CORNER_ALL);
        }
    } else {
        gfx_round_frame(g, s.x, s.y, s.w, s.h, s.h / 2, RGBA(255, 255, 255, 22), CORNER_ALL);
    }
    gfx_blit_tint(g, s.x + 20, s.y + (s.h - 18) / 2, icon_get(GLYPH_SEARCH, 18), HEX(0xB8B8D0));
    gfx_t field = gfx_sub(g, s.x + 50, s.y, s.w - 100, s.h);
    int fy = (s.h - font_heading.height) / 2;
    if (b->search.text[0]) {
        int cx = text_width_n(&font_heading, b->search.text, b->search.cursor);
        int off = MIN(0, (s.w - 110) - cx);
        gfx_text(&field, &font_heading, off, fy, b->search.text, K_TEXT);
        if (focus && (timer_ms() / 530) % 2 == 0)
            gfx_fill(&field, off + cx + 1, 14, 2, s.h - 28, K_CYAN);
    } else {
        gfx_text(&field, &font_menu, 0, (s.h - font_menu.height) / 2, "Search with DuckDuckGo or enter an address", HEX(0x6F6F8C));
        if (focus && (timer_ms() / 530) % 2 == 0)
            gfx_fill(&field, 0, 14, 2, s.h - 28, K_CYAN);
    }
    gfx_round_frame(g, s.x + s.w - 40, s.y + 14, 20, 20, 5, RGBA(255, 255, 255, 30), CORNER_ALL);
    gfx_text_centered(g, &font_ui, s.x + s.w - 40, s.y + 16, 20, "/", K_MUTED);

    for (int i = 0; i < ARRAY_LEN(tiles); i++) {
        rect_t r = tile_rect(cw, ch, i);
        bool hot = b->hover_tile == i;
        if (hot)
            r.y -= 3;
        gfx_round_rect(g, r.x, r.y, r.w, r.h, 18, hot ? RGBA(36, 30, 62, 230) : RGBA(22, 22, 36, 220), CORNER_ALL);
        gfx_round_frame(g, r.x, r.y, r.w, r.h, 18, hot ? WITH_ALPHA(K_VIOLET, 170) : RGBA(255, 255, 255, 20), CORNER_ALL);
        uint32_t hash = 0;
        for (const char *c = tiles[i].url; *c; c++)
            hash = hash * 31 + (uint8_t)*c;
        int hue = hash % 360;
        int ox = r.x + r.w / 2, oy = r.y + 36;
        gfx_circle(g, ox, oy + 2, 23, RGBA(0, 0, 0, 90));
        gfx_circle(g, ox, oy, 22, hue_rgb(hue + 40, 70, 38));
        gfx_circle(g, ox - 3, oy - 3, 17, hue_rgb(hue, 90, 62));
        char letter[2] = { tiles[i].title[0], 0 };
        gfx_text_centered(g, &font_heading, ox - 22, oy - font_heading.height / 2, 44, letter, HEX(0xFFFFFF));
        gfx_text_centered(g, &font_ui, r.x, r.y + 72, r.w, tiles[i].title, K_TEXT);
    }

    /* the shields pill */
    const char *pill_a = "Shields up", *pill_b = "  no scripts, no trackers";
    int pw = 16 + 14 + 6 + text_width(&font_ui_bold, pill_a) + text_width(&font_ui, pill_b) + 16;
    int px = (cw - pw) / 2, py = ch - 46;
    gfx_round_rect(g, px, py, pw, 28, 14, RGBA(22, 22, 36, 220), CORNER_ALL);
    gfx_round_frame(g, px, py, pw, 28, 14, RGBA(255, 255, 255, 20), CORNER_ALL);
    gfx_blit_tint(g, px + 14, py + 7, icon_get(GLYPH_SHIELD, 14), K_CYAN);
    int tx = gfx_text(g, &font_ui_bold, px + 36, py + (28 - font_ui.height) / 2, pill_a, K_CYAN);
    gfx_text(g, &font_ui, tx, py + (28 - font_ui.height) / 2, pill_b, K_MUTED);
}

static void paint_error(window_t *w, browser_t *b, gfx_t *g, int cw, int ch)
{
    gfx_fill(g, 0, 0, cw, ch, K_VOID);
    stars(g, cw, ch, timer_ms() - b->anim);
    gfx_blit_tint(g, cw / 2 - 32, ch / 2 - 130, icon_get(GLYPH_KEPLER, 64), RGBA(255, 255, 255, 120));
    gfx_text_centered(g, &font_big_bold, 0, ch / 2 - 50, cw, "Kepler can't open this page", HEX(0xFFFFFF));
    gfx_t t = gfx_sub(g, 60, ch / 2 - 10, cw - 120, 60);
    int tw = text_width(&font_menu, b->error);
    gfx_text_clipped(&t, &font_menu, MAX(0, (cw - 120 - tw) / 2), 0, cw - 120, b->error, HEX(0xB8B8D0));
    int uw = text_width(&font_ui, b->error_url);
    gfx_text_clipped(&t, &font_ui, MAX(0, (cw - 120 - uw) / 2), 26, cw - 120, b->error_url, K_MUTED);
    if (!net_ready()) {
        char s[96];
        ksnprintf(s, sizeof s, "Network: %s", net_status());
        gfx_text_centered(g, &font_ui, 0, ch / 2 + 44, cw, s, K_MUTED);
    }
    rect_t r = retry_rect(cw, ch);
    grad_pill(g, r, b->hover_retry ? HEX(0x9A6CFA) : HEX(0x7C4FEA), b->hover_retry ? HEX(0x3BC4EE) : HEX(0x2BA9D6));
    gfx_text_centered(g, &font_menu_bold, r.x, r.y + (r.h - font_menu_bold.height) / 2, r.w, "Try Again", HEX(0xFFFFFF));
}

static void br_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    browser_t *b = w->data;
    rect_t c = content_rect(cw, ch);
    gfx_t cg = gfx_sub(g, c.x, c.y, c.w, c.h);
    if (b->error_page)
        paint_error(w, b, &cg, c.w, c.h);
    else if (is_start(b))
        paint_start(w, b, &cg, c.w, c.h);
    else if (b->doc) {
        gfx_fill(&cg, 0, 0, c.w, c.h, HEX(0xFFFFFF));
        gfx_t pg = gfx_sub(&cg, 0, 0, c.w - SCROLLBAR_W, c.h);
        html_paint(b->doc, &b->lay, &pg, b->scroll, c.h, b->hover_link, b->focus_field, (timer_ms() / 530) % 2 == 0);
        b->sb.total = b->lay.height;
        b->sb.visible = c.h;
        b->sb.pos = b->scroll;
        scrollbar_paint(&b->sb, &cg, c.w - SCROLLBAR_W, 0, c.h);
        /* where a hovered link goes */
        if (b->hover_link >= 0) {
            const char *u = html_link(b->doc, b->hover_link);
            int tw = MIN(text_width(&font_ui, u), c.w / 2);
            gfx_round_rect(&cg, 6, c.h - 30, tw + 20, 24, 7, RGBA(28, 28, 40, 235), CORNER_ALL);
            gfx_text_clipped(&cg, &font_ui, 16, c.h - 30 + (24 - font_ui.height) / 2, c.w / 2, u, K_TEXT);
        }
    } else {
        gfx_fill(&cg, 0, 0, c.w, c.h, HEX(0xFFFFFF));
    }
    if (b->req) {
        const char *st = http_stage(b->req);
        int tw = text_width(&font_ui, st);
        gfx_round_rect(&cg, 6, c.h - 30, tw + 20, 24, 7, RGBA(28, 28, 40, 235), CORNER_ALL);
        gfx_text(&cg, &font_ui, 16, c.h - 30 + (24 - font_ui.height) / 2, st, K_TEXT);
    }
    paint_toolbar(w, b, g, cw);
}

/* ---- input ------------------------------------------------------------------------------------------ */

static int frag_at(browser_t *b, int x, int y)
{
    for (int i = 0; i < b->lay.n; i++) {
        frag_t *f = &b->lay.frags[i];
        if (x >= f->x && x < f->x + f->w && y >= f->y - 2 && y < f->y + f->h + 2)
            return i;
    }
    return -1;
}

static void submit(window_t *w, browser_t *b, int field)
{
    if (field < 0 || field >= b->doc->n_fields)
        return;
    int form = b->doc->fields[field].form;
    if (form < 0)
        return;
    char url[1100];
    html_form_url(b->doc, form, url, sizeof url);
    navigate(w, b, url, true);
}

static void set_scroll(window_t *w, browser_t *b, int pos)
{
    rect_t c = wm_client_rect(w);
    int max = MAX(0, b->lay.height - (c.h - TOOLBAR_H));
    pos = CLAMP(pos, 0, max);
    if (pos != b->scroll) {
        b->scroll = pos;
        wm_invalidate_client_rect(w, 0, TOOLBAR_H, c.w, c.h - TOOLBAR_H);
    }
}

static bool br_caption_hit(window_t *w, int x, int y)
{
    rect_t c = wm_client_rect(w);
    for (int i = 0; i < N_TOOLS; i++)
        if (rect_contains(tool_rect(c.w, i), x, y))
            return true;
    return false;
}

static void br_mouse(window_t *w, int kind, int x, int y, int button)
{
    browser_t *b = w->data;
    rect_t c = wm_client_rect(w);
    rect_t cr = content_rect(c.w, c.h);
    int tool = -1;
    for (int i = 0; i < N_TOOLS; i++)
        if (kind != MS_LEAVE && rect_contains(tool_rect(c.w, i), x, y))
            tool = i;
    if (tool != b->hover_tool) {
        b->hover_tool = tool;
        wm_invalidate_client_rect(w, 0, 0, c.w, TOOLBAR_H);
    }
    w->cursor = tool == T_ADDR ? CURSOR_IBEAM : CURSOR_ARROW;
    int lx = x - cr.x, ly = y - cr.y;
    bool in_content = kind != MS_LEAVE && rect_contains(cr, x, y);

    /* page scrollbar */
    if (b->doc && !is_start(b) && !b->error_page &&
        (b->sb.drag_off >= 0 || (in_content && lx >= cr.w - SCROLLBAR_W) || kind == MS_WHEEL)) {
        if (kind == MS_WHEEL) {
            set_scroll(w, b, b->scroll + button * 48);
            return;
        }
        if (scrollbar_mouse(&b->sb, kind, lx, ly, cr.w - SCROLLBAR_W, 0, cr.h)) {
            set_scroll(w, b, b->sb.pos);
            wm_invalidate_client(w);
        }
        if (b->sb.drag_off >= 0 || lx >= cr.w - SCROLLBAR_W)
            return;
    }

    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        b->press_tool = tool;
        if (tool == T_ADDR) {
            if (!b->addr_focus)
                focus_addr(w, b);
            else
                b->addr_all = false;
            wm_invalidate_client(w);
            return;
        }
        if (b->addr_focus && tool < 0) {
            b->addr_focus = false;
            textfield_set(&b->addr, b->error_page ? b->error_url : is_start(b) ? "" : b->url);
            wm_invalidate_client(w);
        }
    }
    if (kind == MS_UP && button == MOUSE_LEFT) {
        int p = b->press_tool;
        b->press_tool = -1;
        if (p >= 0 && p == tool) {
            if (p == T_BACK)
                go_back(w, b);
            else if (p == T_FWD)
                go_forward(w, b);
            else if (p == T_RELOAD)
                reload(w, b);
            else if (p == T_HOME)
                go_start(w, b, true);
        }
        wm_invalidate_client_rect(w, 0, 0, c.w, TOOLBAR_H);
    }
    if (!in_content && kind != MS_LEAVE)
        return;

    if (b->error_page) {
        bool hot = in_content && rect_contains(retry_rect(cr.w, cr.h), lx, ly);
        if (hot != b->hover_retry) {
            b->hover_retry = hot;
            wm_invalidate_client(w);
        }
        if (hot)
            w->cursor = CURSOR_HAND;
        if (kind == MS_UP && button == MOUSE_LEFT && hot)
            reload(w, b);
        return;
    }
    if (is_start(b)) {
        int t = -1;
        for (int i = 0; i < ARRAY_LEN(tiles) && in_content; i++)
            if (rect_contains(tile_rect(cr.w, cr.h, i), lx, ly))
                t = i;
        if (t != b->hover_tile) {
            b->hover_tile = t;
            wm_invalidate_client(w);
        }
        if (t >= 0)
            w->cursor = CURSOR_HAND;
        else if (in_content && rect_contains(search_rect(cr.w, cr.h), lx, ly))
            w->cursor = CURSOR_IBEAM;
        if (kind == MS_DOWN && button == MOUSE_LEFT)
            b->press_tile = t;
        if (kind == MS_UP && button == MOUSE_LEFT && t >= 0 && t == b->press_tile)
            navigate(w, b, tiles[t].url, true);
        return;
    }
    if (!b->doc)
        return;
    int fi = in_content ? frag_at(b, lx, ly + b->scroll) : -1;
    frag_t *f = fi >= 0 ? &b->lay.frags[fi] : NULL;
    int link = f ? f->link : -1;
    if (f && f->kind == IT_IMAGE)
        link = b->doc->items[f->item].link;
    if (link != b->hover_link) {
        b->hover_link = link;
        wm_invalidate_client_rect(w, 0, TOOLBAR_H, c.w, c.h - TOOLBAR_H);
    }
    if (link >= 0 || (f && f->kind == IT_BUTTON))
        w->cursor = CURSOR_HAND;
    else if (f && f->kind == IT_INPUT)
        w->cursor = CURSOR_IBEAM;
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        b->press_link = link;
        int field = f && (f->kind == IT_INPUT) ? b->doc->items[f->item].field : -1;
        if (field != b->focus_field) {
            b->focus_field = field;
            wm_invalidate_client(w);
        }
        if (f && f->kind == IT_BUTTON)
            submit(w, b, b->doc->items[f->item].field);
    }
    if (kind == MS_UP && button == MOUSE_LEFT && link >= 0 && link == b->press_link) {
        char url[1100];
        strlcpy(url, html_link(b->doc, link), sizeof url);
        b->press_link = -1;
        navigate(w, b, url, true);
    }
}

static bool field_key(browser_t *b, const input_event_t *ev)
{
    field_t *f = &b->doc->fields[b->focus_field];
    int n = strlen(f->value);
    if (ev->key == KEY_BACKSPACE) {
        if (n)
            f->value[n - 1] = 0;
        return true;
    }
    if (ev->key >= 32 && ev->key < 127 && !(ev->mods & (MOD_CTRL | MOD_ALT)) && n < (int)sizeof f->value - 1) {
        f->value[n] = (char)ev->key;
        f->value[n + 1] = 0;
        return true;
    }
    return false;
}

static void br_key(window_t *w, const input_event_t *ev)
{
    browser_t *b = w->data;
    if (!ev->pressed)
        return;
    rect_t c = wm_client_rect(w);
    int page = c.h - TOOLBAR_H - 40;
    if (ev->mods & MOD_CTRL) {
        switch (ktolower(ev->key)) {
        case 'l': focus_addr(w, b); return;
        case 'r': reload(w, b); return;
        case '[': go_back(w, b); return;
        case ']': go_forward(w, b); return;
        case 'n': browser_open(NULL); return;
        case 'w': wm_close(w); return;
        }
    }
    if (b->addr_focus) {
        if (ev->key == KEY_ENTER) {
            char t[512];
            strlcpy(t, b->addr.text, sizeof t);
            b->addr_focus = false;
            go_input(w, b, t);
        } else if (ev->key == KEY_ESC) {
            b->addr_focus = false;
            textfield_set(&b->addr, is_start(b) ? "" : b->url);
        } else {
            bool printable = ev->key >= 32 && ev->key < 127 && !(ev->mods & (MOD_CTRL | MOD_ALT));
            if (b->addr_all && (printable || ev->key == KEY_BACKSPACE || ev->key == KEY_DELETE))
                textfield_set(&b->addr, "");
            if (!b->addr_all || printable || (ev->key != KEY_BACKSPACE && ev->key != KEY_DELETE))
                textfield_key(&b->addr, ev);
            b->addr_all = false;
        }
        wm_invalidate_client(w);
        return;
    }
    if (is_start(b)) {
        if (ev->key == KEY_ENTER) {
            char t[256];
            strlcpy(t, b->search.text, sizeof t);
            go_input(w, b, t);
        } else if (ev->key == KEY_ESC) {
            textfield_set(&b->search, "");
        } else if (ev->key != '/' || b->search.text[0]) {
            textfield_key(&b->search, ev);
        }
        wm_invalidate_client(w);
        return;
    }
    if (b->error_page) {
        if (ev->key == KEY_ENTER)
            reload(w, b);
        else if (ev->key == KEY_BACKSPACE)
            go_back(w, b);
        return;
    }
    if (b->doc && b->focus_field >= 0) {
        if (ev->key == KEY_ENTER) {
            submit(w, b, b->focus_field);
            return;
        }
        if (ev->key == KEY_ESC) {
            b->focus_field = -1;
        } else if (field_key(b, ev)) {
            wm_invalidate_client(w);
            return;
        }
        wm_invalidate_client(w);
        return;
    }
    switch (ev->key) {
    case KEY_UP: set_scroll(w, b, b->scroll - 40); break;
    case KEY_DOWN: set_scroll(w, b, b->scroll + 40); break;
    case KEY_PGUP: set_scroll(w, b, b->scroll - page); break;
    case KEY_PGDN:
    case ' ': set_scroll(w, b, b->scroll + ((ev->mods & MOD_SHIFT) ? -page : page)); break;
    case KEY_HOME: set_scroll(w, b, 0); break;
    case KEY_END: set_scroll(w, b, b->lay.height); break;
    case KEY_BACKSPACE: go_back(w, b); break;
    }
}

static void br_tick(window_t *w)
{
    browser_t *b = w->data;
    if (b->req) {
        int r = http_poll(b->req);
        if (r == HTTP_DONE) {
            loaded(w, b);
            return;
        }
        if (r == HTTP_ERROR) {
            failed(w, b);
            return;
        }
        int target = http_received(b->req) ? 90 : 70;
        b->progress += MAX(1, (target - b->progress) / 25);
        b->progress = MIN(b->progress, 95);
        wm_invalidate(w);
        return;
    }
    /* the starfield twinkles; elsewhere only carets blink */
    if (is_start(b) || b->error_page) {
        wm_set_tick(w, 70);
        wm_invalidate_client(w);
    } else if (b->focus_field >= 0 || b->addr_focus) {
        wm_set_tick(w, 530);
        wm_invalidate_client(w);
    } else if (wm_active() == w && b->addr_focus) {
        wm_invalidate_client(w);
    }
}

static void br_resized(window_t *w)
{
    browser_t *b = w->data;
    relayout(w, b);
}

static void br_close(window_t *w)
{
    browser_t *b = w->data;
    cancel(b);
    html_free(b->doc);
    html_layout_free(&b->lay);
    kfree(b->source);
    kfree(b);
}

/* ---- menus ------------------------------------------------------------------------------------------- */

enum { BM_NEW = 1, BM_OPEN, BM_CLOSE, BM_RELOAD, BM_STOP, BM_BACK, BM_FWD, BM_HOME, BM_ABOUT, BM_SOURCE, BM_TILE0 = 100 };

static const menu_item_t file_menu[] = {
    { "New Window", BM_NEW, 0, 0, NULL, 0, "Ctrl+N" },
    { "Open Location...", BM_OPEN, 0, 0, NULL, 0, "Ctrl+L" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Close Window", BM_CLOSE, 0, 0, NULL, 0, "Ctrl+W" },
};
static const menu_item_t view_menu[] = {
    { "Reload Page", BM_RELOAD, 0, 0, NULL, 0, "Ctrl+R" },
    { "Stop", BM_STOP, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "View Source", BM_SOURCE, 0, 0, NULL, 0, NULL },
};
static const menu_item_t history_menu[] = {
    { "Back", BM_BACK, 0, 0, NULL, 0, "Ctrl+[" },
    { "Forward", BM_FWD, 0, 0, NULL, 0, "Ctrl+]" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Start Page", BM_HOME, 0, 0, NULL, 0, NULL },
};
static const menu_item_t bookmarks_menu[] = {
    { "Wikipedia", BM_TILE0 + 0, 0, 0, NULL, 0, NULL },  { "DuckDuckGo", BM_TILE0 + 1, 0, 0, NULL, 0, NULL },
    { "Hacker News", BM_TILE0 + 2, 0, 0, NULL, 0, NULL }, { "NPR News", BM_TILE0 + 3, 0, 0, NULL, 0, NULL },
    { "CNN Lite", BM_TILE0 + 4, 0, 0, NULL, 0, NULL },    { "KeplerOS", BM_TILE0 + 5, 0, 0, NULL, 0, NULL },
};
static const menu_item_t help_menu[] = {
    { "About Kepler", BM_ABOUT, 0, 0, NULL, 0, NULL },
};
static const menubar_entry_t bar[] = {
    { "File", file_menu, ARRAY_LEN(file_menu) },          { "View", view_menu, ARRAY_LEN(view_menu) },
    { "History", history_menu, ARRAY_LEN(history_menu) }, { "Bookmarks", bookmarks_menu, ARRAY_LEN(bookmarks_menu) },
    { "Help", help_menu, ARRAY_LEN(help_menu) },
};

/* the page's source, saved and opened in Notepad */
static void view_source(browser_t *b)
{
    if (!b->doc || !b->source)
        return;
    fnode_t *f = fs_create("/Downloads/page-source.txt", false);
    if (!f || !fs_write(f, b->source, b->source_len))
        return;
    kprintf("kepler: source saved, %d bytes\n", b->source_len);
    notepad_open("/Downloads/page-source.txt");
}

static void command(int id, void *ctx)
{
    window_t *w = ctx;
    if (!wm_alive(w))
        return;
    browser_t *b = w->data;
    switch (id) {
    case BM_NEW: browser_open(NULL); break;
    case BM_OPEN: focus_addr(w, b); break;
    case BM_CLOSE: wm_close(w); break;
    case BM_RELOAD: navigate(w, b, b->error_page ? b->error_url : b->url, false); break;
    case BM_STOP:
        cancel(b);
        wm_invalidate(w);
        break;
    case BM_BACK: go_back(w, b); break;
    case BM_FWD: go_forward(w, b); break;
    case BM_HOME: go_start(w, b, true); break;
    case BM_SOURCE: view_source(b); break;
    case BM_ABOUT:
        msgbox(w, "Kepler 1.0", "A web browser from another orbit, for KeplerOS. Pages are fetched over HTTPS with BearSSL; scripts and trackers never load.", ICON_BROWSER);
        break;
    default:
        if (id >= BM_TILE0 && id < BM_TILE0 + ARRAY_LEN(tiles))
            navigate(w, b, tiles[id - BM_TILE0].url, true);
    }
}

static const window_ops_t br_ops = { br_paint, br_key, br_mouse, br_tick, br_close, br_resized, br_caption_hit };

window_t *browser_open(const char *url)
{
    browser_t *b = kzalloc(sizeof *b);
    b->focus_field = b->hover_link = b->press_link = b->hover_tool = b->press_tool = b->hover_tile = b->press_tile = -1;
    b->sb.drag_off = -1;
    b->anim = timer_ms();
    b->mb.entries = bar;
    b->mb.n = ARRAY_LEN(bar);
    b->mb.open = b->mb.hover = -1;
    strlcpy(b->url, START_URL, sizeof b->url);
    window_t *w = wm_create("Kepler", ICON_BROWSER, 1000, 620, &br_ops, b, WF_RESIZABLE | WF_UNIFIED);
    w->caption_h = TOOLBAR_H;
    w->min_w = 560;
    w->min_h = 380;
    w->bg = K_VOID;
    wm_set_app(w, "Kepler", &b->mb, command, w);
    history_push(b, START_URL);
    show_title(w, b);
    wm_set_tick(w, 70);
    if (url && *url)
        go_input(w, b, url);
    return w;
}
