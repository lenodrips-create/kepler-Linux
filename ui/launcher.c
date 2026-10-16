/* The app list, Launchpad (every app on a frosted full screen) and Spotlight
 * (type to find an app, a file, or run a command). */

#include "ramfs.h"
#include "ui.h"

const app_info_t g_apps[] = {
    { "Finder", ICON_FINDER, "finder", true },
    { "Launchpad", ICON_ALLPROGS, "launchpad", true },
    { "Kepler", ICON_BROWSER, "kepler", true },
    { "Terminal", ICON_CMD, "terminal", true },
    { "BradyBuddy", ICON_BRADY, "brady", true },
    { "Notepad", ICON_NOTEPAD, "notepad", true },
    { "Calculator", ICON_CALC, "calc", true },
    { "Paint", ICON_PAINT, "paint", false },
    { "Preview", ICON_PICTURES, "preview", false },
    { "Minesweeper", ICON_MINES, "mines", false },
    { "Activity Monitor", ICON_TASKMGR, "taskmgr", true },
    { "System Preferences", ICON_DISPLAY, "display", true },
    { "System Information", ICON_SYSINFO, "sysprops", false },
};
const int g_app_count = ARRAY_LEN(g_apps);

const app_info_t *app_find(const char *name)
{
    for (int i = 0; i < g_app_count; i++)
        if (!strcmp(g_apps[i].name, name))
            return &g_apps[i];
    return NULL;
}

/* case-insensitive substring test; returns the match position or -1 */
static int find_ci(const char *hay, const char *needle)
{
    int n = strlen(needle);
    if (!n)
        return 0;
    for (int i = 0; hay[i]; i++) {
        int k = 0;
        while (k < n && hay[i + k] && ktolower(hay[i + k]) == ktolower(needle[k]))
            k++;
        if (k == n)
            return i;
    }
    return -1;
}

static void open_path(const char *path)
{
    fnode_t *f = fs_lookup(path);
    char cmd[200];
    const char *x = f ? fs_ext(f->name) : "";
    if (f && f->dir)
        ksnprintf(cmd, sizeof cmd, "explorer %s", path);
    else if (f && (!strcasecmp(x, "jpg") || !strcasecmp(x, "jpeg")))
        ksnprintf(cmd, sizeof cmd, "viewer %s", path);
    else
        ksnprintf(cmd, sizeof cmd, "notepad %s", path);
    ui_launch(cmd);
}

/* ---- Launchpad -------------------------------------------------------------------------------- */

#define LP_ICON 80
#define LP_CELL_W 150
#define LP_CELL_H 136
#define LP_COLS 7

static bool lp_open;
static textfield_t lp_search;
static int lp_pressed = -1;
static const app_info_t *lp_list[16];
static int lp_n;

bool launchpad_is_open(void) { return lp_open; }

static void lp_filter(void)
{
    lp_n = 0;
    for (int i = 0; i < g_app_count; i++)
        if (strcmp(g_apps[i].cmd, "launchpad") && find_ci(g_apps[i].name, lp_search.text) >= 0)
            lp_list[lp_n++] = &g_apps[i];
}

void launchpad_toggle(void)
{
    if (lp_open) {
        launchpad_close();
        return;
    }
    menu_close();
    spotlight_close();
    lp_open = true;
    textfield_set(&lp_search, "");
    lp_filter();
    lp_pressed = -1;
    ui_invalidate_all();
}

void launchpad_close(void)
{
    if (!lp_open)
        return;
    lp_open = false;
    ui_invalidate_all();
}

static rect_t lp_cell(int i)
{
    int cols = MIN(LP_COLS, MAX(1, lp_n));
    int x0 = (g_screen.w - cols * LP_CELL_W) / 2;
    return rect_make(x0 + (i % LP_COLS) * LP_CELL_W, 120 + (i / LP_COLS) * LP_CELL_H, LP_CELL_W, LP_CELL_H);
}

static rect_t lp_search_rect(void) { return rect_make((g_screen.w - 240) / 2, 46, 240, 28); }

