/* Full-screen moments: boot splash, welcome screen, Turn Off dialog and goodbye. */

#include "ui.h"

#define BAND_H 84

static const image_t *big_logo(void)
{
    static const image_t *logo;
    if (!logo)
        logo = icon_get(ICON_KEPLER, 96);
    return logo;
}

/* "Kepler" + orange "OS", returns the width */
static int wordmark(gfx_t *g, const font_t *f, int x, int y, bool draw)
{
    int w1 = text_width(f, "Kepler"), w2 = text_width(f, "OS");
    if (draw) {
        gfx_text(g, f, x, y, "Kepler", HEX(0xFFFFFF));
        gfx_text(g, f, x + w1 + 2, y, "OS", HEX(0xF59A1B));
    }
    return w1 + 2 + w2;
}

/* ---- boot splash -------------------------------------------------------------------------------------- */

static uint32_t boot_start;
static int boot_pct;

static void paint_splash(void)
{
    gfx_t g = video_gfx();
    int W = g_screen.w, H = g_screen.h;
    gfx_fill(&g, 0, 0, W, H, HEX(0x000000));

    const image_t *logo = big_logo();
    int mw = wordmark(&g, &font_logo, 0, 0, false);
    int total = logo->w + 18 + mw;
    int x = (W - total) / 2, y = H / 2 - 110;
    gfx_blit_alpha(&g, x, y, logo, 255);
    int tx = x + logo->w + 18, ty = y + (logo->h - font_logo.height) / 2 - 8;
    gfx_text(&g, &font_ui, tx + 4, ty - 6, "The", HEX(0xB8B8B8));
    wordmark(&g, &font_logo, tx, ty, true);
    gfx_text(&g, &font_heading, tx + mw - text_width(&font_heading, "Professional") - 4, ty + font_logo.height - 6, "Professional",
             HEX(0xF59A1B));

    /* the marching progress blocks */
    int bw = 170, bh = 20, bx = (W - bw) / 2, by = H / 2 + 60;
    gfx_round_rect(&g, bx, by, bw, bh, 5, HEX(0xB0B0B0), CORNER_ALL);
    gfx_round_rect(&g, bx + 2, by + 2, bw - 4, bh - 4, 4, HEX(0x000000), CORNER_ALL);
    gfx_t inner = gfx_sub(&g, bx + 4, by + 4, bw - 8, bh - 8);
    int span = bw + 30;
    int pos = (int)((timer_ms() - boot_start) / 9 % span) - 30;
    for (int i = 0; i < 3; i++)
        gfx_round_rect_vgrad(&inner, pos + i * 10, 0, 8, bh - 8, 2, HEX(0x7FB0FF), HEX(0x1F55D6), CORNER_ALL);

    gfx_text(&g, &font_ui, 26, H - 34, "Copyright (C) 2026 The KeplerOS Project", HEX(0x9A9A9A));
    char status[48];
    ksnprintf(status, sizeof status, "Starting KeplerOS... %d%%", boot_pct);
    gfx_text_centered(&g, &font_ui, 0, by + 34, W, status, HEX(0x6F6F6F));
    int ww = wordmark(&g, &font_logo_small, 0, 0, false);
    wordmark(&g, &font_logo_small, W - ww - 26, H - 50, true);
    video_flush(rect_make(0, 0, W, H));
}

static void boot_progress(int pct)
{
    boot_pct = pct;
    paint_splash();
}

void screens_boot(void (*work)(void (*progress)(int pct)))
{
    boot_start = timer_ms();
    paint_splash();
    kprintf("splash: shown\n");
    work(boot_progress);
    while (timer_ms() - boot_start < 2600) {
        paint_splash();
        sleep_ms(20);
    }
}

/* ---- welcome screen ----------------------------------------------------------------------------------- */

typedef struct {
    const char *name;
    const char *subtitle;
} account_t;

static const account_t accounts[] = {
    { "Kepler", "Computer administrator" },
    { "Guest", "Guest account" },
};

