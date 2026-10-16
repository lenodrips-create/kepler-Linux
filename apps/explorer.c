/* Finder: folder windows with a translucent sidebar, a toolbar in the title
 * bar and an icon grid. */

#include "apps.h"
#include "ramfs.h"

#define TOOLBAR_H 52
#define SIDEBAR_W 180
#define CELL_W    112
#define CELL_H    112
#define ICON_SZ   64
#define ROW_H     24

typedef struct {
    char path[128]; /* "::computer" or an absolute path */
    char back[8][128];
    int n_back;
    char fwd[8][128];
    int n_fwd;
    int sel;
    int hover_side, hover_tool;
    int pressed_tool;
    int scroll;
    menubar_t mb;
} explorer_t;

typedef struct {
    char name[64];
    int icon;
    char target[160]; /* a path, or "!cd" */
} entry_t;

#define MAX_ENTRIES 64
static entry_t entries[MAX_ENTRIES];
static int n_entries;

/* ---- model ------------------------------------------------------------------------------------------- */

static bool is_computer(explorer_t *e) { return !strcmp(e->path, "::computer"); }

static const char *nice_name(const char *path)
{
    if (!strcmp(path, "::computer"))
        return "Computer";
    if (!strcmp(path, "/"))
        return "Kepler HD";
    if (!strcasecmp(path, "/Documents/My Pictures"))
        return "Pictures";
    if (!strcasecmp(path, "/Recycled"))
        return "Trash";
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static int folder_icon(const char *path)
{
    if (!strcmp(path, "::computer"))
        return ICON_COMPUTER;
    if (!strcmp(path, "/"))
        return ICON_DRIVE;
    if (!strcasecmp(path, "/Documents"))
        return ICON_DOCUMENTS;
    if (!strcasecmp(path, "/Documents/My Pictures"))
        return ICON_FOLDER_PICTURES;
    if (!strcasecmp(path, "/Desktop"))
        return ICON_FOLDER_DESKTOP;
    if (!strcasecmp(path, "/Downloads"))
        return ICON_FOLDER_DOWNLOADS;
    if (!strcasecmp(path, "/Recycled"))
        return ICON_RECYCLE;
    return ICON_FOLDER;
}

static bool is_image(const char *name)
{
    const char *x = fs_ext(name);
    return !strcasecmp(x, "jpg") || !strcasecmp(x, "jpeg");
}

static void add_entry(const char *name, int icon, const char *target)
{
    if (n_entries >= MAX_ENTRIES)
        return;
    entry_t *x = &entries[n_entries++];
    strlcpy(x->name, name, sizeof x->name);
    x->icon = icon;
    strlcpy(x->target, target, sizeof x->target);
}

static void load(explorer_t *e)
{
    n_entries = 0;
    if (is_computer(e)) {
        add_entry("Kepler HD", ICON_DRIVE, "/");
        add_entry("CD Drive", ICON_CDROM, "!cd");
        return;
    }
    fnode_t *d = fs_lookup(e->path);
    for (fnode_t *f = d ? d->child : NULL; f && n_entries < MAX_ENTRIES; f = f->next) {
        char path[160];
        fs_path(f, path, sizeof path);
        int icon = f->dir ? folder_icon(path) : is_image(f->name) ? ICON_IMAGEFILE : ICON_TEXTFILE;
        add_entry(f->name, icon, path);
    }
}

/* ---- sidebar ------------------------------------------------------------------------------------------ */

typedef struct {
    const char *label; /* NULL: a section heading in `target` */
    int glyph;
    const char *target;
} side_t;

static const side_t side[] = {
    { NULL, 0, "Favorites" },
    { "AirDrop", GLYPH_AIRDROP, "#airdrop" },
    { "Recents", GLYPH_RECENTS, "/Documents" },
    { "Applications", GLYPH_APPS, "!launchpad" },
    { "Desktop", GLYPH_DESKTOP, "/Desktop" },
    { "Documents", GLYPH_DOCUMENT, "/Documents" },
    { "Downloads", GLYPH_DOWNLOADS, "/Downloads" },
    { NULL, 0, "Locations" },
    { "Kepler HD", GLYPH_DISK, "/" },
    { "Computer", GLYPH_DESKTOP, "::computer" },
    { "Trash", GLYPH_TRASH, "/Recycled" },
    { NULL, 0, "Tags" },
    { "Red", 0xFF453A, "#tag" },
    { "Orange", 0xFF9F0A, "#tag" },
    { "Yellow", 0xFFD60A, "#tag" },
    { "Green", 0x32D74B, "#tag" },
    { "Blue", 0x0A84FF, "#tag" },
    { "Purple", 0xBF5AF2, "#tag" },
};
#define N_SIDE ((int)ARRAY_LEN(side))

static bool is_tag(int i) { return side[i].label && !strcmp(side[i].target, "#tag"); }

static int side_y(int i)
{
    int y = TOOLBAR_H - 8;
    for (int k = 0; k < i; k++)
        y += side[k].label ? ROW_H : (k ? 30 : 22);
    return y + (side[i].label ? 0 : (i ? 12 : 4));
}

static int side_at(int x, int y)
{
    if (x >= SIDEBAR_W)
        return -1;
    for (int i = 0; i < N_SIDE; i++)
        if (side[i].label && y >= side_y(i) && y < side_y(i) + ROW_H)
            return i;
    return -1;
}

static void paint_sidebar(window_t *w, explorer_t *e, gfx_t *g, int h)
{
    /* behind-window vibrancy: the frosted wallpaper under where the sidebar sits */
    gfx_backdrop(g, 0, 0, SIDEBAR_W, h, 0, ui_vibrancy(), RGBA(238, 238, 242, 170), 0);
    gfx_vline(g, SIDEBAR_W - 1, 0, h, RGBA(0, 0, 0, 30));
    for (int i = 0; i < N_SIDE; i++) {
        int y = side_y(i);
        if (!side[i].label) {
            gfx_text(g, &font_ui_bold, 18, y, side[i].target, RGBA(0, 0, 0, 110));
            continue;
        }
        bool cur = side[i].target[0] == '/' || side[i].target[0] == ':' ? !strcasecmp(side[i].target, e->path) && i != 2 : false;
        if (cur)
            gfx_round_rect(g, 10, y, SIDEBAR_W - 20, ROW_H - 2, 5, RGBA(0, 0, 0, 34), CORNER_ALL);
        else if (e->hover_side == i)
            gfx_round_rect(g, 10, y, SIDEBAR_W - 20, ROW_H - 2, 5, RGBA(0, 0, 0, 14), CORNER_ALL);
        if (is_tag(i))
            gfx_circle(g, 26, y + ROW_H / 2 - 1, 5, HEX(side[i].glyph));
        else
            gfx_blit_tint(g, 18, y + (ROW_H - 2 - 16) / 2, icon_get(side[i].glyph, 16), COL_ACCENT);
        gfx_text(g, &font_ui, 42, y + (ROW_H - 2 - font_ui.height) / 2, side[i].label, COL_TEXT);
    }
    (void)w;
}

/* ---- toolbar ----------------------------------------------------------------------------------------- */

enum { T_BACK, T_FWD, T_GRID, T_LIST, T_COLS, T_GALLERY, T_GROUP, T_SHARE, T_TAG, T_MORE, T_SEARCH, N_TOOLS };

static rect_t tool_rect(int cw, int i)
{
    int y = (TOOLBAR_H - 26) / 2;
    if (i == T_BACK || i == T_FWD)
        return rect_make(SIDEBAR_W + 12 + i * 28, y, 26, 26);
    /* the rest are laid out from the right edge */
    static const int from_right[N_TOOLS] = { 0, 0, 302, 270, 238, 206, 166, 126, 92, 60, 30 };
    return rect_make(cw - from_right[i], y, i == T_GROUP ? 34 : 28, 26);
}

static const int tool_glyph[N_TOOLS] = { GLYPH_CHEVRON_L, GLYPH_CHEVRON_R, GLYPH_GRID, GLYPH_LIST, GLYPH_COLUMNS, GLYPH_GALLERY,
                                         GLYPH_GROUP, GLYPH_SHARE, GLYPH_TAG, GLYPH_MORE, GLYPH_SEARCH };

static bool tool_enabled(explorer_t *e, int i)
{
    if (i == T_BACK)
        return e->n_back > 0;
    if (i == T_FWD)
        return e->n_fwd > 0;
    return true;
}

static void paint_toolbar(explorer_t *e, gfx_t *g, int cw, bool active)
{
    gfx_fill(g, SIDEBAR_W, 0, cw - SIDEBAR_W, TOOLBAR_H, active ? HEX(0xFFFFFF) : HEX(0xF8F8F8));
    gfx_hline(g, SIDEBAR_W, TOOLBAR_H - 1, cw - SIDEBAR_W, RGBA(0, 0, 0, 24));
    for (int i = 0; i < N_TOOLS; i++) {
        rect_t r = tool_rect(cw, i);
        if (r.x < SIDEBAR_W + 70 && i > T_FWD)
            continue; /* a narrow window drops the right-hand tools */
        bool en = tool_enabled(e, i);
        if (i == T_GRID)
            gfx_round_rect(g, r.x, r.y, r.w, r.h, 5, RGBA(0, 0, 0, 22), CORNER_ALL);
        else if (e->hover_tool == i && en)
            gfx_round_rect(g, r.x, r.y, r.w, r.h, 5, RGBA(0, 0, 0, e->pressed_tool == i ? 30 : 14), CORNER_ALL);
        int sz = i == T_BACK || i == T_FWD ? 14 : 16;
        uint32_t c = en ? HEX(0x6E6E73) : HEX(0xC7C7CC);
        gfx_blit_tint(g, r.x + (r.w - sz) / 2 - (i == T_GROUP ? 4 : 0), r.y + (r.h - sz) / 2, icon_get(tool_glyph[i], sz), c);
        if (i == T_GROUP)
            gfx_blit_tint(g, r.x + r.w - 12, r.y + 9, icon_get(GLYPH_CHEVRON_D, 12), c);
    }
    int tx = SIDEBAR_W + 74;
    int max = tool_rect(cw, T_GRID).x - tx - 12;
    gfx_text_clipped(g, &font_title, tx, (TOOLBAR_H - font_title.height) / 2, max, nice_name(e->path),
                     active ? COL_TEXT : HEX(0xA0A0A0));
}

/* ---- content ----------------------------------------------------------------------------------------- */

static rect_t content_rect(window_t *w)
{
    rect_t c = wm_client_rect(w);
    return rect_make(SIDEBAR_W, TOOLBAR_H, c.w - SIDEBAR_W, c.h - TOOLBAR_H);
}

static int cols(window_t *w) { return MAX(1, (content_rect(w).w - 24) / CELL_W); }

static rect_t cell_rect(window_t *w, explorer_t *e, int i)
{
    int c = cols(w);
    return rect_make(12 + (i % c) * CELL_W, 14 + (i / c) * CELL_H - e->scroll, CELL_W, CELL_H);
}

static int wrap2(const char *s, char lines[2][64], int max_w)
{
    if (text_width(&font_ui, s) <= max_w) {
        strlcpy(lines[0], s, 64);
        return 1;
    }
    int n = 0, best = -1;
    while (s[n] && text_width_n(&font_ui, s, n + 1) <= max_w) {
        if (s[n] == ' ')
            best = n;
        n++;
    }
    int cut = best > 0 ? best : n;
    memcpy(lines[0], s, cut);
    lines[0][cut] = 0;
    const char *rest = s + cut + (best > 0 ? 1 : 0);
    if (text_width(&font_ui, rest) <= max_w) {
        strlcpy(lines[1], rest, 64);
    } else {
        int k = strlen(rest);
        while (k > 0 && text_width_n(&font_ui, rest, k) + text_width(&font_ui, "...") > max_w)
            k--;
        memcpy(lines[1], rest, k);
        strlcpy(lines[1] + k, "...", 64 - k);
    }
    return 2;
}

static void paint_cell(gfx_t *g, entry_t *x, rect_t r, bool sel, bool active)
{
    int ix = r.x + (r.w - ICON_SZ) / 2, iy = r.y + 4;
    if (sel)
        gfx_round_rect(g, ix - 6, iy - 6, ICON_SZ + 12, ICON_SZ + 12, 6, RGBA(0, 0, 0, 22), CORNER_ALL);
    gfx_blit_alpha(g, ix, iy, icon_get(x->icon, ICON_SZ), 255);
    char lines[2][64];
    int n = wrap2(x->name, lines, r.w - 12);
    int ty = iy + ICON_SZ + 9;
    for (int l = 0; l < n; l++) {
        int tw = text_width(&font_ui, lines[l]);
        int tx = r.x + (r.w - tw) / 2;
        if (sel) {
            gfx_round_rect(g, tx - 4, ty - 1, tw + 8, font_ui.height + 2, 4, active ? COL_ACCENT : HEX(0xDCDCDC), CORNER_ALL);
            gfx_text(g, &font_ui, tx, ty, lines[l], active ? HEX(0xFFFFFF) : COL_TEXT);
        } else {
            gfx_text(g, &font_ui, tx, ty, lines[l], COL_TEXT);
        }
        ty += font_ui.height + 1;
    }
}

static void ex_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    explorer_t *e = w->data;
    bool active = wm_active() == w;
    paint_sidebar(w, e, g, ch);
    paint_toolbar(e, g, cw, active);
    rect_t cr = content_rect(w);
    gfx_t cg = gfx_sub(g, cr.x, cr.y, cr.w, cr.h);
    gfx_fill(&cg, 0, 0, cr.w, cr.h, HEX(0xFFFFFF));
    for (int i = 0; i < n_entries; i++) {
        rect_t r = cell_rect(w, e, i);
        if (r.y + r.h > 0 && r.y < cr.h)
            paint_cell(&cg, &entries[i], r, i == e->sel, active);
    }
    if (!n_entries)
        gfx_text_centered(&cg, &font_ui, 0, cr.h / 2 - 20, cr.w, "This folder is empty.", HEX(0xA0A0A5));
}

