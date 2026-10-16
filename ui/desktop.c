/* The desktop: wallpaper, icons down the right edge, selection, drag to
 * rearrange, context menus, and Finder's menus for when no window is open. */

#include "apps.h"
#include "ramfs.h"
#include "ui.h"

#define CELL_W 100
#define CELL_H 104
#define ICON_SZ 64
#define ORIGIN_Y (TOPBAR_H + 8)
#define MAX_ITEMS 48

typedef struct {
    char label[64];
    int icon;
    char cmd[160];
    char path[128]; /* file items live in /Desktop */
    int x, y;       /* top-left of the cell */
    bool system;
} ditem_t;

static ditem_t items[MAX_ITEMS];
static int n_items;
static int sel = -1;

static image_t *wall_src; /* the photo, screen sized */
static image_t *bg;       /* what is shown now */
static image_t *blurred;  /* bg, frosted, for the translucent surfaces */
static int bg_style;
static uint32_t bg_color = HEX(0x3A6EA5);

static struct {
    int idx;
    int sx, sy;
    bool active;
    int gx, gy; /* ghost position */
} drag = { -1 };

/* positions the user dragged icons to, by label */
static struct {
    char label[64];
    int x, y;
} placed[MAX_ITEMS];
static int n_placed;

const image_t *desktop_wallpaper(void) { return bg; }
image_t *desktop_wallpaper_source(void) { return wall_src; }
int desktop_background_style(void) { return bg_style; }
uint32_t desktop_background_color(void) { return bg_color; }

const image_t *ui_vibrancy(void) { return blurred; }

static int rows(void) { return (g_screen.h - DOCK_RESERVE - ORIGIN_Y) / CELL_H; }
/* columns count from the right edge, as on the Mac */
static int col_x(int c) { return g_screen.w - 8 - (c + 1) * CELL_W; }

static rect_t cell_rect(const ditem_t *it) { return rect_make(it->x, it->y, CELL_W, CELL_H); }

/* ---- backgrounds ------------------------------------------------------------------------- */

static void make_night(image_t *img)
{
    gfx_t g = gfx_for_image(img);
    g.argb = false;
    gfx_vgrad(&g, 0, 0, img->w, img->h, HEX(0x040A1F), HEX(0x1B3277));
    uint32_t seed = 0x1234567;
    int stars = img->w * img->h / 1100;
    for (int i = 0; i < stars; i++) {
        seed = seed * 1103515245 + 12345;
        int x = (seed >> 8) % img->w;
        seed = seed * 1103515245 + 12345;
        int y = (seed >> 8) % (img->h * 3 / 4);
        seed = seed * 1103515245 + 12345;
        int a = 60 + (seed >> 16) % 196;
        gfx_pixel(&g, x, y, RGBA(255, 255, 255, a));
        if (a > 235 && img->w > 400) {
            gfx_pixel(&g, x + 1, y, RGBA(200, 220, 255, a / 3));
            gfx_pixel(&g, x - 1, y, RGBA(200, 220, 255, a / 3));
            gfx_pixel(&g, x, y + 1, RGBA(200, 220, 255, a / 3));
            gfx_pixel(&g, x, y - 1, RGBA(200, 220, 255, a / 3));
        }
    }
    /* a big planet rising at the bottom */
    int r = img->w;
    int cx = img->w / 2, cy = img->h + r - img->h / 5;
    int glow = MAX(4, img->w / 32);
    for (int y = img->h / 2; y < img->h; y++)
        for (int x = 0; x < img->w; x++) {
            int64_t dx = x - cx, dy = y - cy;
            int64_t d2 = dx * dx + dy * dy;
            int64_t rr = (int64_t)r * r;
            if (d2 <= rr) {
                int depth = (int)((rr - d2) / (2 * r)) * 1280 / img->w;
                uint32_t c = color_lerp(HEX(0x58C4FF), HEX(0x0B2A6B), MIN(256, depth * 3));
                img->px[y * img->w + x] = c;
            } else if (d2 <= (int64_t)(r + glow) * (r + glow)) {
                int out = (int)((d2 - rr) / (2 * r)) * 40 / glow;
                int a = MAX(0, 120 - out * 3);
                img->px[y * img->w + x] = color_lerp(img->px[y * img->w + x], HEX(0x7FD8FF), a);
            }
        }
}

