/* Kepler Explorer: My Computer and folder windows with the blue task pane. */

#include "apps.h"
#include "ramfs.h"

#define TOOLBAR_H 38
#define ADDR_H    26
#define PANE_W    206
#define TILE_W    252
#define TILE_H    62

typedef struct {
    char path[128]; /* "::computer" or an absolute path */
    char back[8][128];
    int n_back;
    char fwd[8][128];
    int n_fwd;
    int sel;
    int hover_link, hover_tool;
    int pressed_tool;
    int scroll;
    menubar_t mb;
} explorer_t;

typedef struct {
    char name[64];
    char line2[64], line3[64];
    int icon;
    char target[160]; /* path or ui_launch command prefixed with '!' */
    int usage;        /* drives: percent used, -1 otherwise */
    int group;
} entry_t;

#define MAX_ENTRIES 64
static entry_t entries[MAX_ENTRIES];
static int n_entries;

typedef struct {
    const char *label;
    int icon;
    const char *action;
} link_t;

/* ---- model ------------------------------------------------------------------------------------------- */

static bool is_computer(explorer_t *e) { return !strcmp(e->path, "::computer"); }

static const char *nice_name(const char *path)
{
    if (!strcmp(path, "::computer"))
        return "My Computer";
    if (!strcmp(path, "/"))
        return "Local Disk (C:)";
    if (!strcasecmp(path, "/Documents"))
        return "My Documents";
    if (!strcasecmp(path, "/Documents/My Pictures"))
        return "My Pictures";
    if (!strcasecmp(path, "/Recycled"))
        return "Recycle Bin";
    if (!strcasecmp(path, "/Desktop"))
        return "Desktop";
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
        return ICON_PICTURES;
    if (!strcasecmp(path, "/Recycled"))
        return ICON_RECYCLE;
    return ICON_FOLDER;
}

static bool is_image(const char *name)
{
    const char *x = fs_ext(name);
    return !strcasecmp(x, "jpg") || !strcasecmp(x, "jpeg");
}

static const char *type_name(const fnode_t *f)
{
    if (f->dir)
        return "File Folder";
    if (is_image(f->name))
        return "JPEG Image";
    const char *x = fs_ext(f->name);
    if (!strcasecmp(x, "txt"))
        return "Text Document";
    if (!strcasecmp(x, "ini"))
        return "Configuration Settings";
    return "File";
}

static void size_str(size_t n, char *out, int len)
{
    if (n < 1024)
        ksnprintf(out, len, "%u bytes", (unsigned)n);
    else if (n < 1024 * 1024)
        ksnprintf(out, len, "%u KB", (unsigned)((n + 1023) / 1024));
    else
        ksnprintf(out, len, "%u.%u MB", (unsigned)(n >> 20), (unsigned)((n % (1 << 20)) * 10 >> 20));
}

static void load(explorer_t *e)
{
    n_entries = 0;
    if (is_computer(e)) {
        entry_t *x = &entries[n_entries++];
        memset(x, 0, sizeof *x);
        ksnprintf(x->name, sizeof x->name, "%s's Documents", g_user_name);
        strlcpy(x->line2, "File Folder", sizeof x->line2);
        x->icon = ICON_DOCUMENTS;
        strlcpy(x->target, "/Documents", sizeof x->target);
        x->usage = -1;
        x->group = 0;

        x = &entries[n_entries++];
        memset(x, 0, sizeof *x);
        strlcpy(x->name, "Local Disk (C:)", sizeof x->name);
        size_t total = heap_total(), used = heap_used();
        ksnprintf(x->line3, sizeof x->line3, "%u MB free of %u MB", (unsigned)((total - used) >> 20), (unsigned)(total >> 20));
        strlcpy(x->line2, "RAM Disk", sizeof x->line2);
        x->icon = ICON_DRIVE;
        strlcpy(x->target, "/", sizeof x->target);
        x->usage = (int)(used * 100 / total);
        x->group = 1;

        x = &entries[n_entries++];
        memset(x, 0, sizeof *x);
        strlcpy(x->name, "CD Drive (D:)", sizeof x->name);
        strlcpy(x->line2, "CD Drive", sizeof x->line2);
        x->icon = ICON_CDROM;
        strlcpy(x->target, "!cd", sizeof x->target);
        x->usage = -1;
        x->group = 2;
        return;
    }
    fnode_t *d = fs_lookup(e->path);
    for (fnode_t *f = d ? d->child : NULL; f && n_entries < MAX_ENTRIES; f = f->next) {
        entry_t *x = &entries[n_entries++];
        memset(x, 0, sizeof *x);
        strlcpy(x->name, f->name, sizeof x->name);
        strlcpy(x->line2, type_name(f), sizeof x->line2);
        if (!f->dir)
            size_str(f->size, x->line3, sizeof x->line3);
        else
            ksnprintf(x->line3, sizeof x->line3, "%d item%s", fs_count(f), fs_count(f) == 1 ? "" : "s");
        x->icon = f->dir ? ICON_FOLDER : is_image(f->name) ? ICON_PICTURES : ICON_TEXTFILE;
        fs_path(f, x->target, sizeof x->target);
        x->usage = -1;
    }
}

