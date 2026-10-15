#pragma once

#include "ui.h"

window_t *notepad_open(const char *path);
window_t *calc_open(void);
window_t *terminal_open(void);
window_t *explorer_open(const char *path); /* "::computer" for My Computer */
window_t *paint_open(void);
window_t *mines_open(void);
window_t *taskmgr_open(int tab);
window_t *about_open(void);
window_t *display_open(void);
window_t *viewer_open(const char *path);
window_t *sysprops_open(void);
