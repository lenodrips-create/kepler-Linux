/* 8253/8254 PIT on IRQ0, plus a sampled CPU-usage meter for Task Manager. */

#include "kernel.h"

static volatile uint32_t ticks_ms;
static uint32_t ms_per_tick = 1;

/* idle accounting: the main loop sets cpu_idle before it halts */
volatile int cpu_idle;
static volatile uint32_t idle_ticks, window_ticks;
static volatile uint32_t busy_percent;

static void timer_irq(struct regs *r)
{
    (void)r;
    ticks_ms += ms_per_tick;
    if (cpu_idle)
        idle_ticks++;
    if (++window_ticks >= 500 / ms_per_tick) {
        busy_percent = 100 - (idle_ticks * 100) / window_ticks;
        idle_ticks = 0;
        window_ticks = 0;
    }
}

void timer_init(uint32_t hz)
{
    uint32_t div = 1193182 / hz;
    ms_per_tick = 1000 / hz;
    if (ms_per_tick == 0)
        ms_per_tick = 1;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, div >> 8);
    irq_install(0, timer_irq);
}

uint32_t timer_ms(void) { return ticks_ms; }

void sleep_ms(uint32_t ms)
{
    uint32_t end = ticks_ms + ms;
    while ((int32_t)(end - ticks_ms) > 0)
        cpu_hlt();
}

uint32_t cpu_busy_percent(void) { return busy_percent; }