/* ---- navigation ---------------------------------------------------------------------------------------------- */

static void refresh(window_t *w)
{
    explorer_t *e = w->data;
    load(e);
    if (e->sel >= n_entries)
        e->sel = -1;
    wm_set_title(w, nice_name(e->path));
    w->icon = folder_icon(e->path);
    wm_invalidate(w);
}

static void navigate(window_t *w, const char *path, bool record)
{
    explorer_t *e = w->data;
    if (strcmp(path, "::computer")) {
        fnode_t *f = fs_lookup(path);
        if (!f || !f->dir)
            return;
    }
    if (record && strcmp(path, e->path)) {
        if (e->n_back == 8) {
            memmove(e->back[0], e->back[1], sizeof e->back[0] * 7);
            e->n_back--;
        }
        strlcpy(e->back[e->n_back++], e->path, sizeof e->back[0]);
        e->n_fwd = 0;
    }
    strlcpy(e->path, path, sizeof e->path);
    e->sel = -1;
    e->scroll = 0;
    refresh(w);
}

static void go_back(window_t *w)
{
    explorer_t *e = w->data;
    if (!e->n_back)
        return;
    if (e->n_fwd < 8)
        strlcpy(e->fwd[e->n_fwd++], e->path, sizeof e->fwd[0]);
    char p[128];
    strlcpy(p, e->back[--e->n_back], sizeof p);
    navigate(w, p, false);
}