static int wl_hover = -1, wl_sel = -1;
static bool welcoming;
static uint32_t welcome_until;
static int off_hover;
static image_t *guest_pic;

static const image_t *account_picture(int i)
{
    if (i == 0)
        return icon_user_picture(48);
    if (!guest_pic) {
        guest_pic = image_new(48, 48);
        gfx_t g = gfx_for_image(guest_pic);
        gfx_round_rect_vgrad(&g, 0, 0, 48, 48, 6, HEX(0xF6C343), HEX(0xE07B12), CORNER_ALL);
        gfx_circle(&g, 24, 17, 8, HEX(0xFFFFFF));
        gfx_round_rect(&g, 10, 28, 28, 22, 11, HEX(0xFFFFFF), CORNER_TOP);
    }
    return guest_pic;
}

static rect_t tile_rect(int i)
{
    int W = g_screen.w, H = g_screen.h;
    int n = ARRAY_LEN(accounts);
    int top = H / 2 - (n * 76) / 2;
    return rect_make(W / 2 + 34, top + i * 76, 330, 66);
}

static rect_t off_rect(void) { return rect_make(28, g_screen.h - BAND_H + 24, 210, 36); }

void screens_welcome_reset(void)
{
    wl_hover = wl_sel = -1;
    welcoming = false;
    off_hover = 0;
}

static void paint_bands(gfx_t *g, int W, int H)
{
    gfx_fill(g, 0, 0, W, BAND_H, HEX(0x00309C));
    /* the middle with a soft glow near the top left */
    gfx_vgrad(g, 0, BAND_H, W, H - 2 * BAND_H, HEX(0x6C8EE3), HEX(0x5070D6));
    /* smooth radial light, falling off with the squared distance */
    const int gx = 120, gy = BAND_H + 40, R = 420;
    rect_t glow = rect_intersect(g->clip, rect_make(0, BAND_H, gx + R, gy + R - BAND_H));
    for (int y = glow.y; y < glow.y + glow.h; y++)
        for (int x = glow.x; x < glow.x + glow.w; x++) {
            int dx = x - gx, dy = y - gy;
            int d2 = dx * dx + dy * dy;
            if (d2 >= R * R)
                continue;
            int a = 70 - d2 * 70 / (R * R);
            gfx_pixel(g, x, y, RGBA(255, 255, 255, a * a / 70));
        }
    gfx_fill(g, 0, H - BAND_H, W, BAND_H, HEX(0x00309C));
    for (int x = 0; x < W; x++) {
        int d = ABS(x - W / 2) * 256 / (W / 2);
        gfx_pixel(g, x, BAND_H, color_lerp(HEX(0xFFFFFF), HEX(0x2E58C8), d));
        gfx_pixel(g, x, BAND_H + 1, color_lerp(HEX(0xB9CCF5), HEX(0x4A6ED2), d));
        gfx_pixel(g, x, H - BAND_H - 1, color_lerp(HEX(0xFFB64D), HEX(0x2E58C8), d));
        gfx_pixel(g, x, H - BAND_H - 2, color_lerp(HEX(0xF59A1B), HEX(0x4A6ED2), d));
    }
}

