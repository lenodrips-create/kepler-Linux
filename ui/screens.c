/* Full-screen moments: the boot screen, the sign-in / lock screen, the shut
 * down / restart / log out alert and the goodbye. */

#include "ui.h"

static void text_shadow_centered(gfx_t *g, const font_t *f, int x, int y, int w, const char *s, uint32_t c)
{
    gfx_text_centered(g, f, x, y + 1, w, s, RGBA(0, 0, 0, 90));
    gfx_text_centered(g, f, x, y, w, s, c);
}

/* word-wrapped and centred; with g NULL it only measures. Returns the height used. */
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

/* ---- boot ------------------------------------------------------------------------------------------ */

static int boot_pct;

static void paint_splash(void)
{
    gfx_t g = video_gfx();
    int W = g_screen.w, H = g_screen.h;
    gfx_fill(&g, 0, 0, W, H, HEX(0x000000));
    gfx_blit_tint(&g, (W - 96) / 2, H / 2 - 110, icon_get(GLYPH_KEPLER, 96), HEX(0xFFFFFF));
    int bw = 210, bh = 5, bx = (W - bw) / 2, by = H / 2 + 40;
    gfx_round_rect(&g, bx, by, bw, bh, 2, HEX(0x3A3A3C), CORNER_ALL);
    int fill = bw * CLAMP(boot_pct, 0, 100) / 100;
    if (fill > 4)
        gfx_round_rect(&g, bx, by, fill, bh, 2, HEX(0xFFFFFF), CORNER_ALL);
    video_flush(rect_make(0, 0, W, H));
}

static void boot_progress(int pct)
{
    boot_pct = pct;
    paint_splash();
}

void screens_boot(void (*work)(void (*progress)(int pct)))
{
    uint32_t start = timer_ms();
    boot_pct = 0;
    paint_splash();
    kprintf("splash: shown\n");
    work(boot_progress);
    /* let the bar finish gracefully */
    while (timer_ms() - start < 1800)
        sleep_ms(20);
}

/* ---- sign in ---------------------------------------------------------------------------------------- */

typedef struct {
    const char *name;
    const char *password; /* NULL: no password */
    const char *hint;
} account_t;

static const account_t accounts[] = {
    { "Kepler", "kepler", "Password hint: kepler" },
    { "Guest", NULL, NULL },
};
#define N_ACCOUNTS ((int)ARRAY_LEN(accounts))

#define LIST_AV  80
#define SEL_AV   96
#define FIELD_W  210
#define FIELD_H  30

static int sel = -1;   /* the account being signed in to; -1 shows the list */
static int hover = -1; /* list item, or 10 + n for the bottom buttons, 20 cancel, 21 arrow */
static bool locked;
static char pw[64];
static int pw_len;
static int failures;
static uint32_t shake_start;
static bool shaking;
static uint32_t blink;
static int last_minute = -1;

static const image_t *avatar(int i, int size)
{
    if (i == 0)
        return icon_user_picture(size);
    return icon_get(ICON_GUEST, size);
}

static int users_y(void) { return g_screen.h - 340; }

static rect_t list_rect(int i)
{
    int gap = 150;
    int x0 = g_screen.w / 2 - (N_ACCOUNTS - 1) * gap / 2;
    return rect_make(x0 + i * gap - 60, users_y(), 120, LIST_AV + 34);
}

static int shake_dx(void)
{
    if (!shaking)
        return 0;
    int t = (int)(timer_ms() - shake_start);
    if (t > 480)
        return 0;
    int amp = 14 * (480 - t) / 480;
    int ph = (t / 30) % 4;
    return ph == 0 ? amp : ph == 2 ? -amp : 0;
}

static rect_t field_rect(void)
{
    return rect_make((g_screen.w - FIELD_W) / 2 + shake_dx(), users_y() + SEL_AV + 40, FIELD_W, FIELD_H);
}

static rect_t cancel_rect(void)
{
    rect_t f = field_rect();
    return rect_make(g_screen.w / 2 - 40, f.y + f.h + 34, 80, 22);
}

static rect_t power_rect(int i)
{
    int gap = 104, x0 = g_screen.w / 2 - gap;
    return rect_make(x0 + i * gap - 40, g_screen.h - 104, 80, 70);
}

