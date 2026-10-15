/* Calculator, standard view. */

#include "apps.h"

enum {
    K_0 = 0, /* 0..9 are the digits */
    K_DOT = 10,
    K_ADD,
    K_SUB,
    K_MUL,
    K_DIV,
    K_EQ,
    K_SQRT,
    K_PCT,
    K_INV,
    K_NEG,
    K_BACK,
    K_CE,
    K_C,
    K_MC,
    K_MR,
    K_MS,
    K_MPLUS,
};

typedef struct {
    int key;
    const char *label;
    int x, y, w, h;
    bool red;
} ckey_t;

typedef struct {
    char entry[32];
    double acc, mem;
    int op;
    bool entering, error, has_mem;
    int hover, pressed;
    menubar_t mb;
} calc_t;

#define BW 36
#define BH 29
#define GX 52
#define GY (MENUBAR_H + 74)
#define SX 40
#define SY 33

static ckey_t keys[28];
static int n_keys;

static void add_key(int key, const char *label, int x, int y, int w, int h, bool red)
{
    keys[n_keys++] = (ckey_t){ key, label, x, y, w, h, red };
}

static void layout(void)
{
    if (n_keys)
        return;
    int top = MENUBAR_H + 38;
    add_key(K_BACK, "Backspace", GX, top, 64, BH, true);
    add_key(K_CE, "CE", GX + 68, top, 64, BH, true);
    add_key(K_C, "C", GX + 136, top, 64, BH, true);
    static const int mem[4] = { K_MC, K_MR, K_MS, K_MPLUS };
    static const char *mem_l[4] = { "MC", "MR", "MS", "M+" };
    static const int grid[4][5] = {
        { 7, 8, 9, K_DIV, K_SQRT },
        { 4, 5, 6, K_MUL, K_PCT },
        { 1, 2, 3, K_SUB, K_INV },
        { 0, K_NEG, K_DOT, K_ADD, K_EQ },
    };
    static const char *labels[4][5] = {
        { "7", "8", "9", "/", "sqrt" },
        { "4", "5", "6", "*", "%" },
        { "1", "2", "3", "-", "1/x" },
        { "0", "+/-", ".", "+", "=" },
    };
    for (int r = 0; r < 4; r++) {
        add_key(mem[r], mem_l[r], 8, GY + r * SY, BW, BH, true);
        for (int c = 0; c < 5; c++) {
            int k = grid[r][c];
            bool red = k == K_DIV || k == K_MUL || k == K_SUB || k == K_ADD || k == K_EQ;
            add_key(k, labels[r][c], GX + c * SX + (c >= 3 ? 4 : 0), GY + r * SY, BW, BH, red);
        }
    }
}

/* ---- number formatting -------------------------------------------------------------------------------- */

