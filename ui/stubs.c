/* Temporary stand-ins while the real modules are written. */

#include "apps.h"
#include "ui.h"

static const window_ops_t stub_ops = { 0 };

static window_t *stub(const char *title, int icon)
{
    window_t *w = wm_create(title, icon, 420, 300, &stub_ops, NULL, WF_RESIZABLE);
    w->bg = HEX(0xFFFFFF);
    return w;
}

window_t *notepad_open(const char *path) { return stub("Untitled - Notepad", ICON_NOTEPAD); }
window_t *calc_open(void) { return stub("Calculator", ICON_CALC); }
window_t *terminal_open(void) { return stub("Command Prompt", ICON_CMD); }
window_t *explorer_open(const char *path) { return stub("My Computer", ICON_COMPUTER); }
window_t *paint_open(void) { return stub("untitled - Paint", ICON_PAINT); }
window_t *mines_open(void) { return stub("Minesweeper", ICON_MINES); }
window_t *taskmgr_open(int tab) { return stub("Task Manager", ICON_TASKMGR); }
window_t *about_open(void) { return stub("About KeplerOS", ICON_KEPLER); }
window_t *display_open(void) { return stub("Display Properties", ICON_DISPLAY); }
window_t *viewer_open(const char *path) { return stub("Picture Viewer", ICON_PICTURES); }
window_t *sysprops_open(void) { return stub("System Properties", ICON_SYSINFO); }

void msgbox(window_t *owner, const char *title, const char *text, int icon) { stub(title, icon); }
void prompt_box(window_t *owner, const char *title, const char *label, const char *initial, prompt_cb cb, void *ctx) {}
void run_dialog(void) { stub("Run", ICON_RUN); }

static bool sm_open;
void startmenu_toggle(void) { sm_open = !sm_open; taskbar_changed(); }
void startmenu_close(void) { sm_open = false; }
bool startmenu_is_open(void) { return sm_open; }
void startmenu_paint(gfx_t *g) {}
bool startmenu_mouse(int kind, int x, int y, int button) { return false; }
bool startmenu_key(const input_event_t *ev) { return false; }

void screens_boot(void (*work)(void (*progress)(int pct)));
static void no_progress(int pct) {}
void screens_boot(void (*work)(void (*progress)(int pct))) { work(no_progress); }
void screens_welcome_paint(gfx_t *g, rect_t dirty) {}
bool screens_welcome_mouse(int kind, int x, int y, int button) { return false; }
void screens_welcome_key(const input_event_t *ev) {}
void screens_welcome_tick(void) {}
void screens_welcome_reset(void) { ui_login(); }
void overlay_open(int kind) {}
bool overlay_is_open(void) { return false; }
void overlay_paint(gfx_t *g, rect_t dirty) {}
bool overlay_mouse(int kind, int x, int y, int button) { return false; }
void overlay_key(const input_event_t *ev) {}
void overlay_tick(void) {}
void screens_goodbye(int action) { power_off(); }