void screens_login_reset(bool lock)
{
    locked = lock;
    sel = -1;
    if (lock)
        for (int i = 0; i < N_ACCOUNTS; i++)
            if (!strcmp(accounts[i].name, g_user_name))
                sel = i;
    hover = -1;
    pw_len = 0;
    pw[0] = 0;
    failures = 0;
    shaking = false;
    blink = timer_ms();
}

static void paint_avatar(gfx_t *g, int i, int cx, int y, int size, bool ring)
{
    if (ring)
        gfx_circle(g, cx, y + size / 2, size / 2 + 4, RGBA(255, 255, 255, 90));
    gfx_circle(g, cx, y + size / 2 + 2, size / 2, RGBA(0, 0, 0, 40));
    gfx_blit_alpha(g, cx - size / 2, y, avatar(i, size), 255);
}

void screens_login_paint(gfx_t *g, rect_t dirty)
{
    int W = g_screen.w;
    gfx_blit(g, 0, 0, ui_vibrancy());
    gfx_fill(g, 0, 0, W, g_screen.h, RGBA(0, 0, 0, 46));

    /* date and time */
    static const char *days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    static const char *months[] = { "January", "February", "March", "April", "May", "June", "July",
                                     "August", "September", "October", "November", "December" };
    datetime_t dt;
    rtc_read(&dt);
    char line[48];
    ksnprintf(line, sizeof line, "%s, %s %d", days[dt.weekday % 7], months[(dt.month + 11) % 12], dt.day);
    text_shadow_centered(g, &font_date, 0, 70, W, line, RGBA(255, 255, 255, 235));
    ksnprintf(line, sizeof line, "%d:%02d", dt.hour % 12 ? dt.hour % 12 : 12, dt.minute);
    gfx_text_centered(g, &font_clock, 0, 94 + 2, W, line, RGBA(0, 0, 0, 40));
    gfx_text_centered(g, &font_clock, 0, 94, W, line, RGBA(255, 255, 255, 225));

    if (sel < 0) {
        for (int i = 0; i < N_ACCOUNTS; i++) {
            rect_t r = list_rect(i);
            paint_avatar(g, i, r.x + r.w / 2, r.y, LIST_AV, hover == i);
            text_shadow_centered(g, &font_heading, r.x - 20, r.y + LIST_AV + 12, r.w + 40, accounts[i].name, HEX(0xFFFFFF));
        }
    } else {
        int y = users_y();
        paint_avatar(g, sel, W / 2, y, SEL_AV, false);
        text_shadow_centered(g, &font_heading, 0, y + SEL_AV + 12, W, accounts[sel].name, HEX(0xFFFFFF));
        rect_t f = field_rect();
        gfx_round_rect(g, f.x, f.y, f.w, f.h, f.h / 2, RGBA(255, 255, 255, 60), CORNER_ALL);
        gfx_round_frame(g, f.x, f.y, f.w, f.h, f.h / 2, RGBA(255, 255, 255, 80), CORNER_ALL);
        int ty = f.y + (f.h - font_ui.height) / 2;
        int cx = f.x + 14;
        if (!pw_len) {
            gfx_text(g, &font_ui, f.x + 14, ty, "Enter Password", RGBA(255, 255, 255, 170));
        } else {
            for (int i = 0; i < pw_len && cx < f.x + f.w - 40; i++, cx += 10)
                gfx_circle(g, cx + 3, f.y + f.h / 2, 3, HEX(0xFFFFFF));
            gfx_blit_tint(g, f.x + f.w - 26, f.y + (f.h - 20) / 2, icon_get(GLYPH_ARROW_R, 20),
                          hover == 21 ? HEX(0xFFFFFF) : RGBA(255, 255, 255, 200));
        }
        if ((timer_ms() - blink) / 530 % 2 == 0 && !shaking)
            gfx_fill(g, pw_len ? cx + 2 : f.x + 13, f.y + 8, 2, f.h - 16, HEX(0xFFFFFF));
        int below = f.y + f.h + 12;
        if (failures && accounts[sel].hint)
            text_shadow_centered(g, &font_ui, 0, below, W, accounts[sel].hint, RGBA(255, 255, 255, 220));
        if (!locked) {
            rect_t c = cancel_rect();
            text_shadow_centered(g, &font_ui, c.x, c.y + 4, c.w, "Cancel", hover == 20 ? HEX(0xFFFFFF) : RGBA(255, 255, 255, 190));
        } else {
            text_shadow_centered(g, &font_ui, 0, below + (failures ? 20 : 0), W, "Locked", RGBA(255, 255, 255, 150));
        }
    }

    static const int glyphs[3] = { GLYPH_SLEEP, GLYPH_RESTART, GLYPH_POWER };
    static const char *labels[3] = { "Sleep", "Restart", "Shut Down" };
    for (int i = 0; i < 3; i++) {
        rect_t r = power_rect(i);
        int cx = r.x + r.w / 2;
        gfx_circle(g, cx, r.y + 22, 22, hover == 10 + i ? RGBA(255, 255, 255, 90) : RGBA(255, 255, 255, 50));
        gfx_blit_tint(g, cx - 10, r.y + 12, icon_get(glyphs[i], 20), HEX(0xFFFFFF));
        text_shadow_centered(g, &font_ui, r.x - 10, r.y + 52, r.w + 20, labels[i], HEX(0xFFFFFF));
    }
}

