/* Minesweeper, the classic way. */

#include "apps.h"

#define CELL 16
#define MAX_W 30
#define MAX_H 16
#define BORDER 12
#define PANEL_H 37

enum { FACE_SMILE, FACE_OH, FACE_DEAD, FACE_COOL };

typedef struct {
    int w, h, mines;
    uint8_t mine[MAX_H][MAX_W];
    uint8_t open[MAX_H][MAX_W];
    uint8_t flag[MAX_H][MAX_W];
    bool started, over, won;
    int boom_x, boom_y;
    uint32_t start_ms;
    int seconds;
    int press_x, press_y; /* cell held down, -1 if none */
    bool face_down;
    menubar_t mb;
} mines_t;

/* ---- geometry ------------------------------------------------------------------------------------------ */

static int grid_x(void) { return BORDER; }
static int grid_y(void) { return MENUBAR_H + BORDER + PANEL_H + BORDER - 2; }
static int client_w(mines_t *m) { return m->w * CELL + 2 * BORDER + 6; }
static int client_h(mines_t *m) { return grid_y() + m->h * CELL + BORDER + 3; }
static rect_t face_rect(mines_t *m) { return rect_make(client_w(m) / 2 - 13, MENUBAR_H + BORDER + 5, 26, 26); }

/* ---- game logic ----------------------------------------------------------------------------------------- */

static int count(mines_t *m, int x, int y)
{
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < m->w && ny < m->h && m->mine[ny][nx])
                n++;
        }
    return n;
}

static void reset(mines_t *m, int w, int h, int mines)
{
    m->w = w;
    m->h = h;
    m->mines = mines;
    memset(m->mine, 0, sizeof m->mine);
    memset(m->open, 0, sizeof m->open);
    memset(m->flag, 0, sizeof m->flag);
    m->started = m->over = m->won = false;
    m->seconds = 0;
    m->press_x = m->press_y = -1;
}

static void place(mines_t *m, int safe_x, int safe_y)
{
    int placed = 0;
    while (placed < m->mines) {
        int x = krand() % m->w, y = krand() % m->h;
        if (m->mine[y][x] || (ABS(x - safe_x) <= 1 && ABS(y - safe_y) <= 1))
            continue;
        m->mine[y][x] = 1;
        placed++;
    }
}

static void reveal(mines_t *m, int x, int y)
{
    /* iterative flood over empty cells */
    static int16_t stack[MAX_W * MAX_H * 2];
    int n = 0;
    stack[n++] = x;
    stack[n++] = y;
    while (n) {
        int cy = stack[--n], cx = stack[--n];
        if (m->open[cy][cx] || m->flag[cy][cx])
            continue;
        m->open[cy][cx] = 1;
        if (count(m, cx, cy))
            continue;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = cx + dx, ny = cy + dy;
                if (nx >= 0 && ny >= 0 && nx < m->w && ny < m->h && !m->open[ny][nx] && n < (int)ARRAY_LEN(stack) - 2) {
                    stack[n++] = nx;
                    stack[n++] = ny;
                }
            }
    }
}

static void check_win(mines_t *m)
{
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (!m->mine[y][x] && !m->open[y][x])
                return;
    m->over = m->won = true;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (m->mine[y][x])
                m->flag[y][x] = 1;
    ui_beep(1320, 120);
}

static void click_cell(mines_t *m, int x, int y)
{
    if (m->over || m->flag[y][x] || m->open[y][x])
        return;
    if (!m->started) {
        place(m, x, y);
        m->started = true;
        m->start_ms = timer_ms();
        m->seconds = 1;
    }
    if (m->mine[y][x]) {
        m->over = true;
        m->boom_x = x;
        m->boom_y = y;
        m->open[y][x] = 1;
        ui_beep(220, 250);
        return;
    }
    reveal(m, x, y);
    check_win(m);
}