void launchpad_paint(gfx_t *g, rect_t dirty)
{
    (void)dirty;
    if (!lp_open)
        return;
    int W = g_screen.w, H = g_screen.h;
    gfx_backdrop(g, 0, 0, W, H, 0, ui_vibrancy(), RGBA(10, 14, 20, 90), 0);

    rect_t s = lp_search_rect();
    gfx_round_rect(g, s.x, s.y, s.w, s.h, 8, RGBA(255, 255, 255, 46), CORNER_ALL);
    gfx_round_frame(g, s.x, s.y, s.w, s.h, 8, RGBA(255, 255, 255, 70), CORNER_ALL);
    if (lp_search.text[0]) {
        gfx_blit_tint(g, s.x + 10, s.y + 7, icon_get(GLYPH_SEARCH, 14), RGBA(255, 255, 255, 200));
        gfx_text_clipped(g, &font_menu, s.x + 32, s.y + (s.h - font_menu.height) / 2, s.w - 40, lp_search.text, HEX(0xFFFFFF));
    } else {
        int tw = text_width(&font_menu, "Search") + 20;
        int x = s.x + (s.w - tw) / 2;
        gfx_blit_tint(g, x, s.y + 7, icon_get(GLYPH_SEARCH, 14), RGBA(255, 255, 255, 170));
        gfx_text(g, &font_menu, x + 20, s.y + (s.h - font_menu.height) / 2, "Search", RGBA(255, 255, 255, 170));
    }

    for (int i = 0; i < lp_n; i++) {
        rect_t c = lp_cell(i);
        int ix = c.x + (c.w - LP_ICON) / 2, iy = c.y;
        const image_t *img = icon_get(lp_list[i]->icon, LP_ICON);
        gfx_blit_alpha(g, ix, iy, img, 255);
        if (lp_pressed == i)
            gfx_blit_tint(g, ix, iy, img, RGBA(0, 0, 0, 90));
        const char *name = lp_list[i]->name;
        int tw = text_width(&font_menu, name);
        int tx = c.x + (c.w - tw) / 2, ty = iy + LP_ICON + 10;
        gfx_text(g, &font_menu, tx, ty + 1, name, RGBA(0, 0, 0, 140));
        gfx_text(g, &font_menu, tx, ty, name, HEX(0xFFFFFF));
    }
    if (!lp_n)
        gfx_text_centered(g, &font_heading, 0, 200, W, "No Results", RGBA(255, 255, 255, 180));
    /* one page: one dot */
    gfx_circle(g, W / 2, H - DOCK_RESERVE - 26, 4, RGBA(255, 255, 255, 220));
}

static int lp_hit(int x, int y)
{
    for (int i = 0; i < lp_n; i++) {
        rect_t c = lp_cell(i);
        int ix = c.x + (c.w - LP_ICON) / 2;
        if (x >= ix - 10 && x < ix + LP_ICON + 10 && y >= c.y && y < c.y + LP_ICON + 30)
            return i;
    }
    return -1;
}

bool launchpad_mouse(int kind, int x, int y, int button)
{
    if (!lp_open)
        return false;
    int h = lp_hit(x, y);
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        lp_pressed = h;
        ui_invalidate_all();
    } else if (kind == MS_UP && button == MOUSE_LEFT) {
        int p = lp_pressed;
        lp_pressed = -1;
        if (p >= 0 && p == h) {
            const char *cmd = lp_list[p]->cmd;
            launchpad_close();
            ui_launch(cmd);
        } else if (h < 0 && !rect_contains(lp_search_rect(), x, y)) {
            launchpad_close();
        } else {
            ui_invalidate_all();
        }
    }
    return true;
}

void launchpad_key(const input_event_t *ev)
{
    if (!ev->pressed)
        return;
    if (ev->key == KEY_ESC) {
        if (lp_search.text[0]) {
            textfield_set(&lp_search, "");
            lp_filter();
            ui_invalidate_all();
        } else {
            launchpad_close();
        }
        return;
    }
    if (ev->key == KEY_ENTER) {
        if (lp_n) {
            const char *cmd = lp_list[0]->cmd;
            launchpad_close();
            ui_launch(cmd);
        }
        return;
    }
    if (textfield_key(&lp_search, ev)) {
        lp_filter();
        ui_invalidate_all();
    }
}

