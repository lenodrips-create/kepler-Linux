#pragma once

/* The Linux side of KeplerOS: the shell runs as an X11 program that is also
 * the window manager. */

#include "input.h"
#include "kernel.h"
#include "video.h"

void host_sysinfo_init(void);
void host_entropy_init(void);
bool host_x11_init(void);
