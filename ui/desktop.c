/* The desktop: wallpaper, icons, selection, drag to rearrange, context menus. */

#include "apps.h"
#include "ramfs.h"
#include "ui.h"

#define CELL_W 86
#define CELL_H 78
#define ORIGIN_X 4
#define ORIGIN_Y 6
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

static int work_h(void) { return g_screen.h - TASKBAR_H; }
static int rows(void) { return (work_h() - ORIGIN_Y) / CELL_H; }

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
            int cx = ORIGIN_X + c * CELL_W, cy = ORIGIN_Y + r * CELL_H;
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
        return ICON_PICTURES;
    return ICON_TEXTFILE;
}

void desktop_refresh_icons(void)
{
    n_items = 0;
    sel = -1;
    /* the Recycle Bin sits in the bottom-right corner, as in the original */
    ditem_t *rb = &items[n_items++];
    memset(rb, 0, sizeof *rb);
    strlcpy(rb->label, "Recycle Bin", sizeof rb->label);
    rb->icon = ICON_RECYCLE;
    strlcpy(rb->cmd, "recycle", sizeof rb->cmd);
    rb->system = true;
    rb->x = g_screen.w - CELL_W - 10;
    rb->y = ORIGIN_Y + (rows() - 1) * CELL_H;

    add_item("My Documents", ICON_DOCUMENTS, "mydocs", NULL, true);
    add_item("My Computer", ICON_COMPUTER, "mycomputer", NULL, true);
    add_item("Command Prompt", ICON_CMD, "cmd", NULL, true);
    add_item("Notepad", ICON_NOTEPAD, "notepad", NULL, true);

    fnode_t *d = fs_lookup("/Desktop");
    for (fnode_t *f = d ? d->child : NULL; f; f = f->next) {
        char path[128], cmd[160];
        fs_path(f, path, sizeof path);
        if (f->dir)
            ksnprintf(cmd, sizeof cmd, "explorer %s", path);
        else if (icon_for_file(f) == ICON_PICTURES)
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
    const image_t *icon = icon_get(it->icon, 32);
    int ix = x + (CELL_W - 32) / 2, iy = y + 4;
    if (selected) {
        /* tint the icon with the selection colour */
        image_t tmp = { 0 };
        uint32_t px[32 * 32];
        tmp.px = px;
        tmp.w = tmp.h = 32;
        for (int k = 0; k < 32 * 32; k++)
            px[k] = (color_lerp(icon->px[k], COL_SELECT, 110) & 0xFFFFFF) | (icon->px[k] & 0xFF000000u);
        gfx_blit_alpha(g, ix, iy, &tmp, 255);
    } else {
        gfx_blit_alpha(g, ix, iy, icon, opacity);
    }

    char lines[2][64];
    int n = wrap_label(it->label, lines, CELL_W - 4);
    int ty = y + 41;
    for (int l = 0; l < n; l++) {
        int tw = text_width(&font_ui, lines[l]);
        int tx = x + (CELL_W - tw) / 2;
        if (selected) {
            gfx_fill(g, tx - 2, ty - 1, tw + 4, font_ui.height + 1, COL_SELECT);
            gfx_text(g, &font_ui, tx, ty, lines[l], HEX(0xFFFFFF));
        } else {
            uint32_t sh = RGBA(0, 0, 0, opacity * 200 / 255);
            gfx_text(g, &font_ui, tx + 1, ty + 1, lines[l], sh);
            gfx_text(g, &font_ui, tx + 1, ty, lines[l], RGBA(0, 0, 0, opacity * 70 / 255));
            gfx_text(g, &font_ui, tx, ty, lines[l], RGBA(255, 255, 255, opacity));
        }
        ty += font_ui.height;
    }
}

void desktop_paint(gfx_t *g, rect_t dirty)
{
    gfx_blit(g, 0, 0, bg);
    for (int i = 0; i < n_items; i++)
        if (!rect_empty(rect_intersect(cell_rect(&items[i]), dirty)))
            paint_item(g, i, items[i].x, items[i].y, 255);
    if (drag.active)
        paint_item(g, drag.idx, drag.gx, drag.gy, 120);
}

/* ---- interaction ----------------------------------------------------------------------------------- */

static int item_at(int x, int y)
{
    for (int i = n_items - 1; i >= 0; i--) {
        ditem_t *it = &items[i];
        int ix = it->x + (CELL_W - 32) / 2;
        if (x >= ix - 4 && x < ix + 36 && y >= it->y + 2 && y < it->y + 40)
            return i;
        char lines[2][64];
        int n = wrap_label(it->label, lines, CELL_W - 4);
        int w = 0;
        for (int l = 0; l < n; l++)
            w = MAX(w, text_width(&font_ui, lines[l]));
        if (x >= it->x + (CELL_W - w) / 2 - 2 && x < it->x + (CELL_W + w) / 2 + 2 && y >= it->y + 40 &&
            y < it->y + 42 + n * font_ui.height)
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
};

static const menu_item_t arrange_menu[] = {
    { "Name", CM_ARRANGE_NAME, 0, 0, NULL, 0, NULL },
    { "Type", CM_ARRANGE_TYPE, 0, 0, NULL, 0, NULL },
};

static const menu_item_t new_menu[] = {
    { "Folder", CM_NEW_FOLDER, 0, ICON_FOLDER, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Text Document", CM_NEW_TEXT, 0, ICON_TEXTFILE, NULL, 0, NULL },
};

static const menu_item_t desk_menu[] = {
    { "Arrange Icons By", 0, 0, 0, arrange_menu, 2, NULL },
    { "Refresh", CM_REFRESH, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Paste", 0, MI_DISABLED, 0, NULL, 0, NULL },
    { "Paste Shortcut", 0, MI_DISABLED, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "New", 0, 0, 0, new_menu, 3, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Properties", CM_PROPERTIES, 0, 0, NULL, 0, NULL },
};

static const menu_item_t system_item_menu[] = {
    { "Open", CM_OPEN, MI_BOLD, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Properties", CM_ITEM_PROPS, 0, 0, NULL, 0, NULL },
};

static const menu_item_t bin_menu[] = {
    { "Open", CM_OPEN, MI_BOLD, 0, NULL, 0, NULL },
    { "Empty Recycle Bin", CM_EMPTY_BIN, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Properties", CM_ITEM_PROPS, 0, 0, NULL, 0, NULL },
};

static const menu_item_t file_item_menu[] = {
    { "Open", CM_OPEN, MI_BOLD, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Delete", CM_DELETE, 0, 0, NULL, 0, NULL },
    { "Rename", CM_RENAME, 0, 0, NULL, 0, NULL },
};

static void arrange(bool by_type)
{
    /* keep the Recycle Bin where it is, re-flow the rest */
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
    for (int i = 1; i < n_items; i++)
        items[i].x = -1000;
    for (int i = 1; i < n_items; i++) {
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
            msgbox(NULL, "Rename", "A file with that name already exists.", ICON_WARNING);
        desktop_refresh_icons();
    }
}

static void menu_action(int id, void *ctx)
{
    int idx = (int)(intptr_t)ctx;
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
        if (idx >= 0 && !strcmp(items[idx].cmd, "mycomputer"))
            ui_launch("sysprops");
        else
            ui_launch("about");
        break;
    case CM_EMPTY_BIN: {
        fnode_t *bin = fs_lookup("/Recycled");
        while (bin && bin->child)
            fs_remove(bin->child);
        break;
    }
    case CM_DELETE:
        if (idx >= 0 && items[idx].path[0]) {
            fs_move(fs_lookup(items[idx].path), fs_lookup("/Recycled"));
            desktop_refresh_icons();
        }
        break;
    case CM_RENAME:
        if (idx >= 0 && items[idx].path[0])
            prompt_box(NULL, "Rename", "New name:", items[idx].label, rename_done, fs_lookup(items[idx].path));
        break;
    }
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
            int col = (drag.gx - ORIGIN_X + CELL_W / 2) / CELL_W;
            int row = (drag.gy - ORIGIN_Y + CELL_H / 2) / CELL_H;
            col = CLAMP(col, 0, (g_screen.w - ORIGIN_X) / CELL_W - 1);
            row = CLAMP(row, 0, rows() - 1);
            int nx = ORIGIN_X + col * CELL_W, ny = ORIGIN_Y + row * CELL_H;
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
            else if (!strcmp(items[i].cmd, "recycle"))
                menu_open(bin_menu, ARRAY_LEN(bin_menu), x, y, menu_action, (void *)(intptr_t)i);
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
            select_item(n_items > 1 ? 1 : 0);
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