static void invalidate_users(void) { ui_invalidate(rect_make(0, users_y() - 10, g_screen.w, g_screen.h - users_y() + 10)); }

static void sign_in(void)
{
    const account_t *a = &accounts[sel];
    if (a->password && strcmp(pw, a->password)) {
        failures++;
        shaking = true;
        shake_start = timer_ms();
        pw_len = 0;
        pw[0] = 0;
        ui_beep(330, 80);
        kprintf("login: wrong password\n");
        invalidate_users();
        return;
    }
    if (!locked && strcmp(g_user_name, a->name))
        strlcpy(g_user_name, a->name, sizeof g_user_name);
    pw_len = 0;
    pw[0] = 0;
    ui_login();
}

static void choose(int i)
{
    sel = i;
    pw_len = 0;
    pw[0] = 0;
    failures = 0;
    blink = timer_ms();
    if (!accounts[i].password)
        sign_in();
    else
        ui_invalidate_all();
}

static void power_button(int i)
{
    if (i == 0)
        ui_power(0);
    else
        screens_goodbye(i == 1 ? 2 : 1);
}

bool screens_login_mouse(int kind, int x, int y, int button)
{
    int h = -1;
    if (sel < 0) {
        for (int i = 0; i < N_ACCOUNTS; i++)
            if (rect_contains(list_rect(i), x, y))
                h = i;
    } else {
        if (!locked && rect_contains(cancel_rect(), x, y))
            h = 20;
        rect_t f = field_rect();
        if (pw_len && rect_contains(rect_make(f.x + f.w - 32, f.y, 32, f.h), x, y))
            h = 21;
    }
    for (int i = 0; i < 3; i++)
        if (rect_contains(power_rect(i), x, y))
            h = 10 + i;
    if (h != hover) {
        hover = h;
        invalidate_users();
    }
    ui_set_cursor(CURSOR_ARROW);
    if (kind == MS_UP && button == MOUSE_LEFT) {
        if (h >= 0 && h < N_ACCOUNTS)
            choose(h);
        else if (h >= 10 && h < 13)
            power_button(h - 10);
        else if (h == 20) {
            sel = -1;
            ui_invalidate_all();
        } else if (h == 21)
            sign_in();
    }
    return true;
}

