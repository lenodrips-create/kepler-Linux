/* Intel PRO/1000 (8254x, "e1000") network card: QEMU's default NIC and one
 * VirtualBox offers. Polled: no interrupts, one ring for each direction. */

#include "net.h"

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_MTA    0x5200
#define REG_RAL    0x5400
#define REG_RAH    0x5404

#define CTRL_ASDE (1u << 5)
#define CTRL_SLU  (1u << 6)
#define CTRL_RST  (1u << 26)

#define RCTL_EN    (1u << 1)
#define RCTL_BAM   (1u << 15)
#define RCTL_SECRC (1u << 26)

#define TCTL_EN  (1u << 1)
#define TCTL_PSP (1u << 3)

#define N_RX 32
#define N_TX 16
#define BUF  2048

typedef struct {
    uint32_t addr_lo, addr_hi;
    uint16_t length, csum;
    uint8_t status, errors;
    uint16_t special;
} __attribute__((packed)) rx_desc_t;

typedef struct {
    uint32_t addr_lo, addr_hi;
    uint16_t length;
    uint8_t cso, cmd, status, css;
    uint16_t special;
} __attribute__((packed)) tx_desc_t;

static volatile uint8_t *mmio;
static volatile rx_desc_t *rx;
static volatile tx_desc_t *tx;
static uint8_t *rx_buf, *tx_buf;
static int rx_cur, tx_cur;
static const char *name;

static const struct {
    uint16_t id;
    const char *name;
} models[] = {
    { 0x100E, "Intel PRO/1000 MT Desktop (82540EM)" },
    { 0x100F, "Intel PRO/1000 MT (82545EM)" },
    { 0x1004, "Intel PRO/1000 T Server (82543GC)" },
    { 0x100C, "Intel PRO/1000 T (82544GC)" },
    { 0x1015, "Intel PRO/1000 MT (82540EM)" },
    { 0x1016, "Intel PRO/1000 MT (82540EP)" },
    { 0x1017, "Intel PRO/1000 MT (82540EP)" },
    { 0x101E, "Intel PRO/1000 MT (82540EP)" },
    { 0x1026, "Intel PRO/1000 (82545GM)" },
    { 0x1076, "Intel PRO/1000 GT (82541GI)" },
};

static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(mmio + reg); }
static void wr(uint32_t reg, uint32_t v) { *(volatile uint32_t *)(mmio + reg) = v; }

/* 16-byte aligned memory that is never freed */
static void *aligned(size_t n)
{
    uintptr_t p = (uintptr_t)kzalloc(n + 16);
    return (void *)((p + 15) & ~(uintptr_t)15);
}

static uint16_t eeprom_word(int addr)
{
    wr(REG_EERD, 1u | (uint32_t)addr << 8);
    for (int i = 0; i < 100000; i++) {
        uint32_t v = rd(REG_EERD);
        if (v & (1u << 4))
            return v >> 16;
    }
    return 0;
}

const char *e1000_name(void) { return name; }