void screens_welcome_paint(gfx_t *g, rect_t dirty)
{
    int W = g_screen.w, H = g_screen.h;
    paint_bands(g, W, H);
    const image_t *logo = icon_get(ICON_KEPLER, 64);

    if (welcoming) {
        int ww = text_width(&font_logo, "welcome");
        gfx_text(g, &font_logo, (W - ww) / 2 + 2, H / 2 - 40 + 2, "welcome", RGBA(0, 0, 0, 60));
        gfx_text(g, &font_logo, (W - ww) / 2, H / 2 - 40, "welcome", HEX(0xFFFFFF));
        gfx_blit_alpha(g, (W - ww) / 2 - 80, H / 2 - 40, logo, 255);
        return;
    }

    /* divider */
    for (int y = BAND_H + 50; y < H - BAND_H - 50; y++) {
        int mid = (H) / 2;
        int d = ABS(y - mid) * 256 / ((H - 2 * BAND_H - 100) / 2);
        gfx_pixel(g, W / 2, y, color_lerp(HEX(0xE8EEFC), HEX(0x5F80DD), d));
    }

    /* left: logo and prompt */
    int right = W / 2 - 40;
    int mw = wordmark(g, &font_logo_small, 0, 0, false);
    int lx = right - mw;
    int ly = H / 2 - 74;
    gfx_blit_alpha(g, lx - 76, ly - 14, logo, 255);
    wordmark(g, &font_logo_small, lx, ly, true);
    const char *prompt = "To begin, click your user name";
    gfx_text(g, &font_big, right - text_width(&font_big, prompt), H / 2 + 10, prompt, HEX(0xFFFFFF));

    /* right: accounts */
    for (int i = 0; i < ARRAY_LEN(accounts); i++) {
        rect_t r = tile_rect(i);
        bool hot = wl_hover == i || wl_sel == i;
        if (hot)
            gfx_round_rect_vgrad(g, r.x, r.y, r.w, r.h, 8, HEX(0x1F4AB8), HEX(0x2E5BC8), CORNER_ALL);
        const image_t *pic = account_picture(i);
        int px = r.x + 9, py = r.y + 9;
        gfx_round_rect(g, px - 3, py - 3, 54, 54, 6, hot ? HEX(0xF59A1B) : HEX(0xFFFFFF), CORNER_ALL);
        gfx_blit_alpha(g, px, py, pic, 255);
        gfx_text(g, &font_big_bold, r.x + 76, r.y + 10, accounts[i].name, HEX(0xFFFFFF));
        if (hot)
            gfx_text(g, &font_ui, r.x + 77, r.y + 40, accounts[i].subtitle, HEX(0xD6E2FF));
    }

    /* bottom band */
    rect_t o = off_rect();
    if (off_hover)
        gfx_round_rect(g, o.x - 4, o.y - 2, o.w, o.h + 4, 6, RGBA(255, 255, 255, 30), CORNER_ALL);
    gfx_blit_alpha(g, o.x, o.y + 2, icon_get(ICON_POWER, 32), 255);
    gfx_text(g, &font_big, o.x + 42, o.y + 7, "Turn off KeplerOS", HEX(0xFFFFFF));
    const char *hint1 = "After you log on, you can add or change accounts.";
    const char *hint2 = "Just go to Control Panel and click User Accounts.";
    gfx_text(g, &font_ui, W - 30 - text_width(&font_ui, hint1), H - BAND_H + 26, hint1, HEX(0xFFFFFF));
    gfx_text(g, &font_ui, W - 30 - text_width(&font_ui, hint2), H - BAND_H + 42, hint2, HEX(0xFFFFFF));
}

static void login(int i)
{
    strlcpy(g_user_name, accounts[i].name, sizeof g_user_name);
    wl_sel = i;
    welcoming = true;
    welcome_until = timer_ms() + 900;
    ui_invalidate_all();
}

bool screens_welcome_mouse(int kind, int x, int y, int button)
{
    if (welcoming)
        return true;
    int h = -1;
    for (int i = 0; i < ARRAY_LEN(accounts); i++)
        if (rect_contains(tile_rect(i), x, y))
            h = i;
    int oh = rect_contains(off_rect(), x, y);
    if (h != wl_hover || oh != off_hover) {
        wl_hover = h;
        off_hover = oh;
        ui_invalidate_all();
    }
    ui_set_cursor(h >= 0 || oh ? CURSOR_HAND : CURSOR_ARROW);
    if (kind == MS_UP && button == MOUSE_LEFT) {
        if (h >= 0)
            login(h);
        else if (oh)
            overlay_open(0);
    }
    return true;
}

void screens_welcome_key(const input_event_t *ev)
{
    if (!ev->pressed || welcoming)
        return;
    int n = ARRAY_LEN(accounts);
    if (ev->key == KEY_DOWN || ev->key == KEY_UP) {
        wl_hover = ((wl_hover < 0 ? 0 : wl_hover) + (ev->key == KEY_DOWN ? 1 : n - 1)) % n;
        ui_invalidate_all();
    } else if (ev->key == KEY_ENTER) {
        login(wl_hover >= 0 ? wl_hover : 0);
    }
}