void screens_login_key(const input_event_t *ev)
{
    if (!ev->pressed)
        return;
    if (sel < 0) {
        if (ev->key == KEY_LEFT || ev->key == KEY_RIGHT) {
            hover = ((hover < 0 || hover >= N_ACCOUNTS ? 0 : hover) + (ev->key == KEY_RIGHT ? 1 : N_ACCOUNTS - 1)) % N_ACCOUNTS;
            invalidate_users();
        } else if (ev->key == KEY_ENTER) {
            choose(hover >= 0 && hover < N_ACCOUNTS ? hover : 0);
        } else if (ev->key >= 32 && ev->key < 127) {
            /* typing straight away picks the first account */
            choose(0);
            screens_login_key(ev);
        }
        return;
    }
    switch (ev->key) {
    case KEY_ENTER: sign_in(); return;
    case KEY_ESC:
        if (!locked)
            sel = -1;
        pw_len = 0;
        pw[0] = 0;
        ui_invalidate_all();
        return;
    case KEY_BACKSPACE:
        if (pw_len)
            pw[--pw_len] = 0;
        break;
    default:
        if (ev->key >= 32 && ev->key < 127 && pw_len < (int)sizeof pw - 1) {
            pw[pw_len++] = (char)ev->key;
            pw[pw_len] = 0;
        } else {
            return;
        }
    }
    blink = timer_ms();
    shaking = false;
    invalidate_users();
}

void screens_login_tick(void)
{
    static int phase = -1;
    int p = (timer_ms() - blink) / 530 % 2;
    if (p != phase) {
        phase = p;
        if (sel >= 0)
            ui_invalidate(field_rect());
    }
    if (shaking) {
        invalidate_users();
        if (timer_ms() - shake_start > 500)
            shaking = false;
    }
    datetime_t dt;
    rtc_read(&dt);
    if (dt.minute != last_minute) {
        last_minute = dt.minute;
        ui_invalidate(rect_make(0, 60, g_screen.w, 160));
    }
}

/* ---- the shut down / restart / log out alert ------------------------------------------------------- */

#define AL_W 300
#define AL_SECONDS 60

static int ov_kind = -1; /* -1 closed, 0 shut down, 1 log out, 2 restart */
static uint32_t ov_start;
static int ov_hover = -1, ov_pressed = -1;
static int ov_shown_secs = -1;

bool overlay_is_open(void) { return ov_kind >= 0; }

static void ov_texts(const char **title, char *msg, int n, const char **action)
{
    int left = AL_SECONDS - (int)((timer_ms() - ov_start) / 1000);
    left = MAX(left, 0);
    static char title_buf[80];
    if (ov_kind == 0) {
        *title = "Are you sure you want to shut down your computer now?";
        *action = "Shut Down";
        ksnprintf(msg, n, "If you do nothing, the computer will shut down automatically in %d seconds.", left);
    } else if (ov_kind == 2) {
        *title = "Are you sure you want to restart your computer now?";
        *action = "Restart";
        ksnprintf(msg, n, "If you do nothing, the computer will restart automatically in %d seconds.", left);
    } else {
        ksnprintf(title_buf, sizeof title_buf, "Are you sure you want to quit all applications and log out now?");
        *title = title_buf;
        *action = "Log Out";
        ksnprintf(msg, n, "If you do nothing, you will be logged out automatically in %d seconds.", left);
    }
}

static int ov_height(void)
{
    const char *title, *action;
    char msg[160];
    ov_texts(&title, msg, sizeof msg, &action);
    int h = 20 + 64 + 14;
    h += wrap_centered(NULL, &font_ui_bold, 0, 0, AL_W - 40, title, 0, 17);
    h += 8 + wrap_centered(NULL, &font_ui, 0, 0, AL_W - 40, msg, 0, 15);
    return h + 20 + 24 + 18;
}

rect_t overlay_rect(void)
{
    int h = ov_height();
    return rect_make((g_screen.w - AL_W) / 2, (g_screen.h - h) / 2 - 60, AL_W, h);
}

static rect_t ov_button(int i)
{
    rect_t d = overlay_rect();
    int bw = (AL_W - 40 - 10) / 2;
    return rect_make(d.x + 20 + i * (bw + 10), d.y + d.h - 18 - 24, bw, 24);
}

static rect_t ov_extent(void)
{
    rect_t d = overlay_rect();
    return rect_make(d.x - 40, d.y - 30, d.w + 80, d.h + 80);
}

void overlay_open(int kind)
{
    if (ov_kind >= 0)
        ui_invalidate(ov_extent());
    ov_kind = kind;
    ov_start = timer_ms();
    ov_hover = ov_pressed = -1;
    ov_shown_secs = -1;
    menu_close();
    launchpad_close();
    spotlight_close();
    ui_invalidate(ov_extent());
}