static double fsqrt(double x)
{
    double r;
    __asm__("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

static double pow10i(int n)
{
    double p = 1;
    while (n-- > 0)
        p *= 10;
    return p;
}

static void fmt_num(double v, char *out, int n)
{
    if (v != v || v > 1e300 || v < -1e300) {
        strlcpy(out, "Cannot divide by zero.", n);
        return;
    }
    char buf[48];
    int p = 0;
    if (v < 0) {
        buf[p++] = '-';
        v = -v;
    }
    if (v != 0 && (v >= 1e16 || v < 1e-10)) {
        int e = 0;
        while (v >= 10) {
            v /= 10;
            e++;
        }
        while (v < 1) {
            v *= 10;
            e--;
        }
        uint64_t m = (uint64_t)(v * 1e11 + 0.5);
        if (m >= 1000000000000ull) {
            m /= 10;
            e++;
        }
        char digits[16];
        ksnprintf(digits, sizeof digits, "%llu", (unsigned long long)m);
        int len = strlen(digits);
        while (len > 1 && digits[len - 1] == '0')
            len--;
        buf[p++] = digits[0];
        buf[p++] = '.';
        for (int i = 1; i < len; i++)
            buf[p++] = digits[i];
        p += ksnprintf(buf + p, sizeof buf - p, "e%c%d", e < 0 ? '-' : '+', ABS(e));
        buf[p] = 0;
        strlcpy(out, buf, n);
        return;
    }
    uint64_t ip = (uint64_t)v;
    double frac = v - (double)ip;
    char ibuf[24];
    ksnprintf(ibuf, sizeof ibuf, "%llu", (unsigned long long)ip);
    int idig = ip ? strlen(ibuf) : 0;
    int places = MAX(0, 16 - MAX(idig, 1));
    uint64_t scale = (uint64_t)pow10i(places);
    uint64_t f = (uint64_t)(frac * (double)scale + 0.5);
    if (f >= scale) {
        ip++;
        f -= scale;
        ksnprintf(ibuf, sizeof ibuf, "%llu", (unsigned long long)ip);
    }
    p += ksnprintf(buf + p, sizeof buf - p, "%s.", ibuf);
    if (f) {
        char fbuf[24];
        ksnprintf(fbuf, sizeof fbuf, "%0*llu", places, (unsigned long long)f);
        int len = strlen(fbuf);
        while (len > 0 && fbuf[len - 1] == '0')
            len--;
        fbuf[len] = 0;
        strlcat(buf, fbuf, sizeof buf);
    }
    strlcpy(out, buf, n);
}

static double parse(const char *s)
{
    double v = 0, scale = 0;
    bool neg = *s == '-';
    if (neg)
        s++;
    for (; *s; s++) {
        if (*s == '.') {
            scale = 1;
            continue;
        }
        if (!kisdigit(*s))
            break;
        v = v * 10 + (*s - '0');
        if (scale)
            scale *= 10;
    }
    if (scale)
        v /= scale;
    return neg ? -v : v;
}

/* ---- logic -------------------------------------------------------------------------------------------- */

static void show(calc_t *c, double v)
{
    fmt_num(v, c->entry, sizeof c->entry);
    if (v != v || v > 1e300 || v < -1e300)
        c->error = true;
}

static double apply(int op, double a, double b)
{
    switch (op) {
    case K_ADD: return a + b;
    case K_SUB: return a - b;
    case K_MUL: return a * b;
    case K_DIV: return b == 0 ? (0.0 / 0.0) : a / b;
    }
    return b;
}

static void press(calc_t *c, int k)
{
    if (c->error && k != K_C && k != K_CE)
        return;
    double cur = parse(c->entry);
    if (k <= 9 || k == K_DOT) {
        if (!c->entering) {
            strlcpy(c->entry, "0.", sizeof c->entry);
            c->entering = true;
        }
        int len = strlen(c->entry);
        if (len >= 18)
            return;
        if (k == K_DOT) {
            /* the trailing dot is already there; mark the decimal part as started */
            if (!strchr(c->entry, ',')) {
                strlcat(c->entry, ",", sizeof c->entry);
            }
            return;
        }
        /* entry is kept as "<int>." or "<int>.<frac>" with a ',' marker while typing decimals */
        char *comma = strchr(c->entry, ',');
        if (comma) {
            *comma = 0;
            char d[2] = { (char)('0' + k), 0 };
            strlcat(c->entry, d, sizeof c->entry);
            strlcat(c->entry, ",", sizeof c->entry);
        } else {
            char *dot = strchr(c->entry, '.');
            *dot = 0;
            if (!strcmp(c->entry, "0"))
                c->entry[0] = 0;
            else if (!strcmp(c->entry, "-0"))
                c->entry[1] = 0;
            char d[3] = { (char)('0' + k), '.', 0 };
            strlcat(c->entry, d, sizeof c->entry);
        }
        return;
    }
    switch (k) {
    case K_ADD:
    case K_SUB:
    case K_MUL:
    case K_DIV:
        if (c->op && c->entering)
            c->acc = apply(c->op, c->acc, cur);
        else if (c->entering || !c->op)
            c->acc = cur;
        c->op = k;
        c->entering = false;
        show(c, c->acc);
        break;
    case K_EQ:
        if (c->op) {
            c->acc = apply(c->op, c->acc, cur);
            c->op = 0;
            show(c, c->acc);
        }
        c->entering = false;
        break;
    case K_SQRT:
        if (cur < 0) {
            strlcpy(c->entry, "Invalid input for function.", sizeof c->entry);
            c->error = true;
        } else {
            show(c, fsqrt(cur));
        }
        c->entering = false;
        break;
    case K_PCT:
        show(c, c->acc * cur / 100);
        c->entering = false;
        break;
    case K_INV:
        if (cur == 0) {
            strlcpy(c->entry, "Cannot divide by zero.", sizeof c->entry);
            c->error = true;
        } else {
            show(c, 1 / cur);
        }
        c->entering = false;
        break;
    case K_NEG:
        if (c->entry[0] == '-')
            memmove(c->entry, c->entry + 1, strlen(c->entry));
        else if (strcmp(c->entry, "0.")) {
            memmove(c->entry + 1, c->entry, strlen(c->entry) + 1);
            c->entry[0] = '-';
        }
        break;
    case K_BACK:
        if (c->entering) {
            char *comma = strchr(c->entry, ',');
            int len = strlen(c->entry);
            if (comma) {
                /* "12.34," -> "12.3," ; "12.," -> "12." */
                *comma = 0;
                len = strlen(c->entry);
                if (c->entry[len - 1] != '.') {
                    c->entry[len - 1] = 0;
                    strlcat(c->entry, ",", sizeof c->entry);
                }
            } else if (len > 2) {
                c->entry[len - 2] = '.';
                c->entry[len - 1] = 0;
                if (!strcmp(c->entry, "-."))
                    strlcpy(c->entry, "0.", sizeof c->entry);
            } else {
                strlcpy(c->entry, "0.", sizeof c->entry);
            }
        }
        break;
    case K_CE:
        strlcpy(c->entry, "0.", sizeof c->entry);
        c->error = false;
        c->entering = false;
        break;
    case K_C:
        strlcpy(c->entry, "0.", sizeof c->entry);
        c->acc = 0;
        c->op = 0;
        c->error = false;
        c->entering = false;
        break;
    case K_MC:
        c->mem = 0;
        c->has_mem = false;
        break;
    case K_MR:
        show(c, c->mem);
        c->entering = false;
        break;
    case K_MS:
        c->mem = cur;
        c->has_mem = cur != 0;
        c->entering = false;
        break;
    case K_MPLUS:
        c->mem += cur;
        c->has_mem = c->mem != 0;
        c->entering = false;
        break;
    }
}

/* ---- window ------------------------------------------------------------------------------------------- */

enum { CM_COPY = 1, CM_PASTE, CM_ABOUT };

static const menu_item_t edit_menu[] = {
    { "Copy", CM_COPY, 0, 0, NULL, 0, "Ctrl+C" },
    { "Paste", CM_PASTE, 0, 0, NULL, 0, "Ctrl+V" },
};
static const menu_item_t view_menu[] = {
    { "Standard", 0, MI_RADIO, 0, NULL, 0, NULL },
    { "Scientific", 0, MI_DISABLED, 0, NULL, 0, NULL },
};
static const menu_item_t help_menu[] = {
    { "About Calculator", CM_ABOUT, 0, 0, NULL, 0, NULL },
};
static const menubar_entry_t bar[] = {
    { "Edit", edit_menu, ARRAY_LEN(edit_menu) },
    { "View", view_menu, ARRAY_LEN(view_menu) },
    { "Help", help_menu, ARRAY_LEN(help_menu) },
};

static void display_text(calc_t *c, char *out, int n)
{
    strlcpy(out, c->entry, n);
    char *comma = strchr(out, ',');
    if (comma)
        *comma = 0;
}

static void calc_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    calc_t *c = w->data;
    menubar_paint(&c->mb, g, cw);
    char text[40];
    display_text(c, text, sizeof text);
    int dy = MENUBAR_H + 6;
    draw_textbox(g, 8, dy, cw - 16, 24);
    gfx_text(g, &font_ui, cw - 14 - text_width(&font_ui, text), dy + 5, text, HEX(0x000000));

    /* memory indicator */
    int my = MENUBAR_H + 38;
    gfx_fill(g, 8, my, BW, BH, COL_FACE);
    draw_sunken(g, 8, my, BW, BH);
    if (c->has_mem)
        gfx_text_centered(g, &font_ui, 8, my + 8, BW, "M", HEX(0x000000));

    for (int i = 0; i < n_keys; i++) {
        ckey_t *k = &keys[i];
        int st = 0;
        if (c->hover == i)
            st |= BTN_HOVER;
        if (c->pressed == i && c->hover == i)
            st |= BTN_PRESSED;
        draw_button_face(g, k->x, k->y, k->w, k->h, st);
        int off = (st & BTN_PRESSED) ? 1 : 0;
        gfx_text_centered(g, &font_ui, k->x + off, k->y + (k->h - font_ui.height) / 2 + 1 + off, k->w, k->label,
                          k->red ? HEX(0xE00000) : HEX(0x0000E0));
    }
}

static int key_at(int x, int y)
{
    for (int i = 0; i < n_keys; i++)
        if (x >= keys[i].x && y >= keys[i].y && x < keys[i].x + keys[i].w && y < keys[i].y + keys[i].h)
            return i;
    return -1;
}

static void command(int id, void *ctx)
{
    window_t *w = ctx;
    if (!wm_alive(w))
        return;
    calc_t *c = w->data;
    char text[40];
    switch (id) {
    case CM_COPY:
        display_text(c, text, sizeof text);
        clipboard_set(text, strlen(text));
        break;
    case CM_PASTE: {
        const char *s = clipboard_get();
        for (; *s; s++) {
            if (kisdigit(*s))
                press(c, *s - '0');
            else if (*s == '.')
                press(c, K_DOT);
        }
        break;
    }
    case CM_ABOUT:
        msgbox(w, "About Calculator", "KeplerOS Calculator\nVersion " KEPLER_VERSION, ICON_CALC);
        return;
    }
    wm_invalidate_client(w);
}

static void calc_mouse(window_t *w, int kind, int x, int y, int button)
{
    calc_t *c = w->data;
    if (menubar_mouse(&c->mb, w, kind, x, y, command, w))
        return;
    int k = kind == MS_LEAVE ? -1 : key_at(x, y);
    if (k != c->hover) {
        c->hover = k;
        wm_invalidate_client(w);
    }
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        c->pressed = k;
        wm_invalidate_client(w);
    } else if (kind == MS_UP && button == MOUSE_LEFT) {
        if (c->pressed >= 0 && c->pressed == k)
            press(c, keys[k].key);
        c->pressed = -1;
        wm_invalidate_client(w);
    }
}