void screens_welcome_tick(void)
{
    if (welcoming && (int32_t)(timer_ms() - welcome_until) >= 0) {
        welcoming = false;
        ui_login();
    }
}

/* ---- Turn Off / Log Off overlay ------------------------------------------------------------------------- */

#define DLG_W 380
#define DLG_H 236

static int ov_kind = -1; /* -1 closed, 0 turn off, 1 log off */
static uint32_t ov_start;
static int ov_hover = -1, ov_pressed = -1;
static int ov_fade;

bool overlay_is_open(void) { return ov_kind >= 0; }

static rect_t dlg_rect(void) { return rect_make((g_screen.w - DLG_W) / 2, (g_screen.h - DLG_H) / 2 - 20, DLG_W, DLG_H); }

static int ov_buttons(void) { return ov_kind == 0 ? 3 : 2; }

static rect_t ov_button(int i)
{
    rect_t d = dlg_rect();
    int n = ov_buttons();
    int gap = n == 3 ? 110 : 140;
    int x0 = d.x + (DLG_W - (n - 1) * gap) / 2 - 16;
    return rect_make(x0 + i * gap - 14, d.y + 84, 60, 70);
}

static rect_t cancel_rect(void)
{
    rect_t d = dlg_rect();
    return rect_make(d.x + DLG_W - 92, d.y + DLG_H - 37, 75, 23);
}

void overlay_open(int kind)
{
    ov_kind = kind;
    ov_start = timer_ms();
    ov_hover = ov_pressed = -1;
    ov_fade = 0;
    startmenu_close();
    menu_close();
    ui_invalidate_all();
}

static void overlay_close(void)
{
    ov_kind = -1;
    ui_invalidate_all();
}

void overlay_tick(void)
{
    if (ov_kind < 0)
        return;
    int target = MIN(256, (int)(timer_ms() - ov_start) * 256 / 900);
    if (target - ov_fade >= 32 || (target == 256 && ov_fade != 256)) {
        ov_fade = target;
        ui_invalidate_all();
    }
}

void overlay_paint(gfx_t *g, rect_t dirty)
{
    if (ov_kind < 0)
        return;
    /* fade what is underneath to grey */
    rect_t d = dlg_rect();
    gfx_desaturate(g, dirty.x, dirty.y, dirty.w, dirty.h, ov_fade, ov_fade / 5);

    gfx_t dg = gfx_sub(g, d.x, d.y, d.w, d.h);
    gfx_fill(&dg, 3, 3, DLG_W, DLG_H, RGBA(0, 0, 0, 60));
    /* top band */
    gfx_fill(&dg, 0, 0, DLG_W, 52, HEX(0x00309C));
    gfx_text(&dg, &font_big, 18, 14, ov_kind == 0 ? "Turn off computer" : "Log Off KeplerOS", HEX(0xFFFFFF));
    gfx_blit_alpha(&dg, DLG_W - 46, 10, icon_get(ICON_KEPLER, 32), 255);
    /* middle */
    gfx_vgrad(&dg, 0, 52, DLG_W, DLG_H - 52 - 50, HEX(0x7C9BEA), HEX(0x5070D6));
    for (int x = 0; x < DLG_W; x++) {
        int dd = ABS(x - DLG_W / 2) * 256 / (DLG_W / 2);
        gfx_pixel(&dg, x, 52, color_lerp(HEX(0xFFFFFF), HEX(0x3C64CC), dd));
        gfx_pixel(&dg, x, DLG_H - 51, color_lerp(HEX(0xFFB64D), HEX(0x3C64CC), dd));
    }
    /* bottom band */
    gfx_fill(&dg, 0, DLG_H - 50, DLG_W, 50, HEX(0x00309C));

    static const int off_icons[3] = { ICON_STANDBY, ICON_POWER, ICON_RESTART };
    static const char *off_labels[3] = { "Stand By", "Turn Off", "Restart" };
    static const int lo_icons[2] = { ICON_SWITCHUSER, ICON_LOGOFF };
    static const char *lo_labels[2] = { "Switch User", "Log Off" };
    for (int i = 0; i < ov_buttons(); i++) {
        rect_t r = ov_button(i);
        int icon = ov_kind == 0 ? off_icons[i] : lo_icons[i];
        const char *label = ov_kind == 0 ? off_labels[i] : lo_labels[i];
        int lx = r.x - d.x, ly = r.y - d.y;
        bool hot = ov_hover == i;
        if (hot)
            gfx_circle(&dg, lx + r.w / 2, ly + 22, 26, RGBA(255, 255, 255, 50));
        gfx_blit_alpha(&dg, lx + (r.w - 32) / 2, ly + 6 + (ov_pressed == i && hot ? 1 : 0), icon_get(icon, 32), hot ? 255 : 235);
        gfx_text_centered(&dg, &font_ui, lx - 20, ly + 48, r.w + 40, label, HEX(0xFFFFFF));
    }
    rect_t c = cancel_rect();
    draw_button(&dg, c.x - d.x, c.y - d.y, c.w, c.h, "Cancel", ov_hover == 9 ? BTN_HOVER : 0);
}

