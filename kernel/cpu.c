/* GDT, IDT, the 8259 PICs and the interrupt dispatcher. */

#include "kernel.h"

/* ---- GDT ------------------------------------------------------------------ */

struct gdt_entry {
    uint16_t limit_lo, base_lo;
    uint8_t base_mid, access, gran, base_hi;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr gdtp;
extern void gdt_flush(struct gdt_ptr *p);

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[i].base_lo = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].base_hi = (base >> 24) & 0xFF;
    gdt[i].limit_lo = limit & 0xFFFF;
    gdt[i].gran = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access = access;
}

void gdt_init(void)
{
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* ring 0 code */
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* ring 0 data */
    gdtp.limit = sizeof gdt - 1;
    gdtp.base = (uint32_t)&gdt;
    gdt_flush(&gdtp);
}

/* ---- IDT ------------------------------------------------------------------ */

struct idt_entry {
    uint16_t off_lo, sel;
    uint8_t zero, flags;
    uint16_t off_hi;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct gdt_ptr idtp;
extern uint32_t isr_stub_table[48];
static irq_handler_t irq_handlers[16];

static void pic_remap(void)
{
    uint8_t m1 = inb(0x21), m2 = inb(0xA1);
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20); /* master -> vectors 32..39 */
    io_wait();
    outb(0xA1, 0x28); /* slave  -> vectors 40..47 */
    io_wait();
    outb(0x21, 4);
    io_wait();
    outb(0xA1, 2);
    io_wait();
    outb(0x21, 1);
    io_wait();
    outb(0xA1, 1);
    io_wait();
    (void)m1;
    (void)m2;
    outb(0x21, 0xFB); /* everything masked except the cascade */
    outb(0xA1, 0xFF);
}

static void irq_unmask(int irq)
{
    if (irq < 8) {
        outb(0x21, inb(0x21) & ~(1 << irq));
    } else {
        outb(0xA1, inb(0xA1) & ~(1 << (irq - 8)));
        outb(0x21, inb(0x21) & ~(1 << 2));
    }
}

void irq_install(int irq, irq_handler_t h)
{
    irq_handlers[irq] = h;
    irq_unmask(irq);
}

void idt_init(void)
{
    for (int i = 0; i < 48; i++) {
        uint32_t a = isr_stub_table[i];
        idt[i].off_lo = a & 0xFFFF;
        idt[i].off_hi = a >> 16;
        idt[i].sel = 0x08;
        idt[i].zero = 0;
        idt[i].flags = 0x8E; /* present, ring 0, 32-bit interrupt gate */
    }
    idtp.limit = sizeof idt - 1;
    idtp.base = (uint32_t)&idt;
    __asm__ volatile("lidt %0" : : "m"(idtp));
    pic_remap();
}

static const char *exception_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range exceeded",
    "Invalid opcode", "Device not available", "Double fault", "Coprocessor overrun",
    "Invalid TSS", "Segment not present", "Stack fault", "General protection fault",
    "Page fault", "Reserved", "x87 floating point", "Alignment check", "Machine check",
    "SIMD floating point", "Virtualization", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor injection", "VMM communication",
    "Security", "Reserved",
};

void isr_dispatch(struct regs *r)
{
    if (r->int_no < 32) {
        panic("%s (#%u) at EIP=%08x, error=%x\nEAX=%08x EBX=%08x ECX=%08x EDX=%08x\nESI=%08x EDI=%08x EBP=%08x ESP=%08x",
              exception_names[r->int_no], r->int_no, r->eip, r->err_code, r->eax, r->ebx, r->ecx, r->edx, r->esi,
              r->edi, r->ebp, r->esp);
    }
    int irq = r->int_no - 32;
    if (irq_handlers[irq])
        irq_handlers[irq](r);
    if (irq >= 8)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* ---- CPUID ------------------------------------------------------------------ */

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

void cpu_identify(char *vendor, char *brand)
{
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    memcpy(vendor, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = 0;

    brand[0] = 0;
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        uint32_t *w = (uint32_t *)brand;
        for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            cpuid(leaf, &w[0], &w[1], &w[2], &w[3]);
            w += 4;
        }
        brand[48] = 0;
        /* trim leading spaces */
        char *s = brand;
        while (*s == ' ')
            s++;
        memmove(brand, s, strlen(s) + 1);
    }
    if (!brand[0])
        strlcpy(brand, vendor, 49);
}

uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
