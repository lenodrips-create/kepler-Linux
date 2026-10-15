#pragma once

#include "font.h"
#include "gfx.h"
#include "icons.h"
#include "input.h"
#include "video.h"

/* ---- theme: Luna-like colours --------------------------------------------------- */

#define COL_FACE      HEX(0xECE9D8)
#define COL_FACE_LT   HEX(0xF7F6F0)
#define COL_SHADOW    HEX(0xACA899)
#define COL_DKSHADOW  HEX(0x716F64)
#define COL_SELECT    HEX(0x316AC5)
#define COL_TEXT      HEX(0x000000)
#define COL_GRAYTEXT  HEX(0xA19F94)
#define COL_WINDOW    HEX(0xFFFFFF)
#define COL_EDIT_EDGE HEX(0x7F9DB9)
#define COL_LINK      HEX(0x0C3FAA)
#define COL_MENU_HI   HEX(0x316AC5)

#define TASKBAR_H 30
#define TITLE_H   30
#define FRAME     4

/* ---- shell (shell.c) ----------------------------------------------------------- */

void ui_invalidate(rect_t r);
void ui_invalidate_all(void);
int ui_mouse_x(void);
int ui_mouse_y(void);
void ui_set_cursor(int cursor);
void ui_launch(const char *cmd);
void ui_logoff(void);
void ui_login(void);
void ui_power(int action); /* 0 stand by, 1 turn off, 2 restart */
void ui_tooltip(const char *text, int x, int y);
bool ui_overlay_active(void);
void ui_beep(int hz, int ms);

/* ---- window manager (wm.c) ----------------------------------------------------- */

enum { MS_DOWN, MS_UP, MS_MOVE, MS_DBLCLICK, MS_WHEEL, MS_LEAVE };

typedef struct window window_t;

typedef struct {
    void (*paint)(window_t *w, gfx_t *g, int cw, int ch);
    void (*key)(window_t *w, const input_event_t *ev);
    /* client coordinates; button is the button that changed (down/up) or the wheel delta */
    void (*mouse)(window_t *w, int kind, int x, int y, int button);
    void (*tick)(window_t *w);
    void (*close)(window_t *w);
    void (*resized)(window_t *w);
} window_ops_t;

#define WF_RESIZABLE  0x01
#define WF_MAXIMIZED  0x02
#define WF_MINIMIZED  0x04
#define WF_DIALOG     0x08 /* close button only */
#define WF_NO_TASKBAR 0x10
#define WF_NO_MAX     0x20

struct window {
    int id;
    int x, y, w, h; /* outer frame */
    char title[96];
    int icon;
    uint32_t flags;
    rect_t restore;
    const window_ops_t *ops;
    void *data;
    window_t *next;  /* z-order, bottom to top */
    window_t *owner; /* dialogs: the window they belong to */
    window_t *modal; /* the dialog currently blocking this window */
    int min_w, min_h;
    uint32_t bg;
    uint32_t tick_ms, tick_last;
    int cursor;
    uint32_t created;
    bool dead; /* closed, waiting to be freed */
};

window_t *wm_create(const char *title, int icon, int cw, int ch, const window_ops_t *ops, void *data, uint32_t flags);
void wm_close(window_t *w);
void wm_focus(window_t *w);
window_t *wm_active(void);
void wm_deactivate(void);
void wm_minimize(window_t *w);
void wm_toggle_maximize(window_t *w);
void wm_restore(window_t *w);
void wm_invalidate(window_t *w);
void wm_invalidate_client(window_t *w);
void wm_invalidate_client_rect(window_t *w, int x, int y, int cw, int ch);
rect_t wm_client_rect(const window_t *w);
void wm_set_title(window_t *w, const char *title);
void wm_move(window_t *w, int x, int y);
void wm_center(window_t *w);
void wm_set_client_size(window_t *w, int cw, int ch);
void wm_set_tick(window_t *w, uint32_t ms);
void wm_set_modal(window_t *owner, window_t *dialog);
window_t *wm_first(void);
window_t *wm_find(const window_ops_t *ops);
int wm_count(void);
bool wm_alive(window_t *w);
void wm_minimize_all(void);
void wm_cycle(void);

/* shell hooks */
void wm_paint(gfx_t *g, rect_t dirty);
bool wm_mouse(int kind, int x, int y, int buttons, int button);
bool wm_dragging(void);
void wm_key(const input_event_t *ev);
void wm_tick(void);

/* ---- popup menus (menu.c) -------------------------------------------------------- */

#define MI_SEP      0x01
#define MI_DISABLED 0x02
#define MI_CHECK    0x04
#define MI_BOLD     0x08
#define MI_RADIO    0x10

typedef struct menu_item {
    const char *label;
    int id;
    uint32_t flags;
    int icon;
    const struct menu_item *sub;
    int sub_n;
    const char *shortcut;
} menu_item_t;

typedef void (*menu_cb)(int id, void *ctx);

void menu_open(const menu_item_t *items, int n, int x, int y, menu_cb cb, void *ctx);
void menu_close(void);
bool menu_is_open(void);
void menu_paint(gfx_t *g);
bool menu_mouse(int kind, int x, int y, int button);
bool menu_key(const input_event_t *ev);

/* menu bars inside windows */
typedef struct {
    const char *title;
    const menu_item_t *items;
    int n;
} menubar_entry_t;

