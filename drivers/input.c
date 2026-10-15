/*
 * PS/2 keyboard (scan code set 1, as delivered by the 8042's translation)
 * and PS/2 mouse. Both IRQ handlers push into one ring buffer that the
 * desktop drains from its main loop.
 */

#include "input.h"

#define QSIZE 256

static input_event_t queue[QSIZE];
static volatile uint32_t q_head, q_tail;

static void push(const input_event_t *ev)
{
    uint32_t next = (q_head + 1) % QSIZE;
    if (next == q_tail)
        return; /* full: drop */
    queue[q_head] = *ev;
    q_head = next;
}

bool input_poll(input_event_t *ev)
{
    uint32_t f = irq_save();
    bool any = q_tail != q_head;
    if (any) {
        *ev = queue[q_tail];
        q_tail = (q_tail + 1) % QSIZE;
    }
    irq_restore(f);
    return any;
}

/* ---- 8042 helpers ------------------------------------------------------------ */

static void ps2_wait_write(void)
{
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
        ;
}

static void ps2_wait_read(void)
{
    for (int i = 0; i < 100000 && !(inb(0x64) & 1); i++)
        ;
}

static void ps2_cmd(uint8_t c)
{
    ps2_wait_write();
    outb(0x64, c);
}

static void ps2_data(uint8_t d)
{
    ps2_wait_write();
    outb(0x60, d);
}

static uint8_t ps2_read(void)
{
    ps2_wait_read();
    return inb(0x60);
}

/* ---- keyboard ----------------------------------------------------------------- */

static const uint16_t map_normal[128] = {
    0, KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', KEY_BACKSPACE, KEY_TAB,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', KEY_ENTER, KEY_CTRL, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', KEY_SHIFT, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', KEY_SHIFT, '*', KEY_ALT, ' ', KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, KEY_F11, KEY_F12,
};

static const uint16_t map_shift[128] = {
    0, KEY_ESC, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', KEY_BACKSPACE, KEY_TAB,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', KEY_ENTER, KEY_CTRL, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', KEY_SHIFT, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', KEY_SHIFT, '*', KEY_ALT, ' ', KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, KEY_F11, KEY_F12,
};

static uint8_t mods;
static bool caps, extended;

uint8_t keyboard_mods(void) { return mods; }

static uint16_t extended_key(uint8_t sc)
{
    switch (sc) {
    case 0x1C: return KEY_ENTER;
    case 0x1D: return KEY_CTRL;
    case 0x35: return '/';
    case 0x38: return KEY_ALT;
    case 0x47: return KEY_HOME;
    case 0x48: return KEY_UP;
    case 0x49: return KEY_PGUP;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x4F: return KEY_END;
    case 0x50: return KEY_DOWN;
    case 0x51: return KEY_PGDN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    case 0x5B:
    case 0x5C: return KEY_LWIN;
    case 0x5D: return KEY_MENU;
    default: return 0;
    }
}

static void keyboard_byte(uint8_t sc)
{
    if (sc == 0xE0) {
        extended = true;
        return;
    }
    bool released = sc & 0x80;
    sc &= 0x7F;

    uint16_t key;
    if (extended) {
        extended = false;
        key = extended_key(sc);
    } else {
        bool shift = mods & MOD_SHIFT;
        key = shift ? map_shift[sc] : map_normal[sc];
        if (caps && key < 128 && kisalpha(key))
            key = shift ? ktolower(key) : ktoupper(key);
    }
    if (!key)
        return;

    uint8_t bit = key == KEY_SHIFT ? MOD_SHIFT : key == KEY_CTRL ? MOD_CTRL : key == KEY_ALT ? MOD_ALT : 0;
    if (bit) {
        if (released)
            mods &= ~bit;
        else
            mods |= bit;
    }
    if (key == KEY_CAPS && !released)
        caps = !caps;

    input_event_t ev = { 0 };
    ev.type = EV_KEY;
    ev.pressed = !released;
    ev.mods = mods;
    ev.key = key;
    push(&ev);
}

static void keyboard_irq(struct regs *r)
{
    (void)r;
    uint8_t status = inb(0x64);
    if (!(status & 1))
        return;
    uint8_t b = inb(0x60);
    if (status & 0x20)
        return; /* mouse byte that raced in on IRQ1 */
    keyboard_byte(b);
}

void keyboard_init(void)
{
    while (inb(0x64) & 1)
        inb(0x60);
    irq_install(1, keyboard_irq);
}

/* ---- mouse --------------------------------------------------------------------- */

static uint8_t packet[4];
static int packet_len, packet_size = 3;

static void mouse_write(uint8_t v)
{
    ps2_cmd(0xD4);
    ps2_data(v);
    ps2_read(); /* ACK */
}

static void mouse_irq(struct regs *r)
{
    (void)r;
    uint8_t status = inb(0x64);
    if (!(status & 1))
        return;
    uint8_t b = inb(0x60);
    if (packet_len == 0 && !(b & 0x08))
        return; /* resync: first byte always has bit 3 set */
    packet[packet_len++] = b;
    if (packet_len < packet_size)
        return;
    packet_len = 0;

    uint8_t flags = packet[0];
    if (flags & 0xC0)
        return; /* overflow, ignore */
    int dx = (int)packet[1] - ((flags << 4) & 0x100);
    int dy = (int)packet[2] - ((flags << 3) & 0x100);
    input_event_t ev = { 0 };
    ev.type = EV_MOUSE;
    ev.buttons = flags & 7;
    ev.dx = dx;
    ev.dy = -dy;
    if (packet_size == 4)
        ev.wheel = (int8_t)(packet[3] << 4) >> 4;
    ev.mods = mods;
    push(&ev);
}

void mouse_init(void)
{
    ps2_cmd(0xA8); /* enable the aux port */
    ps2_cmd(0x20);
    uint8_t cfg = ps2_read();
    cfg |= 0x02;  /* IRQ12 */
    cfg &= ~0x20; /* mouse clock on */
    ps2_cmd(0x60);
    ps2_data(cfg);

    mouse_write(0xF6); /* defaults */

    /* IntelliMouse knock: sample rates 200, 100, 80 turn on the wheel */
    mouse_write(0xF3);
    mouse_write(200);
    mouse_write(0xF3);
    mouse_write(100);
    mouse_write(0xF3);
    mouse_write(80);
    ps2_cmd(0xD4);
    ps2_data(0xF2);
    ps2_read(); /* ACK */
    uint8_t id = ps2_read();
    packet_size = id == 3 || id == 4 ? 4 : 3;

    mouse_write(0xF4); /* start streaming */
    irq_install(12, mouse_irq);
}
