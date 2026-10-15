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

window_t *explorer_open(const char *path) { return stub("My Computer", ICON_COMPUTER); }
window_t *paint_open(void) { return stub("untitled - Paint", ICON_PAINT); }
window_t *mines_open(void) { return stub("Minesweeper", ICON_MINES); }
window_t *taskmgr_open(int tab) { return stub("Task Manager", ICON_TASKMGR); }
window_t *about_open(void) { return stub("About KeplerOS", ICON_KEPLER); }
window_t *display_open(void) { return stub("Display Properties", ICON_DISPLAY); }
window_t *viewer_open(const char *path) { return stub("Picture Viewer", ICON_PICTURES); }
window_t *sysprops_open(void) { return stub("System Properties", ICON_SYSINFO); }


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