/* ---- geometry ------------------------------------------------------------------------------------------- */

static rect_t content_rect(window_t *w)
{
    rect_t c = wm_client_rect(w);
    int top = MENUBAR_H + TOOLBAR_H + ADDR_H;
    return rect_make(PANE_W, top, c.w - PANE_W, c.h - top);
}

static const char *group_names[3] = { "Files Stored on This Computer", "Hard Disk Drives", "Devices with Removable Storage" };

/* tile position in content coordinates */
static rect_t tile_rect(window_t *w, explorer_t *e, int i)
{
    rect_t cr = content_rect(w);
    int cols = MAX(1, (cr.w - 24) / TILE_W);
    if (is_computer(e)) {
        int y = 12, gi = -1, col = 0;
        for (int k = 0; k <= i; k++) {
            if (entries[k].group != gi) {
                if (gi >= 0)
                    y += TILE_H + 8;
                gi = entries[k].group;
                y += 30;
                col = 0;
            } else if (++col >= cols) {
                col = 0;
                y += TILE_H + 4;
            }
        }
        return rect_make(16 + col * TILE_W, y - e->scroll, TILE_W - 8, TILE_H);
    }
    return rect_make(16 + (i % cols) * TILE_W, 14 + (i / cols) * (TILE_H + 6) - e->scroll, TILE_W - 8, TILE_H);
}

/* ---- task pane ------------------------------------------------------------------------------------------- */

#define MAX_LINKS 12
static link_t pane_links[MAX_LINKS];
static rect_t link_rects[MAX_LINKS];
static int n_links;

static void add_link(const char *label, int icon, const char *action)
{
    if (n_links < MAX_LINKS)
        pane_links[n_links++] = (link_t){ label, icon, action };
}