static void chord(mines_t *m, int x, int y)
{
    if (!m->open[y][x] || m->over)
        return;
    int flags = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < m->w && ny < m->h && m->flag[ny][nx])
                flags++;
        }
    if (flags != count(m, x, y))
        return;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < m->w && ny < m->h)
                click_cell(m, nx, ny);
        }
}

static int flags_left(mines_t *m)
{
    int n = m->mines;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            n -= m->flag[y][x];
    return n;
}

/* ---- drawing -------------------------------------------------------------------------------------------- */

static void bevel(gfx_t *g, int x, int y, int w, int h, int t, bool raised)
{
    uint32_t lt = raised ? HEX(0xFFFFFF) : HEX(0x808080), dk = raised ? HEX(0x808080) : HEX(0xFFFFFF);
    for (int i = 0; i < t; i++) {
        gfx_hline(g, x + i, y + i, w - 2 * i - 1, lt);
        gfx_vline(g, x + i, y + i, h - 2 * i - 1, lt);
        gfx_hline(g, x + i + 1, y + h - 1 - i, w - 2 * i - 1, dk);
        gfx_vline(g, x + w - 1 - i, y + i + 1, h - 2 * i - 1, dk);
    }
}

/* seven-segment digit, 13x23 */
static void lcd_digit(gfx_t *g, int x, int y, int d)
{
    static const uint8_t seg[11] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40 };
    uint8_t s = d < 0 ? seg[10] : seg[d % 10];
    for (int i = 0; i < 7; i++) {
        uint32_t c = (s >> i) & 1 ? HEX(0xFF0000) : HEX(0x5A0000);
        switch (i) {
        case 0: for (int k = 0; k < 2; k++) gfx_hline(g, x + 2 + k, y + 1 + k, 9 - 2 * k, c); break;
        case 1: for (int k = 0; k < 2; k++) gfx_vline(g, x + 11 - k, y + 2 + k, 9 - 2 * k, c); break;
        case 2: for (int k = 0; k < 2; k++) gfx_vline(g, x + 11 - k, y + 12 + k, 9 - 2 * k, c); break;
        case 3: for (int k = 0; k < 2; k++) gfx_hline(g, x + 2 + k, y + 21 - k, 9 - 2 * k, c); break;
        case 4: for (int k = 0; k < 2; k++) gfx_vline(g, x + 1 + k, y + 12 + k, 9 - 2 * k, c); break;
        case 5: for (int k = 0; k < 2; k++) gfx_vline(g, x + 1 + k, y + 2 + k, 9 - 2 * k, c); break;
        case 6: gfx_hline(g, x + 3, y + 10, 7, c); gfx_hline(g, x + 2, y + 11, 9, c); gfx_hline(g, x + 3, y + 12, 7, c); break;
        }
    }
}

static void lcd(gfx_t *g, int x, int y, int v)
{
    gfx_fill(g, x, y, 41, 25, HEX(0x000000));
    bevel(g, x - 1, y - 1, 43, 27, 1, false);
    v = CLAMP(v, -99, 999);
    if (v < 0) {
        lcd_digit(g, x + 1, y + 1, -1);
        lcd_digit(g, x + 14, y + 1, (-v) / 10);
        lcd_digit(g, x + 27, y + 1, (-v) % 10);
    } else {
        lcd_digit(g, x + 1, y + 1, v / 100);
        lcd_digit(g, x + 14, y + 1, (v / 10) % 10);
        lcd_digit(g, x + 27, y + 1, v % 10);
    }
}

