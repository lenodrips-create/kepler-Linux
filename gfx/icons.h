#pragma once

#include "gfx.h"

enum {
    ICON_COMPUTER,
    ICON_DOCUMENTS,
    ICON_RECYCLE,
    ICON_CMD,
    ICON_NOTEPAD,
    ICON_CALC,
    ICON_PAINT,
    ICON_MINES,
    ICON_TASKMGR,
    ICON_PICTURES,
    ICON_HELP,
    ICON_DISPLAY,
    ICON_RUN,
    ICON_LOGOFF,
    ICON_POWER,
    ICON_FOLDER,
    ICON_TEXTFILE,
    ICON_DRIVE,
    ICON_CDROM,
    ICON_KEPLER,
    ICON_ALLPROGS,
    ICON_STANDBY,
    ICON_RESTART,
    ICON_SWITCHUSER,
    ICON_SPEAKER,
    ICON_NETWORK,
    ICON_SHOWDESKTOP,
    ICON_INFO,
    ICON_WARNING,
    ICON_ERROR,
    ICON_BACK,
    ICON_FORWARD,
    ICON_UP,
    ICON_SYSINFO,
    ICON_COUNT
};

/* Rendered on first use and cached; sizes 16, 24, 32 and 48 look best. */
const image_t *icon_get(int id, int size);
void icons_prerender(void (*progress)(int done, int total));

/* the account picture, cut from the wallpaper */
void icons_set_user_picture(const image_t *wallpaper);
const image_t *icon_user_picture(int size);

enum {
    CURSOR_ARROW,
    CURSOR_IBEAM,
    CURSOR_SIZE_WE,
    CURSOR_SIZE_NS,
    CURSOR_SIZE_NWSE,
    CURSOR_SIZE_NESW,
    CURSOR_MOVE,
    CURSOR_BUSY,
    CURSOR_HAND,
};

/* draws the cursor with its hotspot at (x, y); returns its bounding box */
rect_t cursor_bounds(int cursor, int x, int y);
void cursor_draw(gfx_t *g, int cursor, int x, int y);