static int panel(gfx_t *g, int y, const char *title, bool special, int first_link, int last_link, explorer_t *e,
                 const char *details[], int n_details)
{
    int x = 12, w = PANE_W - 24;
    if (special) {
        gfx_round_rect_vgrad(g, x, y, w, 25, 4, HEX(0x0C59DB), HEX(0x2869E0), CORNER_TOP);
    } else {
        gfx_round_rect(g, x, y, w, 25, 4, HEX(0xFFFFFF), CORNER_TOP);
        gfx_hgrad(g, x + 4, y + 4, w - 4, 21, HEX(0xFFFFFF), HEX(0xC6D3F7));
        gfx_hgrad(g, x + 4, y, w - 8, 4, HEX(0xFFFFFF), HEX(0xD8E2F8));
    }
    gfx_text(g, &font_ui_bold, x + 12, y + 7, title, special ? HEX(0xFFFFFF) : HEX(0x215DC6));
    /* collapse chevron */
    int cx = x + w - 13, cy = y + 12;
    gfx_circle(g, cx, cy, 9, special ? HEX(0x6E9BEB) : HEX(0xB5C7EF));
    gfx_circle(g, cx, cy, 8, HEX(0xFFFFFF));
    uint32_t cc = special ? HEX(0x215DC6) : HEX(0x215DC6);
    for (int k = 0; k < 3; k++) {
        gfx_hline(g, cx - 3 + k - 1, cy - 2 - k + 1, 2, cc);
        gfx_hline(g, cx + 2 - k - 1, cy - 2 - k + 1, 2, cc);
        gfx_hline(g, cx - 3 + k - 1, cy + 2 - k + 1, 2, cc);
        gfx_hline(g, cx + 2 - k - 1, cy + 2 - k + 1, 2, cc);
    }
    y += 25;
    int body_h = 10 + (last_link - first_link) * 22 + n_details * 16 + (n_details ? 4 : 0);
    gfx_fill(g, x, y, w, body_h, special ? HEX(0xEFF3FF) : HEX(0xD6DFF7));
    gfx_rect(g, x, y - 1, w, body_h + 1, HEX(0xFFFFFF));
    int ly = y + 8;
    for (int i = first_link; i < last_link; i++) {
        bool hot = e->hover_link == i;
        if (pane_links[i].icon >= 0)
            gfx_blit_alpha(g, x + 10, ly, icon_get(pane_links[i].icon, 16), 255);
        uint32_t tc = hot ? HEX(0x428EFF) : HEX(0x215DC6);
        int tx = x + 32;
        gfx_text(g, &font_ui, tx, ly + 2, pane_links[i].label, tc);
        if (hot)
            gfx_hline(g, tx, ly + 2 + font_ui.ascent + 1, text_width(&font_ui, pane_links[i].label), tc);
        link_rects[i] = rect_make(x + 6, ly - 2, w - 12, 20);
        ly += 22;
    }
    for (int i = 0; i < n_details; i++) {
        gfx_text_clipped(g, i == 0 ? &font_ui_bold : &font_ui, x + 12, ly + 1, w - 20, details[i], HEX(0x000000));
        ly += 16;
    }
    return y + body_h + 14;
}

static void build_links(explorer_t *e)
{
    n_links = 0;
    if (is_computer(e)) {
        add_link("View system information", ICON_SYSINFO, "!sysprops");
        add_link("Change a setting", ICON_DISPLAY, "!display");
        add_link("Run a program", ICON_RUN, "!run");
        add_link("My Documents", ICON_DOCUMENTS, "/Documents");
        add_link("Recycle Bin", ICON_RECYCLE, "/Recycled");
        add_link("Control Panel", ICON_DISPLAY, "!display");
    } else {
        add_link("Make a new folder", ICON_FOLDER, "#newfolder");
        add_link("Make a new text file", ICON_TEXTFILE, "#newfile");
        if (e->sel >= 0) {
            add_link("Rename this item", ICON_TEXTFILE, "#rename");
            add_link("Delete this item", ICON_RECYCLE, "#delete");
        }
        add_link("My Computer", ICON_COMPUTER, "::computer");
        add_link("My Documents", ICON_DOCUMENTS, "/Documents");
        add_link("Command Prompt", ICON_CMD, "!cmd");
    }
}

static void paint_pane(window_t *w, explorer_t *e, gfx_t *g, int h)
{
    gfx_vgrad(g, 0, 0, PANE_W, h, HEX(0x7BA2E7), HEX(0x6375D6));
    build_links(e);
    int y = 12;
    char d0[64], d1[64], d2[64];
    const char *details[3] = { d0, d1, d2 };
    int nd = 0;
    if (is_computer(e)) {
        y = panel(g, y, "System Tasks", true, 0, 3, e, NULL, 0);
        y = panel(g, y, "Other Places", false, 3, 6, e, NULL, 0);
        strlcpy(d0, "My Computer", sizeof d0);
        strlcpy(d1, "System Folder", sizeof d1);
        nd = 2;
    } else {
        int split = e->sel >= 0 ? 4 : 2;
        y = panel(g, y, "File and Folder Tasks", true, 0, split, e, NULL, 0);
        y = panel(g, y, "Other Places", false, split, n_links, e, NULL, 0);
        if (e->sel >= 0 && e->sel < n_entries) {
            strlcpy(d0, entries[e->sel].name, sizeof d0);
            strlcpy(d1, entries[e->sel].line2, sizeof d1);
            strlcpy(d2, entries[e->sel].line3, sizeof d2);
            nd = 3;
        } else {
            strlcpy(d0, nice_name(e->path), sizeof d0);
            strlcpy(d1, "File Folder", sizeof d1);
            ksnprintf(d2, sizeof d2, "%d object%s", n_entries, n_entries == 1 ? "" : "s");
            nd = 3;
        }
    }
    panel(g, y, "Details", false, 0, 0, e, details, nd);
}