static void go_forward(window_t *w)
{
    explorer_t *e = w->data;
    if (!e->n_fwd)
        return;
    if (e->n_back < 8)
        strlcpy(e->back[e->n_back++], e->path, sizeof e->back[0]);
    char p[128];
    strlcpy(p, e->fwd[--e->n_fwd], sizeof p);
    navigate(w, p, false);
}

static void go_up(window_t *w)
{
    explorer_t *e = w->data;
    if (is_computer(e))
        return;
    if (!strcmp(e->path, "/")) {
        navigate(w, "::computer", true);
        return;
    }
    char p[128];
    strlcpy(p, e->path, sizeof p);
    char *s = strrchr(p, '/');
    if (s == p)
        s[1] = 0;
    else
        *s = 0;
    navigate(w, p, true);
}

static void open_entry(window_t *w, int i)
{
    if (i < 0 || i >= n_entries)
        return;
    entry_t *x = &entries[i];
    if (!strcmp(x->target, "!cd")) {
        msgbox(w, "No disc inserted.", "Insert a disc into the CD drive and try again.", ICON_CDROM);
        return;
    }
    fnode_t *f = fs_lookup(x->target);
    if (!f)
        return;
    if (f->dir) {
        navigate(w, x->target, true);
        return;
    }
    char cmd[200];
    ksnprintf(cmd, sizeof cmd, "%s %s", is_image(f->name) ? "viewer" : "notepad", x->target);
    ui_launch(cmd);
}