static void calc_key(window_t *w, const input_event_t *ev)
{
    calc_t *c = w->data;
    if (!ev->pressed)
        return;
    int k = -1;
    int ch = ev->key;
    if ((ev->mods & MOD_CTRL) && (ch == 'c' || ch == 'v')) {
        command(ch == 'c' ? CM_COPY : CM_PASTE, w);
        return;
    }
    if (ch >= '0' && ch <= '9')
        k = ch - '0';
    else
        switch (ch) {
        case '.':
        case ',': k = K_DOT; break;
        case '+': k = K_ADD; break;
        case '-': k = K_SUB; break;
        case '*': k = K_MUL; break;
        case '/': k = K_DIV; break;
        case '=':
        case KEY_ENTER: k = K_EQ; break;
        case '%': k = K_PCT; break;
        case 'r': k = K_INV; break;
        case '@': k = K_SQRT; break;
        case KEY_F9: k = K_NEG; break;
        case KEY_BACKSPACE: k = K_BACK; break;
        case KEY_DELETE: k = K_CE; break;
        case KEY_ESC: k = K_C; break;
        }
    if (k >= 0) {
        press(c, k);
        wm_invalidate_client(w);
    }
}

static void calc_close(window_t *w) { kfree(w->data); }

static const window_ops_t calc_ops = { calc_paint, calc_key, calc_mouse, NULL, calc_close, NULL };

window_t *calc_open(void)
{
    layout();
    calc_t *c = kzalloc(sizeof *c);
    strlcpy(c->entry, "0.", sizeof c->entry);
    c->hover = c->pressed = -1;
    c->mb.entries = bar;
    c->mb.n = ARRAY_LEN(bar);
    c->mb.open = c->mb.hover = -1;
    int cw = GX + 4 * SX + 4 + BW + 8;
    int chh = GY + 4 * SY + 6;
    return wm_create("Calculator", ICON_CALC, cw, chh, &calc_ops, c, WF_NO_MAX);
}