/* ---- painting ------------------------------------------------------------------------------------------------ */

static const char *tool_labels[] = { "Back", "", "Search", "Folders" };
static int tool_icons[] = { ICON_BACK, ICON_FORWARD, -1, -1 };

static rect_t tool_rect(int i)
{
    static const int xs[] = { 4, 82, 146, 220 }, ws[] = { 74, 30, 70, 74 };
    return rect_make(xs[i], MENUBAR_H + 4, ws[i], TOOLBAR_H - 8);
}

static rect_t up_rect(void) { return rect_make(116, MENUBAR_H + 4, 30, TOOLBAR_H - 8); }

static void paint_tile(gfx_t *g, entry_t *x, rect_t r, bool sel)
{
    const image_t *ic = icon_get(x->icon, 48);
    if (sel) {
        gfx_round_rect(g, r.x - 2, r.y, r.w, r.h, 3, HEX(0xE1ECFC), CORNER_ALL);
        gfx_round_frame(g, r.x - 2, r.y, r.w, r.h, 3, HEX(0x7DA2CE), CORNER_ALL);
    }
    gfx_blit_alpha(g, r.x + 4, r.y + (r.h - 48) / 2, ic, 255);
    int tx = r.x + 60, ty = r.y + 8;
    gfx_text_clipped(g, &font_ui, tx, ty, r.w - 66, x->name, HEX(0x000000));
    gfx_text_clipped(g, &font_ui, tx, ty + 15, r.w - 66, x->line2, HEX(0x7F7F7F));
    if (x->usage >= 0) {
        int bw = MIN(150, r.w - 70);
        gfx_fill(g, tx, ty + 32, bw, 10, HEX(0xFFFFFF));
        gfx_rect(g, tx, ty + 32, bw, 10, HEX(0x9C9C9C));
        int fw = (bw - 2) * MAX(2, x->usage) / 100;
        gfx_vgrad(g, tx + 1, ty + 33, fw, 8, HEX(0x5FA8FF), HEX(0x1E64D6));
        gfx_text_clipped(g, &font_ui, tx, ty + 44 - 2, r.w - 66, x->line3, HEX(0x7F7F7F));
    } else if (x->line3[0]) {
        gfx_text_clipped(g, &font_ui, tx, ty + 30, r.w - 66, x->line3, HEX(0x7F7F7F));
    }
}

