/* Notepad: a plain-text editor backed by the RAM file system. */

#include "apps.h"
#include "ramfs.h"

#define LINE_H 16

typedef struct {
    char *buf;
    int len, cap;
    int cur, anchor; /* anchor < 0: no selection */
    int top, scroll_x;
    char path[128];
    bool status;
    bool selecting;
    menubar_t mb;
    scrollbar_t sb;
} notepad_t;

enum {
    NP_NEW = 1,
    NP_OPEN,
    NP_SAVE,
    NP_SAVEAS,
    NP_EXIT,
    NP_CUT,
    NP_COPY,
    NP_PASTE,
    NP_DELETE,
    NP_SELALL,
    NP_TIME,
    NP_STATUS,
    NP_ABOUT,
};

static const menu_item_t file_menu[] = {
    { "New", NP_NEW, 0, 0, NULL, 0, "Ctrl+N" },
    { "Open...", NP_OPEN, 0, 0, NULL, 0, "Ctrl+O" },
    { "Save", NP_SAVE, 0, 0, NULL, 0, "Ctrl+S" },
    { "Save As...", NP_SAVEAS, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Exit", NP_EXIT, 0, 0, NULL, 0, NULL },
};

static const menu_item_t edit_menu[] = {
    { "Undo", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+Z" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Cut", NP_CUT, 0, 0, NULL, 0, "Ctrl+X" },
    { "Copy", NP_COPY, 0, 0, NULL, 0, "Ctrl+C" },
    { "Paste", NP_PASTE, 0, 0, NULL, 0, "Ctrl+V" },
    { "Delete", NP_DELETE, 0, 0, NULL, 0, "Del" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Select All", NP_SELALL, 0, 0, NULL, 0, "Ctrl+A" },
    { "Time/Date", NP_TIME, 0, 0, NULL, 0, "F5" },
};

static const menu_item_t format_menu[] = {
    { "Word Wrap", 0, MI_DISABLED, 0, NULL, 0, NULL },
    { "Font...", 0, MI_DISABLED, 0, NULL, 0, NULL },
};

static menu_item_t view_menu[] = {
    { "Status Bar", NP_STATUS, 0, 0, NULL, 0, NULL },
};

static const menu_item_t help_menu[] = {
    { "About Notepad", NP_ABOUT, 0, 0, NULL, 0, NULL },
};

static const menubar_entry_t bar[] = {
    { "File", file_menu, ARRAY_LEN(file_menu) },
    { "Edit", edit_menu, ARRAY_LEN(edit_menu) },
    { "Format", format_menu, ARRAY_LEN(format_menu) },
    { "View", view_menu, ARRAY_LEN(view_menu) },
    { "Help", help_menu, ARRAY_LEN(help_menu) },
};

static int adv(void) { return char_width(&font_mono, 'M'); }

/* ---- buffer helpers ---------------------------------------------------------------------------------- */

static int line_start(notepad_t *np, int i)
{
    while (i > 0 && np->buf[i - 1] != '\n')
        i--;
    return i;
}

static int line_end(notepad_t *np, int i)
{
    while (i < np->len && np->buf[i] != '\n')
        i++;
    return i;
}

static int line_of(notepad_t *np, int idx)
{
    int n = 0;
    for (int i = 0; i < idx && i < np->len; i++)
        if (np->buf[i] == '\n')
            n++;
    return n;
}

static int line_count(notepad_t *np) { return line_of(np, np->len) + 1; }

static int index_of(notepad_t *np, int line, int col)
{
    int i = 0;
    for (int l = 0; l < line && i < np->len; i++)
        if (np->buf[i] == '\n')
            l++;
    int end = line_end(np, i);
    return MIN(i + MAX(col, 0), end);
}

static void reserve(notepad_t *np, int extra)
{
    if (np->len + extra + 1 <= np->cap)
        return;
    int cap = (np->len + extra + 1) * 2;
    np->buf = krealloc(np->buf, cap);
    np->cap = cap;
}

static bool has_sel(notepad_t *np) { return np->anchor >= 0 && np->anchor != np->cur; }

static void sel_range(notepad_t *np, int *a, int *b)
{
    *a = MIN(np->anchor, np->cur);
    *b = MAX(np->anchor, np->cur);
}

static void delete_range(notepad_t *np, int a, int b)
{
    memmove(np->buf + a, np->buf + b, np->len - b);
    np->len -= b - a;
    np->buf[np->len] = 0;
    np->cur = a;
    np->anchor = -1;
}

static void delete_sel(notepad_t *np)
{
    if (!has_sel(np)) {
        np->anchor = -1;
        return;
    }
    int a, b;
    sel_range(np, &a, &b);
    delete_range(np, a, b);
}

static void insert(notepad_t *np, const char *s, int n)
{
    delete_sel(np);
    reserve(np, n);
    memmove(np->buf + np->cur + n, np->buf + np->cur, np->len - np->cur);
    memcpy(np->buf + np->cur, s, n);
    np->len += n;
    np->cur += n;
    np->buf[np->len] = 0;
}

static void set_text(notepad_t *np, const char *s, int n)
{
    np->len = 0;
    np->cur = 0;
    np->anchor = -1;
    np->top = 0;
    np->scroll_x = 0;
    reserve(np, n);
    memcpy(np->buf, s, n);
    np->len = n;
    np->buf[n] = 0;
}

/* ---- geometry -------------------------------------------------------------------------------------------- */

static rect_t text_area(window_t *w)
{
    notepad_t *np = w->data;
    rect_t c = wm_client_rect(w);
    int h = c.h - MENUBAR_H - (np->status ? 22 : 0);
    return rect_make(0, MENUBAR_H, c.w - SCROLLBAR_W, h);
}

static int visible_lines(window_t *w) { return MAX(1, (text_area(w).h - 4) / LINE_H); }

static void ensure_visible(window_t *w)
{
    notepad_t *np = w->data;
    int line = line_of(np, np->cur);
    int vis = visible_lines(w);
    if (line < np->top)
        np->top = line;
    if (line >= np->top + vis)
        np->top = line - vis + 1;
    int col = np->cur - line_start(np, np->cur);
    int x = col * adv();
    rect_t ta = text_area(w);
    if (x < np->scroll_x)
        np->scroll_x = MAX(0, x - 40);
    if (x > np->scroll_x + ta.w - 12)
        np->scroll_x = x - ta.w + 40;
}

static void update_title(window_t *w)
{
    notepad_t *np = w->data;
    char t[96];
    const char *name = np->path[0] ? strrchr(np->path, '/') + 1 : "Untitled";
    ksnprintf(t, sizeof t, "%s - Notepad", name);
    wm_set_title(w, t);
}

/* ---- painting --------------------------------------------------------------------------------------------- */

static void np_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    notepad_t *np = w->data;
    menubar_paint(&np->mb, g, cw);
    rect_t ta = text_area(w);
    gfx_fill(g, ta.x, ta.y, ta.w, ta.h, HEX(0xFFFFFF));
    gfx_hline(g, 0, ta.y, cw, HEX(0x7F9DB9));

    int a = 0, b = 0;
    bool sel = has_sel(np);
    if (sel)
        sel_range(np, &a, &b);
    gfx_t tg = gfx_sub(g, ta.x + 4, ta.y + 2, ta.w - 4, ta.h - 2);
    int A = adv();
    int i = index_of(np, np->top, 0);
    int y = 1;
    int caret_line = line_of(np, np->cur);
    for (int line = np->top; y < ta.h && i <= np->len; line++) {
        int e = line_end(np, i);
        for (int k = i; k < e; k++) {
            int x = (k - i) * A - np->scroll_x;
            bool in_sel = sel && k >= a && k < b;
            if (in_sel)
                gfx_fill(&tg, x, y - 1, A, LINE_H, COL_SELECT);
            char c = np->buf[k];
            gfx_text_n(&tg, &font_mono, x, y - 1, &c, 1, in_sel ? HEX(0xFFFFFF) : HEX(0x000000));
        }
        if (sel && e < np->len && e >= a && e < b)
            gfx_fill(&tg, (e - i) * A - np->scroll_x, y - 1, A / 2, LINE_H, COL_SELECT);
        if (line == caret_line && wm_active() == w && (timer_ms() / 530) % 2 == 0) {
            int cx = (np->cur - i) * A - np->scroll_x;
            gfx_vline(&tg, cx, y, LINE_H - 1, HEX(0x000000));
        }
        y += LINE_H;
        if (e >= np->len)
            break;
        i = e + 1;
    }

    np->sb.total = line_count(np);
    np->sb.visible = visible_lines(w);
    np->sb.pos = np->top;
    scrollbar_paint(&np->sb, g, cw - SCROLLBAR_W, ta.y, ta.h);

    if (np->status) {
        char s[48];
        int line = line_of(np, np->cur);
        ksnprintf(s, sizeof s, "Ln %d, Col %d", line + 1, np->cur - line_start(np, np->cur) + 1);
        draw_statusbar(g, ch - 22, cw, "");
        gfx_vline(g, cw - 160, ch - 19, 17, HEX(0xD8D2BD));
        gfx_text(g, &font_ui, cw - 152, ch - 17, s, HEX(0x000000));
    }
}

/* ---- commands ----------------------------------------------------------------------------------------------- */

static void after_edit(window_t *w)
{
    ensure_visible(w);
    wm_invalidate_client(w);
}

static void save_as_done(const char *value, void *ctx)
{
    window_t *w = ctx;
    if (!value || !value[0] || !wm_alive(w))
        return;
    notepad_t *np = w->data;
    char path[128];
    if (value[0] == '/')
        strlcpy(path, value, sizeof path);
    else
        ksnprintf(path, sizeof path, "/Documents/%s", value);
    if (!strchr(strrchr(path, '/'), '.'))
        strlcat(path, ".txt", sizeof path);
    fnode_t *f = fs_create(path, false);
    if (!f) {
        msgbox(w, "Notepad", "The file could not be saved there.", ICON_ERROR);
        return;
    }
    fs_write(f, np->buf, np->len);
    strlcpy(np->path, path, sizeof np->path);
    update_title(w);
    if (!strncmp(path, "/Desktop/", 9))
        desktop_refresh_icons();
}

static void save(window_t *w, bool as)
{
    notepad_t *np = w->data;
    if (!np->path[0] || as) {
        prompt_box(w, "Save As", "File name (saved in My Documents):", np->path[0] ? strrchr(np->path, '/') + 1 : "Untitled.txt",
                   save_as_done, w);
        return;
    }
    fnode_t *f = fs_lookup(np->path);
    if (!f)
        f = fs_create(np->path, false);
    if (f)
        fs_write(f, np->buf, np->len);
}

static void command(int id, void *ctx)
{
    window_t *w = ctx;
    if (!wm_alive(w))
        return;
    notepad_t *np = w->data;
    int a, b;
    switch (id) {
    case NP_NEW:
        set_text(np, "", 0);
        np->path[0] = 0;
        update_title(w);
        break;
    case NP_OPEN:
        ui_launch("mydocs");
        return;
    case NP_SAVE: save(w, false); break;
    case NP_SAVEAS: save(w, true); break;
    case NP_EXIT: wm_close(w); return;
    case NP_CUT:
    case NP_COPY:
        if (has_sel(np)) {
            sel_range(np, &a, &b);
            clipboard_set(np->buf + a, b - a);
            if (id == NP_CUT)
                delete_sel(np);
        }
        break;
    case NP_PASTE: {
        const char *c = clipboard_get();
        insert(np, c, strlen(c));
        break;
    }
    case NP_DELETE:
        if (has_sel(np))
            delete_sel(np);
        else if (np->cur < np->len)
            delete_range(np, np->cur, np->cur + 1);
        break;
    case NP_SELALL:
        np->anchor = 0;
        np->cur = np->len;
        break;
    case NP_TIME: {
        datetime_t dt;
        rtc_read(&dt);
        char s[32];
        int h12 = dt.hour % 12 ? dt.hour % 12 : 12;
        ksnprintf(s, sizeof s, "%d:%02d %s %d/%d/%d", h12, dt.minute, dt.hour < 12 ? "AM" : "PM", dt.month, dt.day, dt.year);
        insert(np, s, strlen(s));
        break;
    }
    case NP_STATUS:
        np->status = !np->status;
        view_menu[0].flags = np->status ? MI_CHECK : 0;
        break;
    case NP_ABOUT:
        msgbox(w, "About Notepad", "KeplerOS Notepad\nVersion " KEPLER_VERSION "\n\nA small text editor for the KeplerOS RAM disk.",
               ICON_NOTEPAD);
        return;
    }
    after_edit(w);
}

/* ---- input -------------------------------------------------------------------------------------------------- */

static void move_to(notepad_t *np, int idx, bool extend)
{
    if (extend) {
        if (np->anchor < 0)
            np->anchor = np->cur;
    } else {
        np->anchor = -1;
    }
    np->cur = CLAMP(idx, 0, np->len);
}

static void np_key(window_t *w, const input_event_t *ev)
{
    notepad_t *np = w->data;
    if (!ev->pressed)
        return;
    bool shift = ev->mods & MOD_SHIFT, ctrl = ev->mods & MOD_CTRL;
    int line = line_of(np, np->cur);
    int col = np->cur - line_start(np, np->cur);
    if (ctrl) {
        switch (ktolower(ev->key)) {
        case 'a': command(NP_SELALL, w); return;
        case 'c': command(NP_COPY, w); return;
        case 'x': command(NP_CUT, w); return;
        case 'v': command(NP_PASTE, w); return;
        case 's': command(NP_SAVE, w); return;
        case 'n': command(NP_NEW, w); return;
        case 'o': command(NP_OPEN, w); return;
        }
        if (ev->key == KEY_HOME) {
            move_to(np, 0, shift);
            after_edit(w);
        } else if (ev->key == KEY_END) {
            move_to(np, np->len, shift);
            after_edit(w);
        }
        return;
    }
    switch (ev->key) {
    case KEY_LEFT: move_to(np, np->cur - 1, shift); break;
    case KEY_RIGHT: move_to(np, np->cur + 1, shift); break;
    case KEY_UP: move_to(np, line > 0 ? index_of(np, line - 1, col) : 0, shift); break;
    case KEY_DOWN: move_to(np, index_of(np, line + 1, col), shift); break;
    case KEY_HOME: move_to(np, line_start(np, np->cur), shift); break;
    case KEY_END: move_to(np, line_end(np, np->cur), shift); break;
    case KEY_PGUP: move_to(np, index_of(np, MAX(0, line - visible_lines(w)), col), shift); break;
    case KEY_PGDN: move_to(np, index_of(np, line + visible_lines(w), col), shift); break;
    case KEY_BACKSPACE:
        if (has_sel(np))
            delete_sel(np);
        else if (np->cur > 0)
            delete_range(np, np->cur - 1, np->cur);
        break;
    case KEY_DELETE: command(NP_DELETE, w); return;
    case KEY_ENTER: insert(np, "\n", 1); break;
    case KEY_TAB: insert(np, "    ", 4); break;
    case KEY_F5: command(NP_TIME, w); return;
    default:
        if (ev->key >= 32 && ev->key < 127) {
            char c = (char)ev->key;
            insert(np, &c, 1);
        } else {
            return;
        }
    }
    after_edit(w);
}

static int index_at(window_t *w, int x, int y)
{
    notepad_t *np = w->data;
    rect_t ta = text_area(w);
    int line = np->top + MAX(0, (y - ta.y - 2) / LINE_H);
    line = MIN(line, line_count(np) - 1);
    int col = (x - ta.x - 4 + np->scroll_x + adv() / 2) / adv();
    return index_of(np, line, col);
}

static void np_mouse(window_t *w, int kind, int x, int y, int button)
{
    notepad_t *np = w->data;
    if (menubar_mouse(&np->mb, w, kind, x, y, command, w))
        return;
    rect_t ta = text_area(w);
    if (kind == MS_WHEEL) {
        np->top = CLAMP(np->top + button * 3, 0, MAX(0, line_count(np) - visible_lines(w)));
        wm_invalidate_client(w);
        return;
    }
    if (x >= ta.w && y >= ta.y && y < ta.y + ta.h && !np->selecting) {
        np->sb.total = line_count(np);
        np->sb.visible = visible_lines(w);
        np->sb.pos = np->top;
        if (scrollbar_mouse(&np->sb, kind, x, y, ta.w, ta.y, ta.h)) {
            np->top = np->sb.pos;
            wm_invalidate_client(w);
        }
        w->cursor = CURSOR_ARROW;
        return;
    }
    if (np->sb.drag_off >= 0) {
        scrollbar_mouse(&np->sb, kind, x, y, ta.w, ta.y, ta.h);
        np->top = np->sb.pos;
        wm_invalidate_client(w);
        return;
    }
    w->cursor = rect_contains(ta, x, y) ? CURSOR_IBEAM : CURSOR_ARROW;
    if (kind == MS_DOWN && button == MOUSE_LEFT && rect_contains(ta, x, y)) {
        move_to(np, index_at(w, x, y), (keyboard_mods() & MOD_SHIFT) != 0);
        if (np->anchor < 0)
            np->anchor = np->cur;
        np->selecting = true;
        wm_invalidate_client(w);
    } else if (kind == MS_MOVE && np->selecting) {
        np->cur = index_at(w, x, y);
        ensure_visible(w);
        wm_invalidate_client(w);
    } else if (kind == MS_UP) {
        np->selecting = false;
        if (np->anchor == np->cur)
            np->anchor = -1;
    } else if (kind == MS_DOWN && button == MOUSE_RIGHT && rect_contains(ta, x, y)) {
        rect_t c = wm_client_rect(w);
        menu_open(edit_menu, ARRAY_LEN(edit_menu), c.x + x, c.y + y, command, w);
    }
}

static void np_tick(window_t *w)
{
    if (wm_active() == w)
        wm_invalidate_client(w);
}

static void np_close(window_t *w)
{
    notepad_t *np = w->data;
    kfree(np->buf);
    kfree(np);
}

static const window_ops_t np_ops = { np_paint, np_key, np_mouse, np_tick, np_close, NULL };

window_t *notepad_open(const char *path)
{
    notepad_t *np = kzalloc(sizeof *np);
    np->anchor = -1;
    np->mb.entries = bar;
    np->mb.n = ARRAY_LEN(bar);
    np->mb.open = np->mb.hover = -1;
    np->sb.drag_off = -1;
    reserve(np, 256);
    np->buf[0] = 0;
    if (path) {
        fnode_t *f = fs_lookup(path);
        if (f && !f->dir) {
            set_text(np, (const char *)f->data, f->size);
            fs_path(f, np->path, sizeof np->path);
        } else {
            strlcpy(np->path, path, sizeof np->path);
        }
    }
    window_t *w = wm_create("Untitled - Notepad", ICON_NOTEPAD, 560, 380, &np_ops, np, WF_RESIZABLE);
    w->bg = HEX(0xFFFFFF);
    w->cursor = CURSOR_IBEAM;
    update_title(w);
    wm_set_tick(w, 530);
    return w;
}
