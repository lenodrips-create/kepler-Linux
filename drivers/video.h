#pragma once

#include "gfx.h"
#include "multiboot.h"

#define SCREEN_W 1280
#define SCREEN_H 800

typedef struct {
    uint32_t *fb;   /* the visible framebuffer */
    int pitch;      /* framebuffer pitch in pixels */
    uint32_t *back; /* what we draw into */
    int w, h;
} screen_t;

extern screen_t g_screen;

bool video_init(multiboot_info_t *mbi);
void video_flush(rect_t r);
gfx_t video_gfx(void);