static void ex_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    explorer_t *e = w->data;
    menubar_paint(&e->mb, g, cw);

    /* toolbar */
    int ty = MENUBAR_H;
    gfx_vgrad(g, 0, ty, cw, TOOLBAR_H, HEX(0xF8F7F2), HEX(0xE6E3D3));
    gfx_hline(g, 0, ty + TOOLBAR_H - 1, cw, HEX(0xC9C3AF));
    for (int i = 0; i < 4; i++) {
        rect_t r = tool_rect(i);
        bool enabled = i == 0 ? e->n_back > 0 : i == 1 ? e->n_fwd > 0 : true;
        if (e->hover_tool == i && enabled) {
            gfx_round_rect(g, r.x, r.y, r.w, r.h, 3, e->pressed_tool == i ? HEX(0xE2DFD3) : HEX(0xFBFAF6), CORNER_ALL);
            gfx_round_frame(g, r.x, r.y, r.w, r.h, 3, HEX(0xCEC9B4), CORNER_ALL);
        }
        int ix = r.x + 4;
        if (tool_icons[i] >= 0)
            gfx_blit_alpha(g, ix, r.y + (r.h - 24) / 2, icon_get(tool_icons[i], 24), enabled ? 255 : 90);
        else if (i == 2) {
            gfx_circle(g, ix + 10, r.y + 12, 7, HEX(0x5A6B85));
            gfx_circle(g, ix + 10, r.y + 12, 5, HEX(0xDCEBFF));
            gfx_thick_line(g, ix + 15, r.y + 17, ix + 20, r.y + 22, 1, HEX(0x8B5A2B));
        } else {
            gfx_blit_alpha(g, ix, r.y + (r.h - 24) / 2, icon_get(ICON_FOLDER, 24), 255);
        }
        if (tool_labels[i][0])
            gfx_text(g, &font_ui, ix + 28, r.y + (r.h - font_ui.height) / 2 + 1, tool_labels[i], enabled ? HEX(0x000000) : HEX(0xA0A0A0));
    }
    rect_t ur = up_rect();
    bool up_en = !is_computer(e);
    if (e->hover_tool == 4 && up_en)
        gfx_round_frame(g, ur.x, ur.y, ur.w, ur.h, 3, HEX(0xCEC9B4), CORNER_ALL);
    gfx_blit_alpha(g, ur.x + 3, ur.y + (ur.h - 24) / 2, icon_get(ICON_UP, 24), up_en ? 255 : 90);
    gfx_vline(g, 140 + 2, ty + 6, TOOLBAR_H - 12, HEX(0xC9C3AF));

    /* address bar */
    int ay = MENUBAR_H + TOOLBAR_H;
    gfx_fill(g, 0, ay, cw, ADDR_H, COL_FACE);
    gfx_hline(g, 0, ay + ADDR_H - 1, cw, HEX(0xC9C3AF));
    gfx_text(g, &font_ui, 8, ay + 7, "Address", HEX(0x7F7F7F));
    draw_textbox(g, 58, ay + 3, cw - 58 - 52, 20);
    gfx_blit_alpha(g, 61, ay + 5, icon_get(folder_icon(e->path), 16), 255);
    char addr[160];
    if (is_computer(e))
        strlcpy(addr, "My Computer", sizeof addr);
    else {
        strlcpy(addr, "C:", sizeof addr);
        for (const char *p = e->path; *p; p++) {
            char c[2] = { *p == '/' ? '\\' : *p, 0 };
            strlcat(addr, c, sizeof addr);
        }
        if (!strcmp(e->path, "/"))
            strlcpy(addr, "C:\\", sizeof addr);
    }
    gfx_text(g, &font_ui, 82, ay + 7, addr, HEX(0x000000));
    gfx_blit_alpha(g, cw - 48, ay + 4, icon_get(ICON_FORWARD, 16), 255);
    gfx_text(g, &font_ui, cw - 28, ay + 7, "Go", HEX(0x000000));

    /* task pane and content */
    rect_t cr = content_rect(w);
    gfx_t pg = gfx_sub(g, 0, cr.y, PANE_W, cr.h);
    paint_pane(w, e, &pg, cr.h);

    gfx_t cg = gfx_sub(g, cr.x, cr.y, cr.w, cr.h);
    gfx_fill(&cg, 0, 0, cr.w, cr.h, HEX(0xFFFFFF));
    int last_group = -1;
    for (int i = 0; i < n_entries; i++) {
        rect_t r = tile_rect(w, e, i);
        if (is_computer(e) && entries[i].group != last_group) {
            last_group = entries[i].group;
            int hy = r.y - 28;
            gfx_text(&cg, &font_ui_bold, 16, hy + 2, group_names[last_group], HEX(0x000000));
            gfx_hgrad(&cg, 12, hy + 19, 360, 2, HEX(0x70BFFF), HEX(0xFFFFFF));
        }
        paint_tile(&cg, &entries[i], r, i == e->sel);
    }
    if (!n_entries)
        gfx_text(&cg, &font_ui, 20, 20, "This folder is empty.", HEX(0x7F7F7F));
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
    taskbar_changed();
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
    strlcpy(e->fwd[e->n_fwd++ % 8], e->path, sizeof e->fwd[0]);
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
        msgbox(w, "D:\\", "Please insert a disk into drive D:.", ICON_INFO);
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
    if (e->sel < 0)
        return;
    fnode_t *f = fs_lookup(entries[e->sel].target);
    if (f && value[0] && !fs_rename(f, value))
        msgbox(w, "Error Renaming File", "Cannot rename: a file with the name you specified already exists.", ICON_ERROR);
    refresh(w);
    desktop_refresh_icons();
}