static void rename_done(const char *value, void *ctx)
{
    window_t *w = ctx;
    if (!value || !wm_alive(w))
        return;
    explorer_t *e = w->data;
    load(e);
    if (e->sel < 0 || e->sel >= n_entries)
        return;
    fnode_t *f = fs_lookup(entries[e->sel].target);
    if (f && value[0] && !fs_rename(f, value))
        msgbox(w, "The name is already taken.", "Please choose a different name.", ICON_WARNING);
    refresh(w);
    desktop_refresh_icons();
}

enum { EX_CLOSE = 1, EX_REFRESH, EX_UP, EX_NEWFOLDER, EX_NEWFILE, EX_DELETE, EX_RENAME, EX_OPEN, EX_ABOUT, EX_NEWWIN, EX_BACK,
       EX_FORWARD, EX_GO_DOCS, EX_GO_DESKTOP, EX_GO_DOWNLOADS, EX_GO_DISK, EX_GO_COMPUTER, EX_GO_TRASH, EX_EMPTY_TRASH,
       EX_MINIMIZE, EX_ZOOM };

static void do_file_op(window_t *w, int id)
{
    explorer_t *e = w->data;
    fnode_t *dir = fs_lookup(e->path);
    char name[64];
    load(e);
    if (id == EX_NEWFOLDER && dir) {
        fs_unique_name(dir, "untitled folder", "", name, sizeof name);
        fs_create_in(dir, name, true);
    } else if (id == EX_NEWFILE && dir) {
        fs_unique_name(dir, "untitled", "txt", name, sizeof name);
        fs_create_in(dir, name, false);
    } else if (id == EX_DELETE && e->sel >= 0 && e->sel < n_entries) {
        fnode_t *f = fs_lookup(entries[e->sel].target);
        if (f) {
            if (!strcasecmp(e->path, "/Recycled"))
                fs_remove(f);
            else
                fs_move(f, fs_lookup("/Recycled"));
        }
        e->sel = -1;
    } else if (id == EX_RENAME && e->sel >= 0 && e->sel < n_entries) {
        prompt_box(w, "Rename", "Enter a new name:", entries[e->sel].name, rename_done, w);
        return;
    } else if (id == EX_EMPTY_TRASH) {
        fnode_t *bin = fs_lookup("/Recycled");
        while (bin && bin->child)
            fs_remove(bin->child);
    }
    refresh(w);
    desktop_refresh_icons();
    dock_changed();
}

