/* Small hardware helpers: CMOS clock, PCI config space, power control. */

#include "kernel.h"

/* ---- RTC -------------------------------------------------------------------- */

static uint8_t cmos(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static int bcd(uint8_t v, bool is_bcd) { return is_bcd ? (v & 0x0F) + (v >> 4) * 10 : v; }

void rtc_read(datetime_t *dt)
{
    while (cmos(0x0A) & 0x80) /* update in progress */
        ;
    uint8_t s = cmos(0x00), mi = cmos(0x02), h = cmos(0x04);
    uint8_t d = cmos(0x07), mo = cmos(0x08), y = cmos(0x09), cen = cmos(0x32);
    uint8_t b = cmos(0x0B);
    bool is_bcd = !(b & 0x04);
    bool pm = h & 0x80;
    dt->second = bcd(s, is_bcd);
    dt->minute = bcd(mi, is_bcd);
    dt->hour = bcd(h & 0x7F, is_bcd);
    if (!(b & 0x02)) { /* 12 hour clock */
        dt->hour %= 12;
        if (pm)
            dt->hour += 12;
    }
    dt->day = bcd(d, is_bcd);
    dt->month = bcd(mo, is_bcd);
    int century = cen ? bcd(cen, is_bcd) : 20;
    if (century < 19 || century > 30)
        century = 20;
    dt->year = century * 100 + bcd(y, is_bcd);

    /* Sakamoto's day of week, 0 = Sunday */
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int yy = dt->year - (dt->month < 3);
    dt->weekday = (yy + yy / 4 - yy / 100 + yy / 400 + t[(dt->month - 1) % 12] + dt->day) % 7;
}

/* ---- PCI -------------------------------------------------------------------- */

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t value)
{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, value);
}

bool pci_find(uint16_t vendor, uint16_t device, uint8_t *bus, uint8_t *slot, uint8_t *func)
{
    for (int b = 0; b < 256; b++)
        for (int s = 0; s < 32; s++)
            for (int f = 0; f < 8; f++) {
                uint32_t id = pci_read(b, s, f, 0);
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (f == 0)
                        break;
                    continue;
                }
                if ((id & 0xFFFF) == vendor && (id >> 16) == device) {
                    *bus = b;
                    *slot = s;
                    *func = f;
                    return true;
                }
            }
    return false;
}

static const char *pci_class_name(uint8_t cls, uint8_t sub)
{
    switch (cls) {
    case 0x01: return sub == 0x01 ? "IDE controller" : "Storage controller";
    case 0x02: return "Network controller";
    case 0x03: return "Display adapter";
    case 0x04: return "Multimedia device";
    case 0x06:
        switch (sub) {
        case 0x00: return "Host bridge";
        case 0x01: return "ISA bridge";
        case 0x80: return "System bridge";
        default: return "Bridge";
        }
    case 0x0C: return "Serial bus controller";
    default: return "Device";
    }
}

int pci_list(char lines[][64], int max)
{
    int n = 0;
    for (int b = 0; b < 8 && n < max; b++)
        for (int s = 0; s < 32 && n < max; s++)
            for (int f = 0; f < 8 && n < max; f++) {
                uint32_t id = pci_read(b, s, f, 0);
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (f == 0)
                        break;
                    continue;
                }
                uint32_t cls = pci_read(b, s, f, 8);
                ksnprintf(lines[n++], 64, "%02x:%02x.%u  %04x:%04x  %s", b, s, f, id & 0xFFFF, id >> 16,
                          pci_class_name(cls >> 24, (cls >> 16) & 0xFF));
            }
    return n;
}

/* ---- power ---------------------------------------------------------------- */

void power_off(void)
{
    cpu_cli();
    outw(0x604, 0x2000);  /* QEMU (PIIX4 PM) */
    outw(0xB004, 0x2000); /* Bochs, older QEMU */
    outw(0x4004, 0x3400); /* VirtualBox */
    for (;;)
        cpu_hlt();
}

void power_reboot(void)
{
    cpu_cli();
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
        ;
    outb(0x64, 0xFE); /* pulse the CPU reset line */
    /* fall back to a triple fault */
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile("lidt %0; int $3" : : "m"(null_idt));
    for (;;)
        cpu_hlt();
}
