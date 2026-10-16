/* Freestanding C library bits: memory, strings, printf, PRNG. */

#include "kernel.h"

#ifndef KEPLER_HOSTED /* on Linux these come from the C library */

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    uint32_t v = (uint8_t)c * 0x01010101u;
    while (n && ((uintptr_t)d & 3)) {
        *d++ = (uint8_t)c;
        n--;
    }
    size_t words = n >> 2;
    __asm__ volatile("rep stosl" : "+D"(d), "+c"(words) : "a"(v) : "memory");
    n &= 3;
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

#endif

void memset32(uint32_t *dst, uint32_t v, size_t n)
{
#ifdef KEPLER_HOSTED
    while (n--)
        *dst++ = v;
#else
    __asm__ volatile("rep stosl" : "+D"(dst), "+c"(n) : "a"(v) : "memory");
#endif
}

#ifndef KEPLER_HOSTED

void *memcpy(void *dst, const void *src, size_t n)
{
    void *ret = dst;
    size_t words = n >> 2, rest = n & 3;
    __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(words) : : "memory");
    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(rest) : : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n);
    /* overlapping, copy backwards */
    d += n;
    s += n;
    while (n && ((uintptr_t)d & 3)) {
        *--d = *--s;
        n--;
    }
    while (n >= 4) {
        d -= 4;
        s -= 4;
        *(uint32_t *)d = *(const uint32_t *)s;
        n -= 4;
    }
    while (n--)
        *--d = *--s;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i])
            return (uint8_t)a[i] - (uint8_t)b[i];
    }
    return 0;
}

#endif

int ktolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int ktoupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
bool kisdigit(int c) { return c >= '0' && c <= '9'; }
bool kisspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool kisalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

#ifndef KEPLER_HOSTED
int strcasecmp(const char *a, const char *b)
{
    while (*a && ktolower(*a) == ktolower(*b)) {
        a++;
        b++;
    }
    return ktolower((uint8_t)*a) - ktolower((uint8_t)*b);
}

char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c ? NULL : (char *)s;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (; *s; s++)
        if (*s == (char)c)
            last = s;
    return (char *)last;
}

char *strstr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (!strncmp(hay, needle, n))
            return (char *)hay;
    return n ? NULL : (char *)hay;
}

char *strcasestr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && ktolower(hay[i]) == ktolower(needle[i]))
            i++;
        if (i == n)
            return (char *)hay;
    }
    return n ? NULL : (char *)hay;
}

size_t strlcpy(char *dst, const char *src, size_t n)
{
    size_t len = strlen(src);
    if (n) {
        size_t c = len < n - 1 ? len : n - 1;
        memcpy(dst, src, c);
        dst[c] = 0;
    }
    return len;
}

size_t strlcat(char *dst, const char *src, size_t n)
{
    size_t d = strlen(dst);
    if (d >= n)
        return d + strlen(src);
    return d + strlcpy(dst + d, src, n - d);
}

#endif

char *kstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = kmalloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

int katoi(const char *s)
{
    int sign = 1, v = 0;
    while (kisspace(*s))
        s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (kisdigit(*s))
        v = v * 10 + (*s++ - '0');
    return v * sign;
}

#ifndef KEPLER_HOSTED
/* ---- 64-bit division helpers normally provided by libgcc ---------------- */

uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    if (den == 0)
        panic("division by zero (64-bit)");
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((num >> i) & 1);
        if (r >= den) {
            r -= den;
            q |= (uint64_t)1 << i;
        }
    }
    if (rem)
        *rem = r;
    return q;
}

uint64_t __udivdi3(uint64_t a, uint64_t b) { return __udivmoddi4(a, b, NULL); }

uint64_t __umoddi3(uint64_t a, uint64_t b)
{
    uint64_t r;
    __udivmoddi4(a, b, &r);
    return r;
}

