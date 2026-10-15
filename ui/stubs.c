/* Temporary stand-ins while the real modules are written. */

#include "apps.h"
#include "ui.h"

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