static void activate(int i)
{
    int kind = ov_kind;
    if (kind == 0) {
        overlay_close();
        ui_power(i);
    } else {
        overlay_close();
        ui_logoff();
    }
}

bool overlay_mouse(int kind, int x, int y, int button)
{
    int h = -1;
    for (int i = 0; i < ov_buttons(); i++)
        if (rect_contains(ov_button(i), x, y))
            h = i;
    if (rect_contains(cancel_rect(), x, y))
        h = 9;
    if (h != ov_hover) {
        ov_hover = h;
        ui_invalidate(dlg_rect());
    }
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        ov_pressed = h;
        ui_invalidate(dlg_rect());
    } else if (kind == MS_UP && button == MOUSE_LEFT) {
        int p = ov_pressed;
        ov_pressed = -1;
        if (p >= 0 && p == h) {
            if (h == 9)
                overlay_close();
            else
                activate(h);
        }
        if (ov_kind >= 0)
            ui_invalidate(dlg_rect());
    }
    return true;
}

void overlay_key(const input_event_t *ev)
{
    if (!ev->pressed)
        return;
    if (ev->key == KEY_ESC)
        overlay_close();
    else if (ev->key == KEY_ENTER)
        activate(ov_kind == 0 ? 1 : 1);
    else if (ev->key == KEY_LEFT || ev->key == KEY_RIGHT) {
        int n = ov_buttons();
        ov_hover = ((ov_hover < 0 || ov_hover >= n ? 0 : ov_hover) + (ev->key == KEY_RIGHT ? 1 : n - 1)) % n;
        ui_invalidate(dlg_rect());
    }
}

/* ---- goodbye ------------------------------------------------------------------------------------------- */

void screens_goodbye(int action)
{
    gfx_t g = video_gfx();
    int W = g_screen.w, H = g_screen.h;
    paint_bands(&g, W, H);
    const image_t *logo = icon_get(ICON_KEPLER, 64);
    int mw = wordmark(&g, &font_logo_small, 0, 0, false);
    int x = (W - mw - 80) / 2;
    gfx_blit_alpha(&g, x, H / 2 - 70, logo, 255);
    wordmark(&g, &font_logo_small, x + 80, H / 2 - 56, true);
    const char *msg = action == 2 ? "KeplerOS is restarting..." : "KeplerOS is shutting down...";
    gfx_text_centered(&g, &font_big, 0, H / 2 + 20, W, msg, HEX(0xFFFFFF));
    video_flush(rect_make(0, 0, W, H));
    kprintf("goodbye: %s\n", action == 2 ? "restart" : "power off");
    sleep_ms(2200);
    if (action == 2)
        power_reboot();
    power_off();
}