static void draw_face(gfx_t *g, rect_t r, int face, bool down)
{
    gfx_fill(g, r.x, r.y, r.w, r.h, HEX(0xC0C0C0));
    gfx_rect(g, r.x - 1, r.y - 1, r.w + 2, r.h + 2, HEX(0x808080));
    bevel(g, r.x, r.y, r.w, r.h, 2, !down);
    int cx = r.x + 13 + (down ? 1 : 0), cy = r.y + 13 + (down ? 1 : 0);
    gfx_circle(g, cx, cy, 9, HEX(0x000000));
    gfx_circle(g, cx, cy, 8, HEX(0xFFFF00));
    uint32_t k = HEX(0x000000);
    switch (face) {
    case FACE_DEAD:
        for (int i = 0; i < 3; i++) {
            gfx_pixel(g, cx - 5 + i, cy - 4 + i, k);
            gfx_pixel(g, cx - 3 - i, cy - 4 + i, k);
            gfx_pixel(g, cx + 2 + i, cy - 4 + i, k);
            gfx_pixel(g, cx + 4 - i, cy - 4 + i, k);
        }
        gfx_hline(g, cx - 3, cy + 4, 6, k);
        gfx_pixel(g, cx - 4, cy + 5, k);
        gfx_pixel(g, cx + 3, cy + 5, k);
        break;
    case FACE_COOL:
        gfx_fill(g, cx - 7, cy - 3, 14, 1, k);
        gfx_fill(g, cx - 6, cy - 3, 5, 3, k);
        gfx_fill(g, cx + 1, cy - 3, 5, 3, k);
        gfx_hline(g, cx - 3, cy + 4, 6, k);
        gfx_pixel(g, cx - 4, cy + 3, k);
        gfx_pixel(g, cx + 3, cy + 3, k);
        break;
    case FACE_OH:
        gfx_fill(g, cx - 4, cy - 3, 2, 2, k);
        gfx_fill(g, cx + 2, cy - 3, 2, 2, k);
        gfx_circle(g, cx, cy + 4, 2, k);
        break;
    default:
        gfx_fill(g, cx - 4, cy - 3, 2, 2, k);
        gfx_fill(g, cx + 2, cy - 3, 2, 2, k);
        gfx_hline(g, cx - 3, cy + 4, 6, k);
        gfx_pixel(g, cx - 4, cy + 3, k);
        gfx_pixel(g, cx + 3, cy + 3, k);
        break;
    }
}

static void draw_mine(gfx_t *g, int x, int y)
{
    int cx = x + 8, cy = y + 8;
    gfx_hline(g, cx - 6, cy, 13, HEX(0x000000));
    gfx_vline(g, cx, cy - 6, 13, HEX(0x000000));
    gfx_pixel(g, cx - 4, cy - 4, HEX(0x000000));
    gfx_pixel(g, cx + 4, cy - 4, HEX(0x000000));
    gfx_pixel(g, cx - 4, cy + 4, HEX(0x000000));
    gfx_pixel(g, cx + 4, cy + 4, HEX(0x000000));
    gfx_circle(g, cx, cy, 5, HEX(0x000000));
    gfx_fill(g, cx - 2, cy - 2, 2, 2, HEX(0xFFFFFF));
}

static void draw_flag(gfx_t *g, int x, int y)
{
    for (int j = 0; j < 5; j++)
        gfx_hline(g, x + 9 - (j < 3 ? j : 4 - j) * 2, y + 3 + j, 1 + (j < 3 ? j : 4 - j) * 2, HEX(0xFF0000));
    gfx_vline(g, x + 9, y + 3, 7, HEX(0xFF0000));
    gfx_vline(g, x + 9, y + 8, 4, HEX(0x000000));
    gfx_hline(g, x + 7, y + 11, 4, HEX(0x000000));
    gfx_hline(g, x + 5, y + 12, 8, HEX(0x000000));
}

static const uint32_t num_colors[9] = { 0, 0x0000FF, 0x008000, 0xFF0000, 0x000080, 0x800000, 0x008080, 0x000000, 0x808080 };