bool e1000_init(uint8_t mac[6])
{
    uint8_t bus = 0, slot = 0, fn = 0;
    bool found = false;
    for (int i = 0; i < ARRAY_LEN(models) && !found; i++)
        if (pci_find(0x8086, models[i].id, &bus, &slot, &fn)) {
            found = true;
            name = models[i].name;
        }
    if (!found)
        return false;
    uint32_t bar0 = pci_read(bus, slot, fn, 0x10);
    if (bar0 & 1)
        return false; /* an I/O-port BAR: not a layout we know */
    mmio = (volatile uint8_t *)(uintptr_t)(bar0 & ~0xFu);
    /* memory decoding and bus mastering (the card DMAs into our rings) */
    pci_write(bus, slot, fn, 0x04, pci_read(bus, slot, fn, 0x04) | (1u << 1) | (1u << 2));

    wr(REG_IMC, 0xFFFFFFFF);
    wr(REG_CTRL, rd(REG_CTRL) | CTRL_RST);
    sleep_ms(5);
    wr(REG_IMC, 0xFFFFFFFF);
    (void)rd(REG_ICR);
    wr(REG_CTRL, (rd(REG_CTRL) | CTRL_SLU | CTRL_ASDE) & ~CTRL_RST);

    uint32_t ral = rd(REG_RAL), rah = rd(REG_RAH);
    if (ral == 0 && (rah & 0xFFFF) == 0) {
        for (int i = 0; i < 3; i++) {
            uint16_t w = eeprom_word(i);
            mac[2 * i] = w & 0xFF;
            mac[2 * i + 1] = w >> 8;
        }
        wr(REG_RAL, mac[0] | mac[1] << 8 | mac[2] << 16 | (uint32_t)mac[3] << 24);
        wr(REG_RAH, mac[4] | mac[5] << 8 | (1u << 31));
    } else {
        for (int i = 0; i < 4; i++)
            mac[i] = ral >> (8 * i);
        mac[4] = rah;
        mac[5] = rah >> 8;
        wr(REG_RAH, rah | (1u << 31));
    }
    for (int i = 0; i < 128; i++)
        wr(REG_MTA + 4 * i, 0);

    rx = aligned(sizeof(rx_desc_t) * N_RX);
    tx = aligned(sizeof(tx_desc_t) * N_TX);
    rx_buf = aligned(BUF * N_RX);
    tx_buf = aligned(BUF * N_TX);
    for (int i = 0; i < N_RX; i++) {
        rx[i].addr_lo = (uint32_t)(uintptr_t)(rx_buf + i * BUF);
        rx[i].status = 0;
    }
    wr(REG_RDBAL, (uint32_t)(uintptr_t)rx);
    wr(REG_RDBAH, 0);
    wr(REG_RDLEN, sizeof(rx_desc_t) * N_RX);
    wr(REG_RDH, 0);
    wr(REG_RDT, N_RX - 1);
    rx_cur = 0;
    wr(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC); /* 2048-byte buffers */

    for (int i = 0; i < N_TX; i++) {
        tx[i].addr_lo = (uint32_t)(uintptr_t)(tx_buf + i * BUF);
        tx[i].status = 1; /* "done": free to use */
    }
    wr(REG_TDBAL, (uint32_t)(uintptr_t)tx);
    wr(REG_TDBAH, 0);
    wr(REG_TDLEN, sizeof(tx_desc_t) * N_TX);
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    tx_cur = 0;
    wr(REG_TCTL, TCTL_EN | TCTL_PSP | (15u << 4) | (64u << 12));
    wr(REG_TIPG, 0x0060200A);
    kprintf("e1000: %s at %p, mac %02x:%02x:%02x:%02x:%02x:%02x\n", name, (void *)mmio, mac[0], mac[1], mac[2], mac[3],
            mac[4], mac[5]);
    return true;
}

bool e1000_link_up(void) { return mmio && (rd(REG_STATUS) & 2); }

bool e1000_send(const void *frame, int len)
{
    if (!mmio || len > BUF)
        return false;
    volatile tx_desc_t *d = &tx[tx_cur];
    if (!(d->status & 1))
        return false; /* ring full */
    memcpy(tx_buf + tx_cur * BUF, frame, len);
    d->length = len;
    d->cmd = 1 | 2 | 8; /* end of packet, insert FCS, report status */
    d->status = 0;
    tx_cur = (tx_cur + 1) % N_TX;
    wr(REG_TDT, tx_cur);
    return true;
}

int e1000_receive(void *buf, int max)
{
    if (!mmio)
        return 0;
    volatile rx_desc_t *d = &rx[rx_cur];
    if (!(d->status & 1))
        return 0;
    int len = d->length;
    bool whole = (d->status & 2) && !d->errors;
    if (len > max)
        len = max;
    if (whole)
        memcpy(buf, rx_buf + rx_cur * BUF, len);
    d->status = 0;
    wr(REG_RDT, rx_cur);
    rx_cur = (rx_cur + 1) % N_RX;
    return whole ? len : 0;
}
