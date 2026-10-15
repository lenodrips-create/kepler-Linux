/* Early output: COM1 for logs, the VGA text buffer until graphics start. */

#include "kernel.h"

#define COM1 0x3F8

static bool serial_ok;

void serial_init(void)
{
    outb(COM1 + 1, 0x00); /* no interrupts */
    outb(COM1 + 3, 0x80); /* DLAB */
    outb(COM1 + 0, 0x01); /* 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    serial_ok = true;
}

static void serial_putc(char c)
{
    if (!serial_ok)
        return;
    for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++)
        ;
    outb(COM1, c);
}

void serial_write(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s);
    }
}

/* ---- VGA text mode -------------------------------------------------------- */

static volatile uint16_t *const vga = (uint16_t *)0xB8000;
static int col, row;
static uint8_t attr = 0x07;
static bool text_on;

static void vga_cursor(void)
{
    uint16_t pos = row * 80 + col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, pos >> 8);
}

void vga_text_init(void)
{
    text_on = true;
    col = row = 0;
    for (int i = 0; i < 80 * 25; i++)
        vga[i] = (uint16_t)attr << 8 | ' ';
    vga_cursor();
}

void vga_text_color(uint8_t a) { attr = a; }
void vga_text_disable(void) { text_on = false; }

static void vga_putc(char c)
{
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
    } else {
        vga[row * 80 + col] = (uint16_t)attr << 8 | (uint8_t)c;
        if (++col == 80) {
            col = 0;
            row++;
        }
    }
    if (row == 25) {
        memmove((void *)vga, (void *)(vga + 80), 80 * 24 * 2);
        for (int i = 0; i < 80; i++)
            vga[24 * 80 + i] = (uint16_t)attr << 8 | ' ';
        row = 24;
    }
}

void vga_text_write(const char *s)
{
    if (!text_on)
        return;
    while (*s)
        vga_putc(*s++);
    vga_cursor();
}