static void mines_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    mines_t *m = w->data;
    menubar_paint(&m->mb, g, cw);
    int top = MENUBAR_H;
    gfx_fill(g, 0, top, cw, ch - top, HEX(0xC0C0C0));
    bevel(g, 0, top, cw, ch - top, 3, true);

    /* panel */
    int py = top + BORDER - 3;
    bevel(g, BORDER - 3, py, cw - 2 * (BORDER - 3), PANEL_H, 2, false);
    lcd(g, BORDER + 4, py + 6, flags_left(m));
    lcd(g, cw - BORDER - 4 - 41, py + 6, m->seconds);
    int face = m->won ? FACE_COOL : m->over ? FACE_DEAD : m->press_x >= 0 ? FACE_OH : FACE_SMILE;
    draw_face(g, face_rect(m), face, m->face_down);

    /* grid */
    int gx = grid_x(), gy = grid_y();
    bevel(g, gx - 3, gy - 3, m->w * CELL + 6, m->h * CELL + 6, 3, false);
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            int sx = gx + x * CELL, sy = gy + y * CELL;
            bool pressed = m->press_x == x && m->press_y == y && !m->open[y][x] && !m->flag[y][x];
            if (m->open[y][x] || pressed) {
                bool boom = m->over && !m->won && x == m->boom_x && y == m->boom_y;
                gfx_fill(g, sx, sy, CELL, CELL, boom ? HEX(0xFF0000) : HEX(0xC0C0C0));
                gfx_hline(g, sx, sy, CELL, HEX(0x808080));
                gfx_vline(g, sx, sy, CELL, HEX(0x808080));
                if (pressed)
                    continue;
                if (m->mine[y][x]) {
                    draw_mine(g, sx, sy);
                } else {
                    int n = count(m, x, y);
                    if (n) {
                        char s[2] = { (char)('0' + n), 0 };
                        gfx_text_centered(g, &font_ui_bold, sx + 1, sy + 2, CELL, s, HEX(num_colors[n]));
                    }
                }
            } else {
                gfx_fill(g, sx, sy, CELL, CELL, HEX(0xC0C0C0));
                bevel(g, sx, sy, CELL, CELL, 2, true);
                if (m->over && !m->won && m->mine[y][x] && !m->flag[y][x]) {
                    gfx_fill(g, sx, sy, CELL, CELL, HEX(0xC0C0C0));
                    gfx_hline(g, sx, sy, CELL, HEX(0x808080));
                    gfx_vline(g, sx, sy, CELL, HEX(0x808080));
                    draw_mine(g, sx, sy);
                } else if (m->flag[y][x]) {
                    draw_flag(g, sx, sy);
                    if (m->over && !m->won && !m->mine[y][x]) {
                        gfx_line(g, sx + 2, sy + 2, sx + 13, sy + 13, HEX(0xFF0000));
                        gfx_line(g, sx + 13, sy + 2, sx + 2, sy + 13, HEX(0xFF0000));
                    }
                }
            }
        }
}

/* ---- menus & input ---------------------------------------------------------------------------------------- */

enum { MM_NEW = 1, MM_BEGINNER, MM_INTER, MM_EXPERT, MM_EXIT, MM_ABOUT };

static menu_item_t game_menu[] = {
    { "New", MM_NEW, 0, 0, NULL, 0, "F2" },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Beginner", MM_BEGINNER, MI_CHECK, 0, NULL, 0, NULL },
    { "Intermediate", MM_INTER, 0, 0, NULL, 0, NULL },
    { "Expert", MM_EXPERT, 0, 0, NULL, 0, NULL },
    { NULL, 0, MI_SEP, 0, NULL, 0, NULL },
    { "Exit", MM_EXIT, 0, 0, NULL, 0, NULL },
};
static const menu_item_t help_menu[] = {
    { "About Minesweeper...", MM_ABOUT, 0, 0, NULL, 0, NULL },
};
static const menubar_entry_t bar[] = {
    { "Game", game_menu, ARRAY_LEN(game_menu) },
    { "Help", help_menu, ARRAY_LEN(help_menu) },
};

static void resize_window(window_t *w)
{
    mines_t *m = w->data;
    wm_set_client_size(w, client_w(m), client_h(m));
}

