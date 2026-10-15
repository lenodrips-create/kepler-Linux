/* kmain: bring up the machine, then hand over to the desktop. */

#include "input.h"
#include "kernel.h"
#include "multiboot.h"

extern char kernel_end[];

sysinfo_t g_sys;

static void ok(const char *what)
{
    vga_text_color(0x07);
    kprintf("  [");
    vga_text_color(0x0A);
    kprintf(" OK ");
    vga_text_color(0x07);
    kprintf("]  %s\n", what);
}

__attribute__((weak)) void gui_panic(const char *msg)
{
    vga_text_init();
    vga_text_color(0x4F);
    vga_text_write("KeplerOS has stopped.\n\n");
    vga_text_write(msg);
}

__attribute__((weak)) void desktop_main(multiboot_info_t *mbi)
{
    (void)mbi;
    kprintf("\n  Type something:  ");
    for (;;) {
        input_event_t ev;
        while (input_poll(&ev)) {
            if (ev.type == EV_KEY && ev.pressed && ev.key < 128) {
                char s[2] = { (char)ev.key, 0 };
                kprintf("%s", s);
            }
        }
        cpu_hlt();
    }
}

void kmain(uint32_t magic, multiboot_info_t *mbi)
{
    serial_init();
    vga_text_init();
    vga_text_color(0x1F);
    kprintf("                                                                                ");
    kprintf("  KeplerOS %s (build %s)  -  kernel boot log                                    ", KEPLER_VERSION,
            KEPLER_BUILD);
    kprintf("                                                                                ");
    vga_text_color(0x07);
    kprintf("\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not loaded by a Multiboot loader (magic %08x)", magic);
    ok("Multiboot handoff");

    gdt_init();
    ok("Global descriptor table");
    idt_init();
    ok("Interrupt descriptor table, PIC remapped to 0x20");

    uint32_t mem_kb = (mbi->flags & MBI_MEM) ? mbi->mem_upper + 1024 : 64 * 1024;
    g_sys.mem_kb = mem_kb;
    uintptr_t heap_start = (uintptr_t)kernel_end;
    uintptr_t heap_end = (uintptr_t)mem_kb * 1024;
    heap_init(heap_start, heap_end);
    char line[96];
    ksnprintf(line, sizeof line, "Memory: %u MB, heap %u MB at %p", mem_kb / 1024,
              (unsigned)((heap_end - heap_start) >> 20), (void *)heap_start);
    ok(line);

    timer_init(1000);
    ok("PIT timer at 1000 Hz");
    keyboard_init();
    ok("PS/2 keyboard");
    mouse_init();
    ok("PS/2 mouse");
    cpu_sti();

    cpu_identify(g_sys.cpu_vendor, g_sys.cpu_brand);
    uint64_t tsc0 = rdtsc();
    sleep_ms(50);
    g_sys.cpu_mhz = (uint32_t)((rdtsc() - tsc0) / 50000);
    ksnprintf(line, sizeof line, "CPU: %s, ~%u MHz", g_sys.cpu_brand, g_sys.cpu_mhz);
    ok(line);

    datetime_t dt;
    rtc_read(&dt);
    ksrand((uint32_t)rdtsc() ^ (dt.second * 2654435761u));
    ksnprintf(line, sizeof line, "RTC: %04d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute,
              dt.second);
    ok(line);

    desktop_main(mbi);
    panic("desktop_main returned");
}
