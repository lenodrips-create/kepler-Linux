#pragma once

#include "gfx.h"

/* Full-colour icons, Big Sur style. Apps keep their old names (ICON_CMD is the
 * Terminal, ICON_DISPLAY System Preferences, ICON_RECYCLE the Trash...). */
enum {
    ICON_COMPUTER,
    ICON_DOCUMENTS,
    ICON_RECYCLE, /* empty Trash */
    ICON_CMD,
    ICON_NOTEPAD,
    ICON_CALC,
    ICON_PAINT,
    ICON_MINES,
    ICON_TASKMGR,
    ICON_PICTURES, /* the Preview app */
    ICON_HELP,
    ICON_DISPLAY,
    ICON_RUN, /* Spotlight */
    ICON_LOGOFF,
    ICON_POWER,
    ICON_FOLDER,
    ICON_TEXTFILE,
    ICON_DRIVE,
    ICON_CDROM,
    ICON_KEPLER,
    ICON_ALLPROGS, /* Launchpad */
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
    ICON_FINDER,
    ICON_TRASH_FULL,
    ICON_IMAGEFILE,
    ICON_FOLDER_DESKTOP,
    ICON_FOLDER_PICTURES,
    ICON_FOLDER_DOWNLOADS,
    ICON_FOLDER_APPS,
    ICON_GUEST,
    ICON_BROWSER, /* Kepler: drawn from a picture, see icons_set_image */
    ICON_LINUXTERM, /* the Linux Terminal (bash), drawn from a picture */

    /* white template glyphs: draw them with gfx_blit_tint in any colour */
    GLYPH_KEPLER,
    GLYPH_WIFI,
    GLYPH_SPEAKER,
    GLYPH_SEARCH,
    GLYPH_CONTROL,
    GLYPH_AIRDROP,
    GLYPH_RECENTS,
    GLYPH_DESKTOP,
    GLYPH_APPS,
    GLYPH_DOCUMENT,
    GLYPH_DOWNLOADS,
    GLYPH_CLOUD,
    GLYPH_DISK,
    GLYPH_TRASH,
    GLYPH_CHEVRON_L,
    GLYPH_CHEVRON_R,
    GLYPH_GRID,
    GLYPH_LIST,
    GLYPH_COLUMNS,
    GLYPH_GALLERY,
    GLYPH_GROUP,
    GLYPH_SHARE,
    GLYPH_TAG,
    GLYPH_MORE,
    GLYPH_ARROW_R,
    GLYPH_SLEEP,
    GLYPH_RESTART,
    GLYPH_POWER,
    GLYPH_PERSON,
    GLYPH_TL_CLOSE, /* the traffic light marks */
    GLYPH_TL_MIN,
    GLYPH_TL_ZOOM,
    GLYPH_CHECK,
    GLYPH_CHEVRON_D,
    GLYPH_CMD, /* the Command key sign */
    GLYPH_LOCK,
    GLYPH_RELOAD,
    GLYPH_HOME,
    GLYPH_CLOSE,
    GLYPH_SHIELD,
    GLYPH_GLOBE,
    ICON_COUNT
};

/* Rendered on first use and cached for the common sizes (12 to 128 px). */
const image_t *icon_get(int id, int size);
void icons_prerender(void (*progress)(int done, int total));

/* icons drawn from a picture instead of vectors, shaped like the other app icons */
void icons_set_image(int id, const image_t *src);
/* a picture with its own shape and transparency, used as the icon unchanged */
void icons_set_image_plain(int id, const image_t *src);

/* the account picture, cut from the wallpaper; round */
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