image_t *desktop_render_background(int style, uint32_t color, int w, int h)
{
    image_t *img;
    if (style == 0) {
        if (w == wall_src->w && h == wall_src->h) {
            img = image_new(w, h);
            memcpy(img->px, wall_src->px, (size_t)w * h * 4);
        } else {
            img = image_resize(wall_src, w, h);
        }
        return img;
    }
    img = image_new(w, h);
    if (style == 1) {
        make_night(img);
    } else if (style == 2) {
        gfx_t g = gfx_for_image(img);
        g.argb = false;
        gfx_vgrad(&g, 0, 0, w, h, HEX(0x6FA8F0), HEX(0x173F96));
    } else {
        memset32(img->px, color | 0xFF000000u, (size_t)w * h);
    }
    return img;
}

void desktop_set_background(int style, uint32_t color)
{
    bg_style = style;
    bg_color = color;
    image_free(bg);
    bg = desktop_render_background(style, color, g_screen.w, g_screen.h);
    image_free(blurred);
    blurred = image_blurred(bg, 72, 330);
    topbar_background_changed();
    ui_invalidate_all();
}

/* ---- items ---------------------------------------------------------------------------------- */

static bool cell_taken(int x, int y, int except)
{
    for (int i = 0; i < n_items; i++)
        if (i != except && items[i].x == x && items[i].y == y)
            return true;
    return false;
}

static void next_free_cell(int *x, int *y)
{
    for (int c = 0;; c++)
        for (int r = 0; r < rows(); r++) {
            int cx = col_x(c), cy = ORIGIN_Y + r * CELL_H;
            if (!cell_taken(cx, cy, -1)) {
                *x = cx;
                *y = cy;
                return;
            }
        }
}

static void add_item(const char *label, int icon, const char *cmd, const char *path, bool system)
{
    if (n_items >= MAX_ITEMS)
        return;
    ditem_t *it = &items[n_items];
    memset(it, 0, sizeof *it);
    strlcpy(it->label, label, sizeof it->label);
    it->icon = icon;
    strlcpy(it->cmd, cmd, sizeof it->cmd);
    if (path)
        strlcpy(it->path, path, sizeof it->path);
    it->system = system;
    it->x = it->y = -1000;
    for (int i = 0; i < n_placed; i++)
        if (!strcmp(placed[i].label, label)) {
            it->x = placed[i].x;
            it->y = placed[i].y;
        }
    n_items++;
    if (it->x < -100)
        next_free_cell(&it->x, &it->y);
}

static int icon_for_file(const fnode_t *f)
{
    if (f->dir)
        return ICON_FOLDER;
    const char *ext = fs_ext(f->name);
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg"))
        return ICON_IMAGEFILE;
    return ICON_TEXTFILE;
}

void desktop_refresh_icons(void)
{
    n_items = 0;
    sel = -1;
    /* the startup disk heads the column; apps live in the dock now */
    add_item("Kepler HD", ICON_DRIVE, "explorer /", NULL, true);

    fnode_t *d = fs_lookup("/Desktop");
    for (fnode_t *f = d ? d->child : NULL; f; f = f->next) {
        char path[128], cmd[160];
        fs_path(f, path, sizeof path);
        if (f->dir)
            ksnprintf(cmd, sizeof cmd, "explorer %s", path);
        else if (icon_for_file(f) == ICON_IMAGEFILE)
            ksnprintf(cmd, sizeof cmd, "viewer %s", path);
        else
            ksnprintf(cmd, sizeof cmd, "notepad %s", path);
        add_item(f->name, icon_for_file(f), cmd, path, false);
    }
    ui_invalidate_all();
}

void desktop_init(image_t *wallpaper)
{
    wall_src = wallpaper;
    desktop_set_background(0, bg_color);
    desktop_refresh_icons();
}

/* ---- painting ---------------------------------------------------------------------------------- */

static int wrap_label(const char *label, char lines[2][64], int max_w)
{
    if (text_width(&font_ui, label) <= max_w + 2) {
        strlcpy(lines[0], label, 64);
        return 1;
    }
    /* break at the last space that fits */
    int best = -1;
    for (int i = 0; label[i]; i++)
        if (label[i] == ' ' && text_width_n(&font_ui, label, i) <= max_w)
            best = i;
    if (best < 0) {
        int n = 0;
        while (label[n] && text_width_n(&font_ui, label, n + 1) <= max_w)
            n++;
        best = n;
        memcpy(lines[0], label, n);
        lines[0][n] = 0;
        strlcpy(lines[1], label + n, 64);
    } else {
        memcpy(lines[0], label, best);
        lines[0][best] = 0;
        strlcpy(lines[1], label + best + 1, 64);
    }
    if (text_width(&font_ui, lines[1]) > max_w) {
        int n = strlen(lines[1]);
        while (n > 0 && text_width_n(&font_ui, lines[1], n) + text_width(&font_ui, "...") > max_w)
            n--;
        strlcpy(lines[1] + n, "...", 64 - n);
    }
    return 2;
}