static void command(int id, void *ctx)
{
    window_t *w = ctx;
    if (!wm_alive(w))
        return;
    mines_t *m = w->data;
    switch (id) {
    case MM_NEW: reset(m, m->w, m->h, m->mines); break;
    case MM_BEGINNER: reset(m, 9, 9, 10); break;
    case MM_INTER: reset(m, 16, 16, 40); break;
    case MM_EXPERT: reset(m, 30, 16, 99); break;
    case MM_EXIT: wm_close(w); return;
    case MM_ABOUT: msgbox(w, "About Minesweeper", "Minesweeper for KeplerOS\nClear the field without hitting a mine.", ICON_MINES); return;
    }
    if (id >= MM_BEGINNER && id <= MM_EXPERT) {
        for (int i = 2; i <= 4; i++)
            game_menu[i].flags = game_menu[i].id == id ? MI_CHECK : 0;
        resize_window(w);
    }
    wm_invalidate_client(w);
}

static bool cell_at(mines_t *m, int x, int y, int *cx, int *cy)
{
    int gx = x - grid_x(), gy = y - grid_y();
    if (gx < 0 || gy < 0 || gx >= m->w * CELL || gy >= m->h * CELL)
        return false;
    *cx = gx / CELL;
    *cy = gy / CELL;
    return true;
}

static void mines_mouse(window_t *w, int kind, int x, int y, int button)
{
    mines_t *m = w->data;
    if (menubar_mouse(&m->mb, w, kind, x, y, command, w))
        return;
    int cx, cy;
    bool in_cell = cell_at(m, x, y, &cx, &cy);
    rect_t fr = face_rect(m);
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        if (rect_contains(fr, x, y)) {
            m->face_down = true;
        } else if (in_cell && !m->over) {
            m->press_x = cx;
            m->press_y = cy;
        }
        wm_invalidate_client(w);
    } else if (kind == MS_MOVE && (m->press_x >= 0 || m->face_down)) {
        if (m->press_x >= 0) {
            m->press_x = in_cell ? cx : -2;
            m->press_y = in_cell ? cy : -2;
        }
        wm_invalidate_client(w);
    } else if (kind == MS_UP && button == MOUSE_LEFT) {
        if (m->face_down && rect_contains(fr, x, y))
            reset(m, m->w, m->h, m->mines);
        else if (m->press_x != -1 && in_cell)
            click_cell(m, cx, cy);
        m->face_down = false;
        m->press_x = m->press_y = -1;
        wm_invalidate_client(w);
    } else if (kind == MS_DOWN && button == MOUSE_RIGHT && in_cell && !m->over && !m->open[cy][cx]) {
        m->flag[cy][cx] ^= 1;
        wm_invalidate_client(w);
    } else if (kind == MS_DOWN && button == MOUSE_MIDDLE && in_cell) {
        chord(m, cx, cy);
        wm_invalidate_client(w);
    }
}

static void mines_key(window_t *w, const input_event_t *ev)
{
    if (ev->pressed && ev->key == KEY_F2)
        command(MM_NEW, w);
}

static void mines_tick(window_t *w)
{
    mines_t *m = w->data;
    if (m->started && !m->over) {
        int s = 1 + (timer_ms() - m->start_ms) / 1000;
        if (s != m->seconds) {
            m->seconds = MIN(s, 999);
            wm_invalidate_client_rect(w, client_w(m) - 80, MENUBAR_H, 80, 50);
        }
    }
}

static void mines_close(window_t *w) { kfree(w->data); }

static const window_ops_t mines_ops = { mines_paint, mines_key, mines_mouse, mines_tick, mines_close, NULL };

window_t *mines_open(void)
{
    mines_t *m = kzalloc(sizeof *m);
    reset(m, 9, 9, 10);
    m->mb.entries = bar;
    m->mb.n = ARRAY_LEN(bar);
    m->mb.open = m->mb.hover = -1;
    window_t *w = wm_create("Minesweeper", ICON_MINES, client_w(m), client_h(m), &mines_ops, m, WF_NO_MAX);
    wm_set_app(w, "Minesweeper", &m->mb, command, w);
    w->bg = HEX(0xC0C0C0);
    w->min_w = 100;
    wm_set_tick(w, 250);
    return w;
}