typedef struct {
    const menubar_entry_t *entries;
    int n;
    int open; /* index of the open menu, -1 if none */
    int hover;
} menubar_t;

#define MENUBAR_H 20
void menubar_paint(menubar_t *mb, gfx_t *g, int w);
/* returns true if the event was in the bar */
bool menubar_mouse(menubar_t *mb, window_t *win, int kind, int x, int y, menu_cb cb, void *ctx);

/* ---- widgets (widgets.c) -------------------------------------------------------- */

#define BTN_HOVER    0x01
#define BTN_PRESSED  0x02
#define BTN_DEFAULT  0x04
#define BTN_DISABLED 0x08
#define BTN_FOCUS    0x10

void draw_button(gfx_t *g, int x, int y, int w, int h, const char *label, int state);
void draw_button_face(gfx_t *g, int x, int y, int w, int h, int state);
void draw_textbox(gfx_t *g, int x, int y, int w, int h);
void draw_sunken(gfx_t *g, int x, int y, int w, int h);
void draw_raised(gfx_t *g, int x, int y, int w, int h);
void draw_progress(gfx_t *g, int x, int y, int w, int h, int pct);
void draw_groupbox(gfx_t *g, int x, int y, int w, int h, const char *title);
void draw_checkbox(gfx_t *g, int x, int y, bool checked, const char *label);
void draw_focus_rect(gfx_t *g, int x, int y, int w, int h);
void draw_tabs(gfx_t *g, int x, int y, int w, const char *const *tabs, int n, int active);
int tabs_hit(int x, int y, const char *const *tabs, int n, int mx, int my);
void draw_statusbar(gfx_t *g, int y, int w, const char *text);

#define SCROLLBAR_W 17
typedef struct {
    int total, visible, pos;
    int drag_off; /* -1 when not dragging */
    int hover;
} scrollbar_t;

void scrollbar_paint(scrollbar_t *sb, gfx_t *g, int x, int y, int h);
/* returns true if the scroll position changed */
bool scrollbar_mouse(scrollbar_t *sb, int kind, int x, int y, int sx, int sy, int h);
void scrollbar_clamp(scrollbar_t *sb);

typedef struct {
    int x, y, w, h;
    const char *label;
    int id;
    int flags; /* BTN_DEFAULT, BTN_DISABLED */
} button_t;

typedef struct {
    button_t *items;
    int n;
    int hover, pressed;
} buttons_t;

void buttons_paint(buttons_t *b, gfx_t *g);
/* returns the id of a clicked button, or -1 */
int buttons_mouse(buttons_t *b, window_t *w, int kind, int x, int y);

/* single-line text field editing */
typedef struct {
    char text[128];
    int cursor;
} textfield_t;

void textfield_set(textfield_t *t, const char *s);
bool textfield_key(textfield_t *t, const input_event_t *ev);
void textfield_paint(textfield_t *t, gfx_t *g, int x, int y, int w, bool focused);

/* a single shared text clipboard (widgets.c) */
void clipboard_set(const char *s, int n);
const char *clipboard_get(void);

/* ---- dialogs (dialog.c) ---------------------------------------------------------- */

void msgbox(window_t *owner, const char *title, const char *text, int icon);
typedef void (*prompt_cb)(const char *value, void *ctx);
void prompt_box(window_t *owner, const char *title, const char *label, const char *initial, prompt_cb cb, void *ctx);
void run_dialog(void);

/* ---- desktop, taskbar, start menu, overlays ---------------------------------------- */

void desktop_init(image_t *wallpaper);
void desktop_paint(gfx_t *g, rect_t dirty);
bool desktop_mouse(int kind, int x, int y, int button);
void desktop_key(const input_event_t *ev);
const image_t *desktop_wallpaper(void);
image_t *desktop_wallpaper_source(void);
void desktop_set_background(int style, uint32_t color);
image_t *desktop_render_background(int style, uint32_t color, int w, int h);
int desktop_background_style(void);
uint32_t desktop_background_color(void);
void desktop_refresh_icons(void);

void taskbar_paint(gfx_t *g, rect_t dirty);
bool taskbar_mouse(int kind, int x, int y, int button);
void taskbar_tick(void);
void taskbar_changed(void);
rect_t taskbar_start_button(void);

void startmenu_toggle(void);
void startmenu_close(void);
bool startmenu_is_open(void);
void startmenu_paint(gfx_t *g);
bool startmenu_mouse(int kind, int x, int y, int button);
bool startmenu_key(const input_event_t *ev);

/* full-screen modes and the Turn Off / Log Off overlay (screens.c) */
void screens_boot(void (*work)(void (*progress)(int pct)));
void screens_welcome_paint(gfx_t *g, rect_t dirty);
bool screens_welcome_mouse(int kind, int x, int y, int button);
void screens_welcome_key(const input_event_t *ev);
void screens_welcome_tick(void);
void screens_welcome_reset(void);
void overlay_open(int kind); /* 0 turn off, 1 log off */
bool overlay_is_open(void);
void overlay_paint(gfx_t *g, rect_t dirty);
bool overlay_mouse(int kind, int x, int y, int button);
void overlay_key(const input_event_t *ev);
void overlay_tick(void);
void screens_goodbye(int action);

/* user name shown in the start menu and on the welcome screen */
extern char g_user_name[32];