static void command(int id, void *ctx)
{
    window_t *w = ctx;
    if (!wm_alive(w))
        return;
    explorer_t *e = w->data;
    bool fs_op = id == EX_NEWFILE || id == EX_NEWFOLDER || id == EX_DELETE || id == EX_RENAME;
    if (is_computer(e) && fs_op)
        return;
    switch (id) {
    case EX_CLOSE: wm_close(w); break;
    case EX_REFRESH: refresh(w); break;
    case EX_UP: go_up(w); break;
    case EX_BACK: go_back(w); break;
    case EX_FORWARD: go_forward(w); break;
    case EX_OPEN: load(e), open_entry(w, e->sel); break;
    case EX_ABOUT: ui_launch("about"); break;
    case EX_NEWWIN: ui_launch("finder"); break;
    case EX_GO_DOCS: navigate(w, "/Documents", true); break;
    case EX_GO_DESKTOP: navigate(w, "/Desktop", true); break;
    case EX_GO_DOWNLOADS: navigate(w, "/Downloads", true); break;
    case EX_GO_DISK: navigate(w, "/", true); break;
    case EX_GO_COMPUTER: navigate(w, "::computer", true); break;
    case EX_GO_TRASH: navigate(w, "/Recycled", true); break;
    case EX_MINIMIZE: wm_minimize(w); break;
    case EX_ZOOM: wm_toggle_maximize(w); break;
    default:
        if (fs_op || id == EX_EMPTY_TRASH)
            do_file_op(w, id);
        break;
    }
}

/* ---- menus ------------------------------------------------------------------------------------------------- */