static void do_action(window_t *w, const char *action)
{
    explorer_t *e = w->data;
    if (action[0] == '!') {
        ui_launch(action + 1);
        return;
    }
    if (action[0] == '#') {
        fnode_t *dir = fs_lookup(e->path);
        char name[64];
        if (!strcmp(action, "#newfolder") && dir) {
            fs_unique_name(dir, "New Folder", "", name, sizeof name);
            fs_create_in(dir, name, true);
        } else if (!strcmp(action, "#newfile") && dir) {
            fs_unique_name(dir, "New Text Document", "txt", name, sizeof name);
            fs_create_in(dir, name, false);
        } else if (!strcmp(action, "#delete") && e->sel >= 0) {
            fnode_t *f = fs_lookup(entries[e->sel].target);
            if (f) {
                if (!strcasecmp(e->path, "/Recycled"))
                    fs_remove(f);
                else
                    fs_move(f, fs_lookup("/Recycled"));
            }
            e->sel = -1;
        } else if (!strcmp(action, "#rename") && e->sel >= 0) {
            prompt_box(w, "Rename", "New name:", entries[e->sel].name, rename_done, w);
            return;
        }
        refresh(w);
        desktop_refresh_icons();
        return;
    }
    navigate(w, action, true);
}

/* ---- menus ------------------------------------------------------------------------------------------------- */

enum { EX_CLOSE = 1, EX_REFRESH, EX_UP, EX_NEWFOLDER, EX_NEWFILE, EX_DELETE, EX_RENAME, EX_OPEN, EX_ABOUT, EX_SELALL };

