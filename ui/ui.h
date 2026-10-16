#pragma once

#include "font.h"
#include "gfx.h"
#include "icons.h"
#include "input.h"
#include "video.h"

/* ---- theme: Big Sur-like colours ------------------------------------------------- */

#define COL_FACE      HEX(0xECECEC) /* window background */
#define COL_FACE_LT   HEX(0xF6F6F6)
#define COL_SHADOW    HEX(0xC9C9C9)
#define COL_DKSHADOW  HEX(0x8E8E8E)
#define COL_ACCENT    HEX(0x0A64DE) /* system blue */
#define COL_SELECT    HEX(0x0A64DE)
#define COL_TEXT      HEX(0x1D1D1F)
#define COL_GRAYTEXT  HEX(0x9B9B9B)
#define COL_WINDOW    HEX(0xFFFFFF)
#define COL_EDIT_EDGE HEX(0xC6C6C6)
#define COL_LINK      HEX(0x0A64DE)
#define COL_MENU_HI   HEX(0x2A6CF0)
#define COL_SEP       HEX(0xD9D9D9)

#define TOPBAR_H      0 /* the global menu bar is gone for now: menus are in the windows, the clock in the dock */
#define DOCK_RESERVE  78 /* kept free above the bottom edge for the dock */
#define TITLE_H       28
#define FRAME         0
#define WIN_RADIUS    10

/* ---- shell (shell.c) ----------------------------------------------------------- */

void ui_invalidate(rect_t r);
void ui_invalidate_all(void);
int ui_mouse_x(void);
int ui_mouse_y(void);
void ui_set_cursor(int cursor);
void ui_launch(const char *cmd);
void ui_logoff(void);
void ui_login(void);
void ui_lock(void);
void ui_power(int action); /* 0 sleep, 1 shut down, 2 restart */
/* where windows live: between the menu bar and the dock */
rect_t ui_work_area(void);
/* the open window list or the active app changed: refresh the menu bar and dock */
void shell_windows_changed(void);
/* the pre-blurred wallpaper behind translucent surfaces */
const image_t *ui_vibrancy(void);
void ui_tooltip(const char *text, int x, int y);
bool ui_overlay_active(void);
void ui_beep(int hz, int ms);
/* the desktop and its windows are on screen (not the sign-in screen, Launchpad or an alert) */
bool ui_desktop_shown(void);
/* the shell, not another program's window, should get the keyboard */
bool ui_wants_keyboard(void);

#ifdef KEPLER_HOSTED
/* Running on Linux under X (linux/x11.c): other programs' windows live in the
 * window manager next to the shell's own. */
void host_pump(void);                        /* X events in: input, windows coming and going */
void host_sync(void);                        /* after drawing: window placement, focus, input routing */
void host_wait(int ms);                      /* sleep until X has news or ms pass */
void host_set_cursor(int cursor);
void host_paint_popups(gfx_t *g, rect_t dirty); /* other programs' menus and tooltips */
void host_beep(void);
void host_launch(const char *program, const char *arg); /* start an installed program, e.g. Kepler */
void shell_main(void);
#endif

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
    /* WF_UNIFIED windows: is (x, y) a control in the title/toolbar area? Elsewhere there drags the window */
    bool (*caption_hit)(window_t *w, int x, int y);
    /* windows owned by another program: asked to close, they go away later by themselves */
    void (*close_request)(window_t *w);
} window_ops_t;

#define WF_RESIZABLE  0x01
#define WF_MAXIMIZED  0x02
#define WF_MINIMIZED  0x04
#define WF_DIALOG     0x08 /* close button only */
#define WF_NO_TASKBAR 0x10 /* not shown in the dock */
#define WF_NO_MAX     0x20
#define WF_UNIFIED    0x40 /* the client covers the title bar; the app draws its own toolbar there */
#define WF_ALERT      0x80 /* WF_UNIFIED without traffic lights: alerts, moved by their background */
#define WF_FULLSCREEN 0x100 /* covers the whole screen, no frame */

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
    char app[32];          /* the app name shown in bold in the menu bar */
    struct menubar *menus; /* this app's menus, shown in the menu bar while it is active */
    void (*menu_fn)(int id, void *ctx);
    void *menu_ctx;
    int caption_h;  /* WF_UNIFIED: height of the app's toolbar */
    image_t *thumb; /* snapshot shown in the dock while minimised */
    bool closing;   /* close_request windows: really closing now */
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
/* app name and the menus to show in the menu bar while the window is active */
void wm_set_app(window_t *w, const char *app, struct menubar *menus, void (*fn)(int id, void *ctx), void *ctx);
/* the window whose app owns the menu bar right now (dialogs defer to their owner), or NULL */
window_t *wm_menu_owner(void);
/* the frontmost window of an app, or NULL */
window_t *wm_app_window(const char *app);
void wm_hide_app(const char *app);
int wm_count(void);
bool wm_alive(window_t *w);
void wm_minimize_all(void);
void wm_set_fullscreen(window_t *w, bool on);
/* the window covering the screen, if the active one does */
window_t *wm_fullscreen(void);
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