static const menu_item_t file_menu[] = {
    { "New Finder Window", EX_NEWWIN, 0, 0, NULL, 0, "Ctrl+N" },
    { "New Folder", EX_NEWFOLDER, 0, 0, NULL, 0, NULL },
    { "New Text Document", EX_NEWFILE, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Open", EX_OPEN, 0, 0, NULL, 0, "Ctrl+O" },
    { "Close Window", EX_CLOSE, 0, 0, NULL, 0, "Ctrl+W" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Rename", EX_RENAME, 0, 0, NULL, 0, NULL },
    { "Move to Trash", EX_DELETE, 0, 0, NULL, 0, NULL },
    { "Empty Trash", EX_EMPTY_TRASH, 0, 0, NULL, 0, NULL },
};
static const menu_item_t edit_menu[] = {
    { "Undo", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+Z" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Cut", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+X" },
    { "Copy", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+C" },
    { "Paste", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+V" },
};
static const menu_item_t view_menu[] = {
    { "as Icons", 0, MI_CHECK, 0, NULL, 0, "Ctrl+1" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Refresh", EX_REFRESH, 0, 0, NULL, 0, NULL },
};
static const menu_item_t go_menu[] = {
    { "Back", EX_BACK, 0, 0, NULL, 0, "Ctrl+[" },
    { "Forward", EX_FORWARD, 0, 0, NULL, 0, "Ctrl+]" },
    { "Enclosing Folder", EX_UP, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Documents", EX_GO_DOCS, 0, 0, NULL, 0, NULL },
    { "Desktop", EX_GO_DESKTOP, 0, 0, NULL, 0, NULL },
    { "Downloads", EX_GO_DOWNLOADS, 0, 0, NULL, 0, NULL },
    { "Computer", EX_GO_COMPUTER, 0, 0, NULL, 0, NULL },
    { "Kepler HD", EX_GO_DISK, 0, 0, NULL, 0, NULL },
    { "Trash", EX_GO_TRASH, 0, 0, NULL, 0, NULL },
};
static const menu_item_t window_menu[] = {
    { "Minimize", EX_MINIMIZE, 0, 0, NULL, 0, "Ctrl+M" },
    { "Zoom", EX_ZOOM, 0, 0, NULL, 0, NULL },
};
static const menu_item_t help_menu[] = {
    { "About KeplerOS", EX_ABOUT, 0, 0, NULL, 0, NULL },
};
static const menubar_entry_t bar[] = {
    { "File", file_menu, ARRAY_LEN(file_menu) }, { "Edit", edit_menu, ARRAY_LEN(edit_menu) },
    { "View", view_menu, ARRAY_LEN(view_menu) }, { "Go", go_menu, ARRAY_LEN(go_menu) },
    { "Window", window_menu, ARRAY_LEN(window_menu) }, { "Help", help_menu, ARRAY_LEN(help_menu) },
};

static const menu_item_t item_menu[] = {
    { "Open", EX_OPEN, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Move to Trash", EX_DELETE, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Rename", EX_RENAME, 0, 0, NULL, 0, NULL },
};
static const menu_item_t bg_menu[] = {
    { "New Folder", EX_NEWFOLDER, 0, 0, NULL, 0, NULL },
    { "New Text Document", EX_NEWFILE, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Refresh", EX_REFRESH, 0, 0, NULL, 0, NULL },
};

/* ---- input --------------------------------------------------------------------------------------------------- */

static int entry_at(window_t *w, explorer_t *e, int x, int y)
{
    rect_t cr = content_rect(w);
    for (int i = 0; i < n_entries; i++) {
        rect_t r = cell_rect(w, e, i);
        r.x += cr.x + 10;
        r.y += cr.y;
        r.w -= 20;
        r.h -= 8;
        if (rect_contains(r, x, y))
            return i;
    }
    return -1;
}

static int tool_at(window_t *w, int x, int y)
{
    int cw = wm_client_rect(w).w;
    for (int i = 0; i < N_TOOLS; i++) {
        rect_t r = tool_rect(cw, i);
        if ((i <= T_FWD || r.x >= SIDEBAR_W + 70) && rect_contains(r, x, y))
            return i;
    }
    return -1;
}

static bool ex_caption_hit(window_t *w, int x, int y) { return tool_at(w, x, y) >= 0; }

static void max_scroll(window_t *w, explorer_t *e)
{
    int rows = (n_entries + cols(w) - 1) / cols(w);
    int max = MAX(0, 14 + rows * CELL_H - content_rect(w).h);
    e->scroll = CLAMP(e->scroll, 0, max);
}

static void ex_mouse(window_t *w, int kind, int x, int y, int button)
{
    explorer_t *e = w->data;
    load(e);
    rect_t cr = content_rect(w);
    int tool = kind == MS_LEAVE ? -1 : tool_at(w, x, y);
    int sh = kind == MS_LEAVE || y < TOOLBAR_H ? -1 : side_at(x, y);
    if (tool != e->hover_tool || sh != e->hover_side) {
        e->hover_tool = tool;
        e->hover_side = sh;
        wm_invalidate_client(w);
    }
    if (kind == MS_WHEEL && rect_contains(cr, x, y)) {
        e->scroll += button * 40;
        max_scroll(w, e);
        wm_invalidate_client(w);
        return;
    }
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        e->pressed_tool = tool;
        if (tool >= 0) {
            wm_invalidate_client(w);
            return;
        }
        if (sh >= 0 && !is_tag(sh)) {
            const char *t = side[sh].target;
            if (t[0] == '!')
                ui_launch(t + 1);
            else if (t[0] == '/' || t[0] == ':')
                navigate(w, t, true);
            return;
        }
        if (rect_contains(cr, x, y)) {
            int i = entry_at(w, e, x, y);
            if (i != e->sel) {
                e->sel = i;
                wm_invalidate_client(w);
            }
        }
        return;
    }
    if (kind == MS_UP && button == MOUSE_LEFT) {
        int p = e->pressed_tool;
        e->pressed_tool = -1;
        if (p >= 0 && p == tool) {
            if (p == T_BACK)
                go_back(w);
            else if (p == T_FWD)
                go_forward(w);
            else if (p == T_SEARCH)
                ui_launch("spotlight");
        }
        wm_invalidate_client(w);
        return;
    }
    if (kind == MS_DBLCLICK && rect_contains(cr, x, y)) {
        open_entry(w, entry_at(w, e, x, y));
        return;
    }
    if (kind == MS_DOWN && button == MOUSE_RIGHT && rect_contains(cr, x, y)) {
        int i = entry_at(w, e, x, y);
        e->sel = i;
        wm_invalidate_client(w);
        rect_t c = wm_client_rect(w);
        if (i >= 0)
            menu_open(item_menu, is_computer(e) ? 1 : ARRAY_LEN(item_menu), c.x + x, c.y + y, command, w);
        else if (!is_computer(e))
            menu_open(bg_menu, ARRAY_LEN(bg_menu), c.x + x, c.y + y, command, w);
    }
}

static void ex_key(window_t *w, const input_event_t *ev)
{
    explorer_t *e = w->data;
    if (!ev->pressed)
        return;
    load(e);
    int c = cols(w);
    switch (ev->key) {
    case KEY_BACKSPACE: go_up(w); break;
    case KEY_ENTER: open_entry(w, e->sel); break;
    case KEY_DELETE: command(EX_DELETE, w); break;
    case KEY_F2: command(EX_RENAME, w); break;
    case KEY_F5: refresh(w); break;
    case KEY_LEFT: e->sel = e->sel <= 0 ? 0 : e->sel - 1; break;
    case KEY_RIGHT: e->sel = MIN(n_entries - 1, e->sel + 1); break;
    case KEY_UP: e->sel = e->sel < 0 ? 0 : MAX(e->sel % c, e->sel - c); break;
    case KEY_DOWN: e->sel = e->sel < 0 ? 0 : MIN(n_entries - 1, e->sel + c); break;
    default: return;
    }
    wm_invalidate_client(w);
}

static void ex_paint_wrap(window_t *w, gfx_t *g, int cw, int ch)
{
    load(w->data);
    ex_paint(w, g, cw, ch);
}

static void ex_close(window_t *w) { kfree(w->data); }

static const window_ops_t ex_ops = { ex_paint_wrap, ex_key, ex_mouse, NULL, ex_close, NULL, ex_caption_hit };

window_t *explorer_open(const char *path)
{
    explorer_t *e = kzalloc(sizeof *e);
    e->sel = e->hover_side = e->hover_tool = e->pressed_tool = -1;
    e->mb.entries = bar;
    e->mb.n = ARRAY_LEN(bar);
    e->mb.open = e->mb.hover = -1;
    if (strcmp(path, "::computer")) {
        fnode_t *f = fs_lookup(path);
        if (f && f->dir)
            fs_path(f, e->path, sizeof e->path);
        else
            strlcpy(e->path, "/Documents", sizeof e->path);
    } else {
        strlcpy(e->path, path, sizeof e->path);
    }
    load(e);
    window_t *w = wm_create(nice_name(e->path), folder_icon(e->path), 800, 500, &ex_ops, e, WF_RESIZABLE | WF_UNIFIED);
    w->caption_h = TOOLBAR_H;
    w->min_w = 560;
    w->min_h = 320;
    w->bg = HEX(0xFFFFFF);
    wm_set_app(w, "Finder", &e->mb, command, w);
    return w;
}