int64_t __divdi3(int64_t a, int64_t b)
{
    int neg = (a < 0) ^ (b < 0);
    uint64_t q = __udivmoddi4(a < 0 ? -(uint64_t)a : (uint64_t)a, b < 0 ? -(uint64_t)b : (uint64_t)b, NULL);
    return neg ? -(int64_t)q : (int64_t)q;
}

int64_t __moddi3(int64_t a, int64_t b)
{
    uint64_t r;
    __udivmoddi4(a < 0 ? -(uint64_t)a : (uint64_t)a, b < 0 ? -(uint64_t)b : (uint64_t)b, &r);
    return a < 0 ? -(int64_t)r : (int64_t)r;
}

#endif

/* ---- printf ---------------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t cap, len;
} outbuf_t;

static void put(outbuf_t *o, char c)
{
    if (o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void put_num(outbuf_t *o, uint64_t v, int base, bool upper, bool neg, int width, bool zero, bool left)
{
    char tmp[24];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);
    int len = n + (neg ? 1 : 0);
    if (!left && !zero)
        for (int i = len; i < width; i++)
            put(o, ' ');
    if (neg)
        put(o, '-');
    if (!left && zero)
        for (int i = len; i < width; i++)
            put(o, '0');
    while (n)
        put(o, tmp[--n]);
    if (left)
        for (int i = len; i < width; i++)
            put(o, ' ');
}

int kvsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    outbuf_t o = { buf, cap, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        fmt++;
        bool left = false, zero = false;
        int width = 0, prec = -1, lng = 0;
        for (;; fmt++) {
            if (*fmt == '-')
                left = true;
            else if (*fmt == '0')
                zero = true;
            else
                break;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        } else {
            while (kisdigit(*fmt))
                width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (kisdigit(*fmt))
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }
        if (*fmt == 'z') {
            lng = sizeof(size_t) == 8 ? 2 : 0;
            fmt++;
        } else if (lng == 1 && sizeof(long) == 8) {
            lng = 2;
        }
        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t v = lng >= 2 ? va_arg(ap, int64_t) : va_arg(ap, int);
            put_num(&o, v < 0 ? -(uint64_t)v : (uint64_t)v, 10, false, v < 0, width, zero, left);
            break;
        }
        case 'u':
            put_num(&o, lng >= 2 ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10, false, false, width, zero, left);
            break;
        case 'x':
        case 'X':
            put_num(&o, lng >= 2 ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16, *fmt == 'X', false, width, zero, left);
            break;
        case 'p':
            put(&o, '0');
            put(&o, 'x');
            put_num(&o, (uintptr_t)va_arg(ap, void *), 16, false, false, 8, true, false);
            break;
        case 'c':
            put(&o, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int len = (int)strlen(s);
            if (prec >= 0 && len > prec)
                len = prec;
            if (!left)
                for (int i = len; i < width; i++)
                    put(&o, ' ');
            for (int i = 0; i < len; i++)
                put(&o, s[i]);
            if (left)
                for (int i = len; i < width; i++)
                    put(&o, ' ');
            break;
        }
        case '%':
            put(&o, '%');
            break;
        case 0:
            fmt--;
            break;
        default:
            put(&o, '%');
            put(&o, *fmt);
        }
    }
    if (cap)
        buf[o.len < cap ? o.len : cap - 1] = 0;
    return (int)o.len;
}

int ksnprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

#ifndef KEPLER_HOSTED
void kprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    serial_write(buf);
    vga_text_write(buf);
}

void panic(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    cpu_cli();
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    serial_write("\n*** KERNEL PANIC: ");
    serial_write(buf);
    serial_write("\n");
    extern void gui_panic(const char *msg);
    gui_panic(buf);
    for (;;)
        cpu_hlt();
}

#endif

/* ---- xorshift PRNG --------------------------------------------------------- */

static uint32_t rng_state = 0x2545F491;

void ksrand(uint32_t seed) { rng_state = seed ? seed : 0x2545F491; }

uint32_t krand(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}