typedef struct menubar {
    const menubar_entry_t *entries;
    int n;
    int open; /* index of the open menu, -1 if none */
    int hover;
} menubar_t;

/* Window menus live in the global menu bar now, so the in-window bar takes no
 * space: menubar_paint draws nothing and menubar_mouse never claims a click. */
#define MENUBAR_H 0
void menubar_paint(menubar_t *mb, gfx_t *g, int w);
bool menubar_mouse(menubar_t *mb, window_t *win, int kind, int x, int y, menu_cb cb, void *ctx);
/* open a menu bar menu at (x, y); slide() lets the caller switch menus while dragging along its bar */
void menu_open_bar(const menu_item_t *items, int n, int x, int y, menu_cb cb, void *ctx, rect_t bar,
                   void (*slide)(int x, int y));
/* the open menus' rectangles (for the compositor: they are translucent) */
int menu_rects(rect_t *out, int max);
/* the items of the open top-level menu, or NULL */
const menu_item_t *menu_current(void);
/* shortcut text for menus: "Ctrl+" is drawn as the Command sign */
int shortcut_width(const char *s);
void shortcut_paint(gfx_t *g, int x, int y, const char *s, uint32_t c);

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
/* a segmented control centred in w; the content box below starts at y + TABS_H */
#define TABS_H 22
void draw_tabs(gfx_t *g, int x, int y, int w, const char *const *tabs, int n, int active);
int tabs_hit(int x, int y, int w, const char *const *tabs, int n, int mx, int my);
/* the rounded panel a tab view's pages sit in */
void draw_tab_panel(gfx_t *g, int x, int y, int w, int h);
void draw_statusbar(gfx_t *g, int y, int w, const char *text);

#define SCROLLBAR_W 15
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

/* ---- desktop, menu bar, dock, launchers, overlays ----------------------------------- */

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
/* Finder's menus, shown when no window is active */
void desktop_menu_command(int id, void *ctx);
extern menubar_t desktop_menus;

/* the global menu bar (topbar.c) */
void topbar_paint(gfx_t *g, rect_t dirty);
bool topbar_mouse(int kind, int x, int y, int button);
void topbar_tick(void);
void topbar_changed(void);
/* true when the menu bar text should be white (dark wallpaper) */
bool topbar_dark(void);
void topbar_background_changed(void);
/* the KeplerOS menu: About, System Preferences, Sleep, Restart, Shut Down, Lock, Log Out */
void kepler_menu_open(int x, int y);

/* the dock (dock.c) */
rect_t dock_rect(void);
void dock_paint(gfx_t *g, rect_t dirty);
bool dock_mouse(int kind, int x, int y, int button);
void dock_tick(void);
void dock_changed(void);

/* Launchpad and Spotlight (launcher.c) */
void launchpad_toggle(void);
void launchpad_close(void);
bool launchpad_is_open(void);
void launchpad_paint(gfx_t *g, rect_t dirty);
bool launchpad_mouse(int kind, int x, int y, int button);
void launchpad_key(const input_event_t *ev);
void spotlight_open(void);
void spotlight_close(void);
bool spotlight_is_open(void);
rect_t spotlight_rect(void);
void spotlight_paint(gfx_t *g);
bool spotlight_mouse(int kind, int x, int y, int button);
void spotlight_key(const input_event_t *ev);
void spotlight_tick(void);

/* the apps the dock, Launchpad and Spotlight know about */
typedef struct {
    const char *name; /* also the app name its windows carry */
    int icon;
    const char *cmd;
    bool in_dock;
} app_info_t;
extern const app_info_t g_apps[];
extern const int g_app_count;
const app_info_t *app_find(const char *name);

/* full-screen modes and the shut down / log out alert (screens.c) */
void screens_boot(void (*work)(void (*progress)(int pct)));
void screens_login_paint(gfx_t *g, rect_t dirty);
bool screens_login_mouse(int kind, int x, int y, int button);
void screens_login_key(const input_event_t *ev);
void screens_login_tick(void);
/* locked: only the signed-in user, windows stay open */
void screens_login_reset(bool locked);
void overlay_open(int kind); /* 0 shut down, 1 log out, 2 restart */
bool overlay_is_open(void);
rect_t overlay_rect(void);
void overlay_paint(gfx_t *g, rect_t dirty);
bool overlay_mouse(int kind, int x, int y, int button);
void overlay_key(const input_event_t *ev);
void overlay_tick(void);
void screens_goodbye(int action);

/* user name shown in the menu bar and on the sign-in screen */
extern char g_user_name[32];
