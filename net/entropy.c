/* A SHA-256 entropy pool for TLS. There is no hardware RNG to rely on in a
 * plain VM, so it stirs in cycle-counter timings of everything unpredictable:
 * boot-time jitter, key presses and mouse moves, packet arrivals, plus the
 * clock, the MAC address and RDRAND when the CPU has it. */

#include "bearssl.h"
#include "net.h"

static br_sha256_context pool;
static bool ready;
static uint32_t counter;

static bool cpu_has_rdrand(void)
{
    uint32_t a = 1, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return c & (1u << 30);
}

static bool rdrand32(uint32_t *v)
{
    uint8_t ok;
    /* rdrand %eax, spelled out for assemblers that don't know it */
    __asm__ volatile(".byte 0x0f, 0xc7, 0xf0; setc %1" : "=a"(*v), "=qm"(ok));
    return ok;
}

static void start(void)
{
    br_sha256_init(&pool);
    ready = true;
    /* timing jitter of a busy loop, measured with the cycle counter */
    for (int i = 0; i < 256; i++) {
        uint64_t t0 = rdtsc();
        volatile uint32_t x = 0;
        for (int k = 0; k < 50 + (int)(t0 & 31); k++)
            x += k;
        uint64_t t1 = rdtsc();
        uint32_t d = (uint32_t)(t1 - t0) ^ (uint32_t)t1;
        br_sha256_update(&pool, &d, sizeof d);
    }
    datetime_t dt;
    rtc_read(&dt);
    br_sha256_update(&pool, &dt, sizeof dt);
    if (cpu_has_rdrand())
        for (int i = 0; i < 16; i++) {
            uint32_t v;
            if (rdrand32(&v))
                br_sha256_update(&pool, &v, sizeof v);
        }
}

void entropy_add(const void *data, int n)
{
    if (!ready)
        start();
    uint64_t t = rdtsc();
    br_sha256_update(&pool, &t, sizeof t);
    br_sha256_update(&pool, data, n);
}

void entropy_get(uint8_t out[32])
{
    if (!ready)
        start();
    uint64_t t = rdtsc();
    uint32_t ms = timer_ms();
    counter++;
    br_sha256_update(&pool, &t, sizeof t);
    br_sha256_update(&pool, &ms, sizeof ms);
    br_sha256_update(&pool, &counter, sizeof counter);
    br_sha256_context snap = pool;
    br_sha256_out(&snap, out);
    /* feed the output back so the next call differs even without new input */
    br_sha256_update(&pool, out, 32);
}
