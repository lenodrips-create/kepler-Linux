#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEPLER_NAME    "KeplerOS"
#define KEPLER_VERSION "1.0"
#define KEPLER_BUILD   "2600"

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b)    ((a) < (b) ? (a) : (b))
#define MAX(a, b)    ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define ABS(a)       ((a) < 0 ? -(a) : (a))

/* ---- port I/O ---------------------------------------------------------- */

static inline void outb(uint16_t port, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline void outw(uint16_t port, uint16_t v) { __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port)); }
static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port)); }

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void io_wait(void) { outb(0x80, 0); }
static inline void cpu_cli(void) { __asm__ volatile("cli"); }
static inline void cpu_sti(void) { __asm__ volatile("sti"); }
static inline void cpu_hlt(void) { __asm__ volatile("hlt"); }

static inline uint32_t irq_save(void)
{
    uint32_t f;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(f) : : "memory");
    return f;
}

static inline void irq_restore(uint32_t f)
{
    if (f & (1u << 9))
        __asm__ volatile("sti" : : : "memory");
}

/* ---- lib.c -------------------------------------------------------------- */

void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void memset32(uint32_t *dst, uint32_t v, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int strcasecmp(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
size_t strlcpy(char *dst, const char *src, size_t n);
size_t strlcat(char *dst, const char *src, size_t n);
char *kstrdup(const char *s);
int katoi(const char *s);
int ktolower(int c);
int ktoupper(int c);
bool kisdigit(int c);
bool kisspace(int c);
bool kisalpha(int c);

int kvsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

uint32_t krand(void);
void ksrand(uint32_t seed);

/* ---- heap.c ------------------------------------------------------------- */

void heap_init(uintptr_t start, uintptr_t end);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void *krealloc(void *p, size_t n);
void kfree(void *p);
size_t heap_total(void);
size_t heap_used(void);

/* ---- serial.c / console ------------------------------------------------- */

void serial_init(void);
void serial_write(const char *s);

void vga_text_init(void);
void vga_text_write(const char *s);
void vga_text_color(uint8_t attr);
void vga_text_disable(void);

/* ---- cpu.c -------------------------------------------------------------- */

struct regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
};

typedef void (*irq_handler_t)(struct regs *r);

void gdt_init(void);
void idt_init(void);
void irq_install(int irq, irq_handler_t h);
void cpu_identify(char *vendor, char *brand);
uint64_t rdtsc(void);

/* ---- timer.c ------------------------------------------------------------ */

void timer_init(uint32_t hz);
uint32_t timer_ms(void);
void sleep_ms(uint32_t ms);
uint32_t cpu_busy_percent(void);
void timer_account_idle(uint32_t start_tsc_lo, uint32_t end_tsc_lo);

/* ---- rtc.c -------------------------------------------------------------- */

typedef struct {
    int year, month, day, hour, minute, second, weekday;
} datetime_t;

void rtc_read(datetime_t *dt);

/* ---- pci.c -------------------------------------------------------------- */

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
bool pci_find(uint16_t vendor, uint16_t device, uint8_t *bus, uint8_t *slot, uint8_t *func);
int pci_list(char lines[][64], int max);

/* ---- power.c ------------------------------------------------------------ */

void power_off(void) __attribute__((noreturn));
void power_reboot(void) __attribute__((noreturn));

/* ---- system info filled in by kmain ------------------------------------- */

typedef struct {
    uint32_t mem_kb;          /* usable RAM reported by the loader */
    char cpu_vendor[16];
    char cpu_brand[52];
    int screen_w, screen_h;
    char video[48];
    uint32_t boot_ms;
} sysinfo_t;

extern sysinfo_t g_sys;