static const menu_item_t new_menu[] = {
    { "Folder", EX_NEWFOLDER, 0, ICON_FOLDER, NULL, 0, NULL },
    { "Text Document", EX_NEWFILE, 0, ICON_TEXTFILE, NULL, 0, NULL },
};
static const menu_item_t file_menu[] = {
    { "New", 0, 0, 0, new_menu, 2, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Delete", EX_DELETE, 0, 0, NULL, 0, "Del" },
    { "Rename", EX_RENAME, 0, 0, NULL, 0, "F2" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Close", EX_CLOSE, 0, 0, NULL, 0, NULL },
};
static const menu_item_t edit_menu[] = {
    { "Undo", 0, MI_DISABLED, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Select All", EX_SELALL, MI_DISABLED, 0, NULL, 0, "Ctrl+A" },
};
static const menu_item_t view_menu[] = {
    { "Tiles", 0, MI_RADIO, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Go To Parent Folder", EX_UP, 0, 0, NULL, 0, "Backspace" },
    { "Refresh", EX_REFRESH, 0, 0, NULL, 0, "F5" },
};
static const menu_item_t fav_menu[] = {
    { "My Documents", 0, MI_DISABLED, ICON_DOCUMENTS, NULL, 0, NULL },
};
static const menu_item_t tools_menu[] = {
    { "Folder Options...", 0, MI_DISABLED, 0, NULL, 0, NULL },
};
static const menu_item_t help_menu[] = {
    { "About KeplerOS", EX_ABOUT, 0, 0, NULL, 0, NULL },
};
static const menubar_entry_t bar[] = {
    { "File", file_menu, ARRAY_LEN(file_menu) },   { "Edit", edit_menu, ARRAY_LEN(edit_menu) },
    { "View", view_menu, ARRAY_LEN(view_menu) },   { "Favorites", fav_menu, ARRAY_LEN(fav_menu) },
    { "Tools", tools_menu, ARRAY_LEN(tools_menu) }, { "Help", help_menu, ARRAY_LEN(help_menu) },
};

static const menu_item_t item_menu[] = {
    { "Open", EX_OPEN, MI_BOLD, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Delete", EX_DELETE, 0, 0, NULL, 0, NULL },
    { "Rename", EX_RENAME, 0, 0, NULL, 0, NULL },
};
static const menu_item_t bg_menu[] = {
    { "Refresh", EX_REFRESH, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "New", 0, 0, 0, new_menu, 2, NULL },
};

static void command(int id, void *ctx)
{
    window_t *w = ctx;
    if (!wm_alive(w))
        return;
    explorer_t *e = w->data;
    if (is_computer(e) && (id == EX_NEWFILE || id == EX_NEWFOLDER || id == EX_DELETE || id == EX_RENAME))
        return;
    switch (id) {
    case EX_CLOSE: wm_close(w); break;
    case EX_REFRESH: refresh(w); break;
    case EX_UP: go_up(w); break;
    case EX_NEWFOLDER: do_action(w, "#newfolder"); break;
    case EX_NEWFILE: do_action(w, "#newfile"); break;
    case EX_DELETE: do_action(w, "#delete"); break;
    case EX_RENAME: do_action(w, "#rename"); break;
    case EX_OPEN: open_entry(w, e->sel); break;
    case EX_ABOUT: ui_launch("about"); break;
    }
}

/* ---- input --------------------------------------------------------------------------------------------------- */

static int entry_at(window_t *w, explorer_t *e, int x, int y)
{
    rect_t cr = content_rect(w);
    for (int i = 0; i < n_entries; i++) {
        rect_t r = tile_rect(w, e, i);
        r.x += cr.x;
        r.y += cr.y;
        if (rect_contains(r, x, y))
            return i;
    }
    return -1;
}

static void ex_mouse(window_t *w, int kind, int x, int y, int button)
{
    explorer_t *e = w->data;
    load(e);
    if (menubar_mouse(&e->mb, w, kind, x, y, command, w))
        return;
    rect_t cr = content_rect(w);

    int tool = -1;
    for (int i = 0; i < 4; i++)
        if (rect_contains(tool_rect(i), x, y))
            tool = i;
    if (rect_contains(up_rect(), x, y))
        tool = 4;
    int link = -1;
    if (x < PANE_W && y >= cr.y) {
        build_links(e);
        for (int i = 0; i < n_links; i++)
            if (rect_contains(link_rects[i], x, y - cr.y))
                link = i;
    }
    if (kind == MS_LEAVE)
        tool = link = -1;
    if (tool != e->hover_tool || link != e->hover_link) {
        e->hover_tool = tool;
        e->hover_link = link;
        wm_invalidate_client(w);
    }
    w->cursor = link >= 0 ? CURSOR_HAND : CURSOR_ARROW;

    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        e->pressed_tool = tool;
        if (tool >= 0) {
            wm_invalidate_client(w);
            return;
        }
        if (link >= 0) {
            char action[64];
            strlcpy(action, pane_links[link].action, sizeof action);
            do_action(w, action);
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
            if (p == 0)
                go_back(w);
            else if (p == 1)
                go_forward(w);
            else if (p == 4)
                go_up(w);
            else if (p == 3)
                navigate(w, "::computer", true);
            else if (p == 2)
                ui_launch("cmd");
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
    switch (ev->key) {
    case KEY_BACKSPACE: go_up(w); break;
    case KEY_ENTER: open_entry(w, e->sel); break;
    case KEY_DELETE: command(EX_DELETE, w); break;
    case KEY_F2: command(EX_RENAME, w); break;
    case KEY_F5: refresh(w); break;
    case KEY_LEFT:
    case KEY_UP:
        e->sel = e->sel <= 0 ? 0 : e->sel - 1;
        wm_invalidate_client(w);
        break;
    case KEY_RIGHT:
    case KEY_DOWN:
        e->sel = MIN(n_entries - 1, e->sel + 1);
        wm_invalidate_client(w);
        break;
    }
}

static void ex_paint_wrap(window_t *w, gfx_t *g, int cw, int ch)
{
    load(w->data);
    ex_paint(w, g, cw, ch);
}

static void ex_close(window_t *w) { kfree(w->data); }

static const window_ops_t ex_ops = { ex_paint_wrap, ex_key, ex_mouse, NULL, ex_close, NULL };

window_t *explorer_open(const char *path)
{
    explorer_t *e = kzalloc(sizeof *e);
    e->sel = e->hover_link = e->hover_tool = e->pressed_tool = -1;
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
    window_t *w = wm_create(nice_name(e->path), folder_icon(e->path), 780, 500, &ex_ops, e, WF_RESIZABLE);
    w->min_w = 520;
    w->min_h = 300;
    w->bg = HEX(0xFFFFFF);
    return w;
}