static void paint_item(gfx_t *g, int i, int x, int y, int opacity)
{
    ditem_t *it = &items[i];
    bool selected = i == sel && opacity == 255;
    int ix = x + (CELL_W - ICON_SZ) / 2, iy = y + 4;
    if (selected)
        gfx_round_rect(g, ix - 5, iy - 5, ICON_SZ + 10, ICON_SZ + 10, 7, RGBA(0, 0, 0, 64), CORNER_ALL);
    gfx_blit_alpha(g, ix, iy, icon_get(it->icon, ICON_SZ), opacity);

    char lines[2][64];
    int n = wrap_label(it->label, lines, CELL_W - 10);
    int ty = y + ICON_SZ + 10;
    for (int l = 0; l < n; l++) {
        int tw = text_width(&font_ui, lines[l]);
        int tx = x + (CELL_W - tw) / 2;
        if (selected) {
            gfx_round_rect(g, tx - 4, ty - 1, tw + 8, font_ui.height + 2, 4, COL_ACCENT, CORNER_ALL);
            gfx_text(g, &font_ui, tx, ty, lines[l], HEX(0xFFFFFF));
        } else {
            gfx_text(g, &font_ui, tx, ty + 1, lines[l], RGBA(0, 0, 0, opacity * 150 / 255));
            gfx_text(g, &font_ui, tx + 1, ty + 1, lines[l], RGBA(0, 0, 0, opacity * 60 / 255));
            gfx_text(g, &font_ui, tx, ty, lines[l], RGBA(255, 255, 255, opacity));
        }
        ty += font_ui.height + 1;
    }
}

void desktop_paint(gfx_t *g, rect_t dirty)
{
    gfx_blit(g, 0, 0, bg);
    for (int i = 0; i < n_items; i++)
        if (!rect_empty(rect_intersect(cell_rect(&items[i]), dirty)))
            paint_item(g, i, items[i].x, items[i].y, 255);
    if (drag.active)
        paint_item(g, drag.idx, drag.gx, drag.gy, 130);
}

/* ---- interaction ----------------------------------------------------------------------------------- */

static int item_at(int x, int y)
{
    for (int i = n_items - 1; i >= 0; i--) {
        ditem_t *it = &items[i];
        int ix = it->x + (CELL_W - ICON_SZ) / 2;
        if (x >= ix - 4 && x < ix + ICON_SZ + 4 && y >= it->y && y < it->y + ICON_SZ + 8)
            return i;
        char lines[2][64];
        int n = wrap_label(it->label, lines, CELL_W - 10);
        int w = 0;
        for (int l = 0; l < n; l++)
            w = MAX(w, text_width(&font_ui, lines[l]));
        int ty = it->y + ICON_SZ + 9;
        if (x >= it->x + (CELL_W - w) / 2 - 4 && x < it->x + (CELL_W + w) / 2 + 4 && y >= ty &&
            y < ty + n * (font_ui.height + 1) + 2)
            return i;
    }
    return -1;
}

static void select_item(int i)
{
    if (sel == i)
        return;
    if (sel >= 0)
        ui_invalidate(cell_rect(&items[sel]));
    sel = i;
    if (sel >= 0)
        ui_invalidate(cell_rect(&items[sel]));
}

static void open_item(int i)
{
    if (i < 0)
        return;
    char cmd[160];
    strlcpy(cmd, items[i].cmd, sizeof cmd);
    ui_launch(cmd);
}

