/* Alerts and a text prompt, laid out like Big Sur alerts: icon on top, a bold
 * message, smaller details and full-width buttons. Run now opens Spotlight. */

#include "ui.h"

enum { DLG_MSG, DLG_PROMPT };
enum { BTN_OK = 1, BTN_CANCEL };

#define AL_W  280
#define PAD   20
#define ICON  64

typedef struct {
    int kind;
    char title[128];
    char text[512];
    int icon;
    textfield_t field;
    button_t items[2];
    buttons_t btns;
    prompt_cb cb;
    void *ctx;
    int title_h, text_h;
} dlg_t;

/* centred, word-wrapped text; with g NULL it only measures */
static int wrap_centered(gfx_t *g, const font_t *f, int x, int y, int w, const char *s, uint32_t c, int lh)
{
    int top = y;
    while (*s) {
        int n = 0, last_space = -1, lw = 0;
        while (s[n] && s[n] != '\n') {
            int cw = char_width(f, s[n]);
            if (lw + cw > w && n > 0)
                break;
            if (s[n] == ' ')
                last_space = n;
            lw += cw;
            n++;
        }
        int take = (s[n] && s[n] != '\n' && last_space > 0) ? last_space : n;
        if (g)
            gfx_text_n(g, f, x + (w - text_width_n(f, s, take)) / 2, y, s, take, c);
        y += lh;
        s += take;
        while (*s == ' ')
            s++;
        if (*s == '\n')
            s++;
    }
    return y - top;
}

static void finish(window_t *w, bool ok)
{
    dlg_t *d = w->data;
    prompt_cb cb = d->cb;
    void *ctx = d->ctx;
    char value[128];
    strlcpy(value, d->field.text, sizeof value);
    d->cb = NULL;
    wm_close(w);
    if (cb)
        cb(ok ? value : NULL, ctx);
}

static int field_y(dlg_t *d) { return PAD + ICON + 14 + d->title_h + (d->text_h ? 6 + d->text_h : 0) + 12; }

static void dlg_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    dlg_t *d = w->data;
    gfx_blit_alpha(g, (cw - ICON) / 2, PAD, icon_get(d->icon, ICON), 255);
    int y = PAD + ICON + 14;
    y += wrap_centered(g, &font_ui_bold, PAD, y, cw - 2 * PAD, d->title, COL_TEXT, 17);
    if (d->text[0])
        wrap_centered(g, &font_ui, PAD, y + 6, cw - 2 * PAD, d->text, HEX(0x4A4A4E), 15);
    if (d->kind == DLG_PROMPT)
        textfield_paint(&d->field, g, PAD, field_y(d), cw - 2 * PAD, true);
    buttons_paint(&d->btns, g);
}

static void dlg_key(window_t *w, const input_event_t *ev)
{
    dlg_t *d = w->data;
    if (!ev->pressed)
        return;
    if (ev->key == KEY_ENTER) {
        finish(w, true);
        return;
    }
    if (ev->key == KEY_ESC) {
        finish(w, false);
        return;
    }
    if (d->kind != DLG_MSG && textfield_key(&d->field, ev))
        wm_invalidate_client(w);
}

static void dlg_mouse(window_t *w, int kind, int x, int y, int button)
{
    dlg_t *d = w->data;
    int id = buttons_mouse(&d->btns, w, kind, x, y);
    if (id == BTN_OK)
        finish(w, true);
    else if (id == BTN_CANCEL)
        finish(w, false);
}

/* the buttons and the text field take clicks; the rest of the alert drags it */
static bool dlg_caption_hit(window_t *w, int x, int y)
{
    dlg_t *d = w->data;
    for (int i = 0; i < d->btns.n; i++) {
        button_t *b = &d->items[i];
        if (x >= b->x && y >= b->y && x < b->x + b->w && y < b->y + b->h)
            return true;
    }
    return d->kind == DLG_PROMPT && y >= field_y(d) - 4 && y < field_y(d) + 26;
}

static void dlg_tick(window_t *w) { wm_invalidate_client(w); }

static void dlg_close(window_t *w)
{
    dlg_t *d = w->data;
    if (d->cb)
        d->cb(NULL, d->ctx);
    kfree(d);
}

static const window_ops_t dlg_ops = { dlg_paint, dlg_key, dlg_mouse, dlg_tick, dlg_close, NULL, dlg_caption_hit };

static window_t *make(dlg_t *d, window_t *owner, int nbuttons, const char *ok_label)
{
    d->title_h = wrap_centered(NULL, &font_ui_bold, 0, 0, AL_W - 2 * PAD, d->title, 0, 17);
    d->text_h = d->text[0] ? wrap_centered(NULL, &font_ui, 0, 0, AL_W - 2 * PAD, d->text, 0, 15) : 0;
    int ch = field_y(d) + (d->kind == DLG_PROMPT ? 21 + 18 : 4) + 24 + PAD;
    int by = ch - PAD - 24;
    if (nbuttons == 1) {
        d->items[0] = (button_t){ PAD, by, AL_W - 2 * PAD, 24, ok_label, BTN_OK, BTN_DEFAULT };
    } else {
        int bw = (AL_W - 2 * PAD - 10) / 2;
        d->items[0] = (button_t){ PAD, by, bw, 24, "Cancel", BTN_CANCEL, 0 };
        d->items[1] = (button_t){ PAD + bw + 10, by, bw, 24, ok_label, BTN_OK, BTN_DEFAULT };
    }
    d->btns.items = d->items;
    d->btns.n = nbuttons;
    d->btns.hover = d->btns.pressed = -1;
    window_t *w = wm_create(d->title, d->icon, AL_W, ch, &dlg_ops, d, WF_DIALOG | WF_ALERT | (owner ? WF_NO_TASKBAR : 0));
    if (owner && wm_alive(owner)) {
        wm_set_modal(owner, w);
        rect_t oc = wm_client_rect(owner), a = ui_work_area();
        wm_move(w, CLAMP(oc.x + (oc.w - w->w) / 2, 0, g_screen.w - w->w), CLAMP(oc.y + 40, a.y, a.y + a.h - w->h));
    } else {
        wm_center(w);
    }
    return w;
}

void msgbox(window_t *owner, const char *title, const char *text, int icon)
{
    dlg_t *d = kzalloc(sizeof *d);
    d->kind = DLG_MSG;
    d->icon = icon >= 0 ? icon : ICON_KEPLER;
    strlcpy(d->title, title, sizeof d->title);
    strlcpy(d->text, text, sizeof d->text);
    make(d, owner, 1, "OK");
    ui_beep(icon == ICON_ERROR ? 440 : 660, 90);
}

void prompt_box(window_t *owner, const char *title, const char *label, const char *initial, prompt_cb cb, void *ctx)
{
    dlg_t *d = kzalloc(sizeof *d);
    d->kind = DLG_PROMPT;
    d->icon = owner ? owner->icon : ICON_FINDER;
    d->cb = cb;
    d->ctx = ctx;
    strlcpy(d->title, title, sizeof d->title);
    strlcpy(d->text, label, sizeof d->text);
    textfield_set(&d->field, initial ? initial : "");
    window_t *w = make(d, owner, 2, "OK");
    wm_set_tick(w, 530);
}

void run_dialog(void) { spotlight_open(); }
