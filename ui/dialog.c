/* Message boxes, a text prompt and the Run dialog. */

#include "ramfs.h"
#include "ui.h"

enum { DLG_MSG, DLG_PROMPT, DLG_RUN };
enum { BTN_OK = 1, BTN_CANCEL, BTN_BROWSE };

typedef struct {
    int kind;
    char text[512];
    char label[96];
    int icon;
    textfield_t field;
    button_t items[3];
    buttons_t btns;
    prompt_cb cb;
    void *ctx;
    int text_h;
} dlg_t;

#define TEXT_W 340

static void finish(window_t *w, bool ok)
{
    dlg_t *d = w->data;
    prompt_cb cb = d->cb;
    void *ctx = d->ctx;
    char value[128];
    strlcpy(value, d->field.text, sizeof value);
    int kind = d->kind;
    d->cb = NULL;
    wm_close(w);
    if (kind == DLG_RUN && ok && value[0]) {
        /* a path opens the file, anything else is a program name */
        fnode_t *f = value[0] == '/' ? fs_lookup(value) : NULL;
        char cmd[160];
        if (f && f->dir)
            ksnprintf(cmd, sizeof cmd, "explorer %s", value);
        else if (f && (!strcasecmp(fs_ext(f->name), "jpg") || !strcasecmp(fs_ext(f->name), "jpeg")))
            ksnprintf(cmd, sizeof cmd, "viewer %s", value);
        else if (f)
            ksnprintf(cmd, sizeof cmd, "notepad %s", value);
        else
            strlcpy(cmd, value, sizeof cmd);
        ui_launch(cmd);
    }
    if (cb)
        cb(ok ? value : NULL, ctx);
}

static void dlg_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    dlg_t *d = w->data;
    int x = 14, y = 14;
    if (d->icon >= 0) {
        gfx_blit_alpha(g, x, y, icon_get(d->icon, 32), 255);
        x += 48;
    }
    if (d->kind == DLG_MSG) {
        gfx_text_wrapped(g, &font_ui, x, y + (d->text_h < 32 ? (32 - d->text_h) / 2 : 0), TEXT_W, d->text, HEX(0x000000), 15);
    } else if (d->kind == DLG_PROMPT) {
        gfx_text(g, &font_ui, x, y, d->label, HEX(0x000000));
        textfield_paint(&d->field, g, x, y + 20, cw - x - 14, true);
    } else {
        gfx_text_wrapped(g, &font_ui, x, y, cw - x - 14, d->text, HEX(0x000000), 15);
        gfx_text(g, &font_ui, 14, 72, "Open:", HEX(0x000000));
        textfield_paint(&d->field, g, 62, 68, cw - 62 - 14, true);
    }
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
    else if (id == BTN_BROWSE) {
        finish(w, false);
        ui_launch("mydocs");
    }
}

static void dlg_tick(window_t *w) { wm_invalidate_client(w); }

static void dlg_close(window_t *w)
{
    dlg_t *d = w->data;
    if (d->cb)
        d->cb(NULL, d->ctx);
    kfree(d);
}

static const window_ops_t dlg_ops = { dlg_paint, dlg_key, dlg_mouse, dlg_tick, dlg_close, NULL };

static window_t *make(dlg_t *d, window_t *owner, const char *title, int icon, int cw, int ch)
{
    window_t *w = wm_create(title, icon, cw, ch, &dlg_ops, d, WF_DIALOG | (owner ? WF_NO_TASKBAR : 0));
    if (owner && wm_alive(owner)) {
        wm_set_modal(owner, w);
        rect_t oc = wm_client_rect(owner);
        wm_move(w, CLAMP(oc.x + (oc.w - w->w) / 2, 0, g_screen.w - w->w),
                CLAMP(oc.y + (oc.h - w->h) / 2, 0, g_screen.h - TASKBAR_H - w->h));
    } else {
        wm_center(w);
    }
    d->btns.items = d->items;
    d->btns.hover = d->btns.pressed = -1;
    return w;
}

void msgbox(window_t *owner, const char *title, const char *text, int icon)
{
    dlg_t *d = kzalloc(sizeof *d);
    d->kind = DLG_MSG;
    d->icon = icon;
    strlcpy(d->text, text, sizeof d->text);
    d->text_h = gfx_text_wrapped(NULL, &font_ui, 0, 0, TEXT_W, text, 0, 15);
    int widest = 0;
    for (const char *p = text; *p;) {
        const char *nl = strchr(p, '\n');
        int n = nl ? nl - p : (int)strlen(p);
        widest = MAX(widest, text_width_n(&font_ui, p, n));
        p += n;
        if (*p)
            p++;
    }
    int cw = MAX(260, MIN(TEXT_W, widest) + (icon >= 0 ? 48 : 0) + 34);
    int body = MAX(d->text_h, 32);
    int ch = 14 + body + 18 + 23 + 12;
    d->items[0] = (button_t){ (cw - 75) / 2, ch - 35, 75, 23, "OK", BTN_OK, BTN_DEFAULT };
    d->btns.n = 1;
    make(d, owner, title, icon >= 0 ? icon : ICON_INFO, cw, ch);
    ui_beep(icon == ICON_ERROR ? 440 : 660, 90);
}

void prompt_box(window_t *owner, const char *title, const char *label, const char *initial, prompt_cb cb, void *ctx)
{
    dlg_t *d = kzalloc(sizeof *d);
    d->kind = DLG_PROMPT;
    d->icon = -1;
    d->cb = cb;
    d->ctx = ctx;
    strlcpy(d->label, label, sizeof d->label);
    textfield_set(&d->field, initial ? initial : "");
    int cw = 330, ch = 104;
    d->items[0] = (button_t){ cw - 170, ch - 35, 75, 23, "OK", BTN_OK, BTN_DEFAULT };
    d->items[1] = (button_t){ cw - 89, ch - 35, 75, 23, "Cancel", BTN_CANCEL, 0 };
    d->btns.n = 2;
    window_t *w = make(d, owner, title, ICON_NOTEPAD, cw, ch);
    wm_set_tick(w, 530);
}

void run_dialog(void)
{
    dlg_t *d = kzalloc(sizeof *d);
    d->kind = DLG_RUN;
    d->icon = ICON_RUN;
    strlcpy(d->text, "Type the name of a program, folder or document, and KeplerOS will open it for you.", sizeof d->text);
    textfield_set(&d->field, "");
    int cw = 360, ch = 140;
    d->items[0] = (button_t){ cw - 252, ch - 35, 75, 23, "OK", BTN_OK, BTN_DEFAULT };
    d->items[1] = (button_t){ cw - 170, ch - 35, 75, 23, "Cancel", BTN_CANCEL, 0 };
    d->items[2] = (button_t){ cw - 89, ch - 35, 75, 23, "Browse...", BTN_BROWSE, 0 };
    d->btns.n = 3;
    window_t *w = make(d, NULL, "Run", ICON_RUN, cw, ch);
    wm_move(w, 6, g_screen.h - TASKBAR_H - w->h - 6);
    wm_set_tick(w, 530);
}
