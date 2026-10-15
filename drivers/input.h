#pragma once

#include "kernel.h"

enum {
    EV_NONE,
    EV_KEY,
    EV_MOUSE,
};

/* non-printable keys; printable ones use their ASCII code */
enum {
    KEY_BACKSPACE = 8,
    KEY_TAB = 9,
    KEY_ENTER = 10,
    KEY_ESC = 27,
    KEY_UP = 0x100,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
    KEY_LWIN,
    KEY_MENU,
    KEY_SHIFT,
    KEY_CTRL,
    KEY_ALT,
    KEY_CAPS,
};

#define MOD_SHIFT 1
#define MOD_CTRL  2
#define MOD_ALT   4

#define MOUSE_LEFT   1
#define MOUSE_RIGHT  2
#define MOUSE_MIDDLE 4

typedef struct {
    uint8_t type;
    uint8_t pressed; /* key events */
    uint8_t mods;
    uint8_t buttons; /* mouse events: current button state */
    int16_t dx, dy;  /* mouse events: screen-space delta (y grows downward) */
    int16_t wheel;
    uint16_t key;
} input_event_t;

void keyboard_init(void);
void mouse_init(void);
bool input_poll(input_event_t *ev);
uint8_t keyboard_mods(void);