/* ---- Spotlight ------------------------------------------------------------------------------- */

#define SP_W     680
#define SP_ROW_H 48
#define SP_ITEM  34
#define SP_MAX   8

enum { R_APP, R_FILE, R_RUN };

typedef struct {
    int kind;
    int icon;
    char name[64];
    char detail[40];
    char target[160];
} result_t;

static bool sp_open;
static textfield_t sp_text;
static result_t results[SP_MAX];
static int n_results, sp_sel;
static uint32_t sp_blink;

bool spotlight_is_open(void) { return sp_open; }

rect_t spotlight_rect(void)
{
    int h = SP_ROW_H + (n_results ? 1 + n_results * SP_ITEM + 10 : 0);
    return rect_make((g_screen.w - SP_W) / 2, 150, SP_W, h);
}

static rect_t sp_extent(void)
{
    rect_t r = spotlight_rect();
    return rect_make(r.x - 30, r.y - 20, r.w + 60, r.h + 60);
}

static void add_result(int kind, int icon, const char *name, const char *detail, const char *target)
{
    if (n_results >= SP_MAX)
        return;
    result_t *r = &results[n_results++];
    r->kind = kind;
    r->icon = icon;
    strlcpy(r->name, name, sizeof r->name);
    strlcpy(r->detail, detail, sizeof r->detail);
    strlcpy(r->target, target, sizeof r->target);
}

static void search_files(fnode_t *dir, int *budget)
{
    for (fnode_t *f = dir ? dir->child : NULL; f && *budget > 0; f = f->next) {
        if (!strcasecmp(f->name, "Recycled") && dir == fs_root())
            continue;
        if (find_ci(f->name, sp_text.text) >= 0) {
            char path[160];
            fs_path(f, path, sizeof path);
            const char *x = fs_ext(f->name);
            bool img = !strcasecmp(x, "jpg") || !strcasecmp(x, "jpeg");
            add_result(R_FILE, f->dir ? ICON_FOLDER : img ? ICON_IMAGEFILE : ICON_TEXTFILE, f->name,
                       f->dir ? "Folder" : img ? "Image" : "Document", path);
            (*budget)--;
        }
        if (f->dir)
            search_files(f, budget);
    }
}

static void sp_search(void)
{
    n_results = 0;
    sp_sel = 0;
    const char *q = sp_text.text;
    if (!q[0])
        return;
    /* apps whose name starts with the query first, then the other matches */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < g_app_count; i++) {
            int at = find_ci(g_apps[i].name, q);
            if (at < 0 || (pass == 0) != (at == 0))
                continue;
            add_result(R_APP, g_apps[i].icon, g_apps[i].name, "Application", g_apps[i].cmd);
        }
    int budget = 4;
    search_files(fs_root(), &budget);
    char line[80], target[160];
    ksnprintf(line, sizeof line, "Search the Web for \"%s\"", q);
    ksnprintf(target, sizeof target, "kepler ?%s", q);
    add_result(R_RUN, ICON_BROWSER, line, "Kepler", target);
    ksnprintf(line, sizeof line, "Run \"%s\"", q);
    add_result(R_RUN, ICON_CMD, line, "Command", q);
}

void spotlight_open(void)
{
    menu_close();
    launchpad_close();
    if (sp_open)
        return;
    sp_open = true;
    textfield_set(&sp_text, "");
    n_results = 0;
    sp_blink = timer_ms();
    ui_invalidate(sp_extent());
}

void spotlight_close(void)
{
    if (!sp_open)
        return;
    ui_invalidate(sp_extent());
    sp_open = false;
}

static void sp_activate(int i)
{
    if (i < 0 || i >= n_results)
        return;
    result_t r = results[i];
    spotlight_close();
    if (r.kind == R_APP)
        ui_launch(r.target);
    else if (r.kind == R_FILE)
        open_path(r.target);
    else if (r.target[0] == '/')
        open_path(r.target);
    else
        ui_launch(r.target);
}