static void overlay_close(void)
{
    ui_invalidate(ov_extent());
    ov_kind = -1;
}

static void activate(void)
{
    int kind = ov_kind;
    overlay_close();
    if (kind == 1)
        ui_logoff();
    else
        ui_power(kind == 2 ? 2 : 1);
}

void overlay_tick(void)
{
    if (ov_kind < 0)
        return;
    int secs = (int)((timer_ms() - ov_start) / 1000);
    if (secs != ov_shown_secs) {
        ov_shown_secs = secs;
        ui_invalidate(overlay_rect());
    }
    if (secs >= AL_SECONDS)
        activate();
}

void overlay_paint(gfx_t *g, rect_t dirty)
{
    (void)dirty;
    if (ov_kind < 0)
        return;
    rect_t d = overlay_rect();
    gfx_shadow(g, d.x, d.y, d.w, d.h, 12, 26, 12, 80);
    gfx_blur_behind(g, d.x, d.y, d.w, d.h, 12, 14, RGBA(240, 240, 242, 226), CORNER_ALL);
    gfx_round_frame(g, d.x, d.y, d.w, d.h, 12, RGBA(0, 0, 0, 50), CORNER_ALL);

    const char *title, *action;
    char msg[160];
    ov_texts(&title, msg, sizeof msg, &action);
    gfx_blit_alpha(g, d.x + (AL_W - 64) / 2, d.y + 20, icon_get(ICON_KEPLER, 64), 255);
    int y = d.y + 20 + 64 + 14;
    y += wrap_centered(g, &font_ui_bold, d.x + 20, y, AL_W - 40, title, COL_TEXT, 17);
    y += 8;
    wrap_centered(g, &font_ui, d.x + 20, y, AL_W - 40, msg, HEX(0x4A4A4E), 15);
    for (int i = 0; i < 2; i++) {
        rect_t r = ov_button(i);
        int st = (i == 1 ? BTN_DEFAULT : 0) | (ov_pressed == i && ov_hover == i ? BTN_PRESSED : 0);
        draw_button(g, r.x, r.y, r.w, r.h, i ? action : "Cancel", st);
    }
}

bool overlay_mouse(int kind, int x, int y, int button)
{
    int h = -1;
    for (int i = 0; i < 2; i++)
        if (rect_contains(ov_button(i), x, y))
            h = i;
    if (h != ov_hover) {
        ov_hover = h;
        ui_invalidate(overlay_rect());
    }
    if (kind == MS_DOWN && button == MOUSE_LEFT) {
        ov_pressed = h;
        ui_invalidate(overlay_rect());
    } else if (kind == MS_UP && button == MOUSE_LEFT) {
        int p = ov_pressed;
        ov_pressed = -1;
        if (p >= 0 && p == h) {
            if (h == 0)
                overlay_close();
            else
                activate();
        } else if (ov_kind >= 0) {
            ui_invalidate(overlay_rect());
        }
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
        activate();
}

/* ---- goodbye ------------------------------------------------------------------------------------------- */

void screens_goodbye(int action)
{
    gfx_t g = video_gfx();
    int W = g_screen.w, H = g_screen.h;
    /* fade to black, then the logo for a moment */
    for (int i = 1; i <= 8; i++) {
        gfx_desaturate(&g, 0, 0, W, H, 0, 64);
        video_flush(rect_make(0, 0, W, H));
        sleep_ms(30);
    }
    gfx_fill(&g, 0, 0, W, H, HEX(0x000000));
    gfx_blit_tint(&g, (W - 96) / 2, H / 2 - 110, icon_get(GLYPH_KEPLER, 96), RGBA(255, 255, 255, 230));
    gfx_text_centered(&g, &font_ui, 0, H / 2 + 30, W, action == 2 ? "Restarting..." : "Shutting down...", HEX(0x8E8E93));
    video_flush(rect_make(0, 0, W, H));
    kprintf("goodbye: %s\n", action == 2 ? "restart" : "power off");
    sleep_ms(1800);
    if (action == 2)
        power_reboot();
    power_off();
}