enum {
    CM_ARRANGE_NAME = 1,
    CM_ARRANGE_TYPE,
    CM_REFRESH,
    CM_NEW_FOLDER,
    CM_NEW_TEXT,
    CM_PROPERTIES,
    CM_OPEN,
    CM_DELETE,
    CM_RENAME,
    CM_ITEM_PROPS,
    CM_EMPTY_BIN,
    CM_NEW_WINDOW,
    CM_GO_DOCUMENTS,
    CM_GO_DESKTOP,
    CM_GO_COMPUTER,
    CM_GO_DISK,
    CM_GO_APPS,
    CM_GO_FOLDER,
    CM_GO_TRASH,
    CM_MIN_ALL,
    CM_HELP,
    CM_TERMINAL,
};

static const menu_item_t sort_menu[] = {
    { "Name", CM_ARRANGE_NAME, 0, 0, NULL, 0, NULL },
    { "Kind", CM_ARRANGE_TYPE, 0, 0, NULL, 0, NULL },
};

static const menu_item_t desk_menu[] = {
    { "New Folder", CM_NEW_FOLDER, 0, 0, NULL, 0, NULL },
    { "New Text Document", CM_NEW_TEXT, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Get Info", CM_ITEM_PROPS, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Change Desktop Background...", CM_PROPERTIES, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Clean Up", CM_ARRANGE_NAME, 0, 0, NULL, 0, NULL },
    { "Sort By", 0, 0, 0, sort_menu, 2, NULL },
    { "Refresh", CM_REFRESH, 0, 0, NULL, 0, NULL },
};

static const menu_item_t system_item_menu[] = {
    { "Open", CM_OPEN, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Get Info", CM_ITEM_PROPS, 0, 0, NULL, 0, NULL },
};

static const menu_item_t file_item_menu[] = {
    { "Open", CM_OPEN, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Move to Trash", CM_DELETE, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Get Info", CM_ITEM_PROPS, 0, 0, NULL, 0, NULL },
    { "Rename", CM_RENAME, 0, 0, NULL, 0, NULL },
};

/* Finder's menus in the menu bar while the desktop is in front */
static const menu_item_t fm_file[] = {
    { "New Finder Window", CM_NEW_WINDOW, 0, 0, NULL, 0, "Ctrl+N" },
    { "New Folder", CM_NEW_FOLDER, 0, 0, NULL, 0, NULL },
    { "New Text Document", CM_NEW_TEXT, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Open", CM_OPEN, 0, 0, NULL, 0, "Ctrl+O" },
    { "Get Info", CM_ITEM_PROPS, 0, 0, NULL, 0, "Ctrl+I" },
    { "Rename", CM_RENAME, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Move to Trash", CM_DELETE, 0, 0, NULL, 0, NULL },
};
static const menu_item_t fm_edit[] = {
    { "Undo", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+Z" },
    { "Redo", 0, MI_DISABLED, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Cut", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+X" },
    { "Copy", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+C" },
    { "Paste", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+V" },
    { "Select All", 0, MI_DISABLED, 0, NULL, 0, "Ctrl+A" },
};
static const menu_item_t fm_view[] = {
    { "as Icons", 0, MI_CHECK, 0, NULL, 0, "Ctrl+1" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Clean Up", CM_ARRANGE_NAME, 0, 0, NULL, 0, NULL },
    { "Sort By", 0, 0, 0, sort_menu, 2, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Refresh", CM_REFRESH, 0, 0, NULL, 0, NULL },
};
static const menu_item_t fm_go[] = {
    { "Documents", CM_GO_DOCUMENTS, 0, 0, NULL, 0, NULL },
    { "Desktop", CM_GO_DESKTOP, 0, 0, NULL, 0, NULL },
    { "Computer", CM_GO_COMPUTER, 0, 0, NULL, 0, NULL },
    { "Kepler HD", CM_GO_DISK, 0, 0, NULL, 0, NULL },
    { "Applications", CM_GO_APPS, 0, 0, NULL, 0, NULL },
    { "Utilities", CM_TERMINAL, 0, 0, NULL, 0, NULL },
    { "Trash", CM_GO_TRASH, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Go to Folder...", CM_GO_FOLDER, 0, 0, NULL, 0, NULL },
};
static const menu_item_t fm_window[] = {
    { "Minimize All", CM_MIN_ALL, 0, 0, NULL, 0, "Ctrl+M" },
};
static const menu_item_t fm_help[] = {
    { "KeplerOS Help", CM_HELP, 0, 0, NULL, 0, NULL },
};
static const menubar_entry_t fm_entries[] = {
    { "File", fm_file, ARRAY_LEN(fm_file) },     { "Edit", fm_edit, ARRAY_LEN(fm_edit) },
    { "View", fm_view, ARRAY_LEN(fm_view) },     { "Go", fm_go, ARRAY_LEN(fm_go) },
    { "Window", fm_window, ARRAY_LEN(fm_window) }, { "Help", fm_help, ARRAY_LEN(fm_help) },
};
menubar_t desktop_menus = { fm_entries, ARRAY_LEN(fm_entries), -1, -1 };

static void arrange(bool by_type)
{
    /* the disk stays first, the rest re-flow */
    for (int i = 1; i < n_items; i++)
        for (int j = i + 1; j < n_items; j++) {
            bool swap;
            if (items[i].system != items[j].system)
                swap = !items[i].system;
            else if (by_type && items[i].icon != items[j].icon)
                swap = items[i].icon > items[j].icon;
            else
                swap = !items[i].system && strcasecmp(items[i].label, items[j].label) > 0;
            if (swap) {
                ditem_t t = items[i];
                items[i] = items[j];
                items[j] = t;
            }
        }
    n_placed = 0;
    for (int i = 0; i < n_items; i++)
        items[i].x = -1000;
    for (int i = 0; i < n_items; i++) {
        items[i].y = -1000;
        next_free_cell(&items[i].x, &items[i].y);
    }
    sel = -1;
    ui_invalidate_all();
}

static void rename_done(const char *value, void *ctx)
{
    fnode_t *f = ctx;
    if (value && value[0] && f) {
        if (!fs_rename(f, value))
            msgbox(NULL, "The name is already taken.", "Please choose a different name.", ICON_WARNING);
        desktop_refresh_icons();
    }
}

static void menu_action(int id, void *ctx)
{
    int idx = (int)(intptr_t)ctx;
    if (idx == -2)
        idx = sel; /* from the menu bar: whatever is selected */
    fnode_t *desk = fs_lookup("/Desktop");
    char name[64];
    switch (id) {
    case CM_ARRANGE_NAME: arrange(false); break;
    case CM_ARRANGE_TYPE: arrange(true); break;
    case CM_REFRESH: desktop_refresh_icons(); break;
    case CM_NEW_FOLDER:
        fs_unique_name(desk, "New Folder", "", name, sizeof name);
        fs_create_in(desk, name, true);
        desktop_refresh_icons();
        break;
    case CM_NEW_TEXT:
        fs_unique_name(desk, "New Text Document", "txt", name, sizeof name);
        fs_create_in(desk, name, false);
        desktop_refresh_icons();
        for (int i = 0; i < n_items; i++)
            if (!strcmp(items[i].label, name))
                select_item(i);
        break;
    case CM_PROPERTIES: ui_launch("display"); break;
    case CM_OPEN: open_item(idx); break;
    case CM_ITEM_PROPS:
        if (idx >= 0 && items[idx].system)
            ui_launch("sysprops");
        else
            ui_launch("about");
        break;
    case CM_NEW_WINDOW:
    case CM_GO_DOCUMENTS: ui_launch("finder"); break;
    case CM_GO_DESKTOP: ui_launch("explorer /Desktop"); break;
    case CM_GO_COMPUTER: ui_launch("mycomputer"); break;
    case CM_GO_DISK: ui_launch("explorer /"); break;
    case CM_GO_APPS: ui_launch("launchpad"); break;
    case CM_GO_TRASH: ui_launch("trash"); break;
    case CM_GO_FOLDER: ui_launch("spotlight"); break;
    case CM_TERMINAL: ui_launch("terminal"); break;
    case CM_MIN_ALL: wm_minimize_all(); break;
    case CM_HELP: ui_launch("about"); break;
    case CM_EMPTY_BIN: {
        fnode_t *bin = fs_lookup("/Recycled");
        while (bin && bin->child)
            fs_remove(bin->child);
        dock_changed();
        break;
    }
    case CM_DELETE:
        if (idx >= 0 && items[idx].path[0]) {
            fs_move(fs_lookup(items[idx].path), fs_lookup("/Recycled"));
            desktop_refresh_icons();
            dock_changed();
        }
        break;
    case CM_RENAME:
        if (idx >= 0 && items[idx].path[0])
            prompt_box(NULL, "Rename", "New name:", items[idx].label, rename_done, fs_lookup(items[idx].path));
        break;
    }
}

void desktop_menu_command(int id, void *ctx)
{
    (void)ctx;
    menu_action(id, (void *)-2);
}

bool desktop_mouse(int kind, int x, int y, int button)
{
    if (drag.idx >= 0 && (kind == MS_MOVE || kind == MS_UP)) {
        if (kind == MS_MOVE) {
            if (!drag.active && (ABS(x - drag.sx) > 4 || ABS(y - drag.sy) > 4))
                drag.active = true;
            if (drag.active) {
                ui_invalidate(rect_make(drag.gx, drag.gy, CELL_W, CELL_H));
                drag.gx = items[drag.idx].x + x - drag.sx;
                drag.gy = items[drag.idx].y + y - drag.sy;
                ui_invalidate(rect_make(drag.gx, drag.gy, CELL_W, CELL_H));
            }
            return true;
        }
        if (drag.active) {
            ui_invalidate(rect_make(drag.gx, drag.gy, CELL_W, CELL_H));
            int col = (col_x(0) - drag.gx + CELL_W / 2) / CELL_W;
            int row = (drag.gy - ORIGIN_Y + CELL_H / 2) / CELL_H;
            col = CLAMP(col, 0, (g_screen.w - 8) / CELL_W - 1);
            row = CLAMP(row, 0, rows() - 1);
            int nx = col_x(col), ny = ORIGIN_Y + row * CELL_H;
            ditem_t *it = &items[drag.idx];
            if (!cell_taken(nx, ny, drag.idx)) {
                ui_invalidate(cell_rect(it));
                it->x = nx;
                it->y = ny;
                ui_invalidate(cell_rect(it));
                int k = 0;
                while (k < n_placed && strcmp(placed[k].label, it->label))
                    k++;
                if (k < MAX_ITEMS) {
                    strlcpy(placed[k].label, it->label, sizeof placed[k].label);
                    placed[k].x = nx;
                    placed[k].y = ny;
                    if (k == n_placed)
                        n_placed++;
                }
            }
        }
        drag.idx = -1;
        drag.active = false;
        return true;
    }
    if (kind == MS_DOWN) {
        wm_deactivate();
        int i = item_at(x, y);
        select_item(i);
        if (button == MOUSE_LEFT && i >= 0) {
            drag.idx = i;
            drag.sx = x;
            drag.sy = y;
            drag.active = false;
            drag.gx = items[i].x;
            drag.gy = items[i].y;
        }
        if (button == MOUSE_RIGHT) {
            if (i < 0)
                menu_open(desk_menu, ARRAY_LEN(desk_menu), x, y, menu_action, (void *)-1);
            else if (items[i].path[0])
                menu_open(file_item_menu, ARRAY_LEN(file_item_menu), x, y, menu_action, (void *)(intptr_t)i);
            else
                menu_open(system_item_menu, ARRAY_LEN(system_item_menu), x, y, menu_action, (void *)(intptr_t)i);
        }
        return true;
    }
    if (kind == MS_DBLCLICK) {
        int i = item_at(x, y);
        drag.idx = -1;
        if (i >= 0)
            open_item(i);
        return true;
    }
    return true;
}

void desktop_key(const input_event_t *ev)
{
    if (!ev->pressed)
        return;
    switch (ev->key) {
    case KEY_ENTER:
        open_item(sel);
        break;
    case KEY_F5:
        desktop_refresh_icons();
        break;
    case KEY_DELETE:
        if (sel >= 0 && items[sel].path[0])
            menu_action(CM_DELETE, (void *)(intptr_t)sel);
        break;
    case KEY_UP:
    case KEY_DOWN:
    case KEY_LEFT:
    case KEY_RIGHT: {
        if (sel < 0) {
            select_item(0);
            break;
        }
        int dx = ev->key == KEY_LEFT ? -1 : ev->key == KEY_RIGHT ? 1 : 0;
        int dy = ev->key == KEY_UP ? -1 : ev->key == KEY_DOWN ? 1 : 0;
        int best = -1, best_d = 1 << 30;
        for (int i = 0; i < n_items; i++) {
            if (i == sel)
                continue;
            int ddx = items[i].x - items[sel].x, ddy = items[i].y - items[sel].y;
            if ((dx && ddx * dx <= 0) || (dy && ddy * dy <= 0))
                continue;
            int d = ABS(ddx) + ABS(ddy) * 2;
            if (dx)
                d = ABS(ddx) + ABS(ddy) * 3;
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        if (best >= 0)
            select_item(best);
        break;
    }
    }
}