void spotlight_paint(gfx_t *g)
{
    if (!sp_open)
        return;
    rect_t r = spotlight_rect();
    gfx_shadow(g, r.x, r.y, r.w, r.h, 12, 24, 10, 70);
    gfx_blur_behind(g, r.x, r.y, r.w, r.h, 12, 14, RGBA(244, 244, 246, 218), CORNER_ALL);
    gfx_round_frame(g, r.x, r.y, r.w, r.h, 12, RGBA(0, 0, 0, 50), CORNER_ALL);

    gfx_blit_tint(g, r.x + 16, r.y + (SP_ROW_H - 22) / 2, icon_get(GLYPH_SEARCH, 20), HEX(0x6E6E73));
    int tx = r.x + 50, ty = r.y + (SP_ROW_H - font_big.height) / 2;
    if (sp_text.text[0])
        gfx_text_clipped(g, &font_big, tx, ty, r.w - 70, sp_text.text, COL_TEXT);
    else
        gfx_text(g, &font_big, tx, ty, "Spotlight Search", HEX(0xA0A0A5));
    if ((timer_ms() - sp_blink) / 530 % 2 == 0) {
        int cx = tx + text_width_n(&font_big, sp_text.text, sp_text.cursor);
        gfx_fill(g, cx, ty + 2, 2, font_big.height - 4, COL_ACCENT);
    }
    if (!n_results)
        return;
    int y = r.y + SP_ROW_H;
    gfx_hline(g, r.x, y, r.w, RGBA(0, 0, 0, 30));
    y += 6;
    for (int i = 0; i < n_results; i++) {
        result_t *it = &results[i];
        bool sel = i == sp_sel;
        if (sel)
            gfx_round_rect(g, r.x + 8, y, r.w - 16, SP_ITEM, 6, COL_MENU_HI, CORNER_ALL);
        gfx_blit_alpha(g, r.x + 18, y + (SP_ITEM - 24) / 2, icon_get(it->icon, 24), 255);
        int ly = y + (SP_ITEM - font_menu.height) / 2;
        gfx_text_clipped(g, &font_menu, r.x + 52, ly, r.w - 200, it->name, sel ? HEX(0xFFFFFF) : COL_TEXT);
        gfx_text(g, &font_menu, r.x + r.w - 20 - text_width(&font_menu, it->detail), ly, it->detail,
                 sel ? RGBA(255, 255, 255, 210) : HEX(0x8A8A8E));
        y += SP_ITEM;
    }
}

static int sp_hit(int x, int y)
{
    rect_t r = spotlight_rect();
    int top = r.y + SP_ROW_H + 6;
    if (x < r.x || x >= r.x + r.w || y < top)
        return -1;
    int i = (y - top) / SP_ITEM;
    return i < n_results ? i : -1;
}

bool spotlight_mouse(int kind, int x, int y, int button)
{
    if (!sp_open)
        return false;
    bool inside = rect_contains(spotlight_rect(), x, y);
    int h = sp_hit(x, y);
    if (kind == MS_MOVE && h >= 0 && h != sp_sel) {
        sp_sel = h;
        ui_invalidate(spotlight_rect());
    }
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        if (!inside)
            spotlight_close();
        else if (h >= 0)
            sp_activate(h);
        return true;
    }
    if (inside)
        ui_set_cursor(CURSOR_ARROW);
    return inside || kind == MS_DOWN || kind == MS_UP;
}

void spotlight_key(const input_event_t *ev)
{
    if (!ev->pressed)
        return;
    switch (ev->key) {
    case KEY_ESC: spotlight_close(); return;
    case KEY_ENTER: sp_activate(sp_sel); return;
    case KEY_DOWN:
    case KEY_UP:
        if (n_results) {
            sp_sel = (sp_sel + (ev->key == KEY_DOWN ? 1 : n_results - 1)) % n_results;
            ui_invalidate(spotlight_rect());
        }
        return;
    }
    rect_t before = sp_extent();
    if (textfield_key(&sp_text, ev)) {
        sp_search();
        sp_blink = timer_ms();
        ui_invalidate(before);
        ui_invalidate(sp_extent());
    }
}

void spotlight_tick(void)
{
    static int phase = -1;
    if (!sp_open)
        return;
    int p = (timer_ms() - sp_blink) / 530 % 2;
    if (p != phase) {
        phase = p;
        ui_invalidate(spotlight_rect());
    }
}
