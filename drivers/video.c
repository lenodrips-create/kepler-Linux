/*
 * Linear framebuffer setup. Under QEMU/Bochs/VirtualBox we program the
 * Bochs VBE ("BGA") registers directly; if GRUB already set a 32-bit
 * mode for us through Multiboot we use that instead.
 */

#include "video.h"

#define VBE_INDEX 0x01CE
#define VBE_DATA  0x01CF

enum {
    VBE_ID,
    VBE_XRES,
    VBE_YRES,
    VBE_BPP,
    VBE_ENABLE,
    VBE_BANK,
    VBE_VIRT_W,
    VBE_VIRT_H,
    VBE_X_OFF,
    VBE_Y_OFF,
};

screen_t g_screen;

static void vbe_write(uint16_t idx, uint16_t v)
{
    outw(VBE_INDEX, idx);
    outw(VBE_DATA, v);
}

static uint16_t vbe_read(uint16_t idx)
{
    outw(VBE_INDEX, idx);
    return inw(VBE_DATA);
}

static bool bga_init(int w, int h)
{
    uint16_t id = vbe_read(VBE_ID);
    if (id < 0xB0C0 || id > 0xB0CF)
        return false;

    uint8_t bus, slot, func;
    uint32_t lfb = 0xFD000000;
    if (pci_find(0x1234, 0x1111, &bus, &slot, &func) || pci_find(0x80EE, 0xBEEF, &bus, &slot, &func))
        lfb = pci_read(bus, slot, func, 0x10) & 0xFFFFFFF0;

    vbe_write(VBE_ENABLE, 0);
    vbe_write(VBE_XRES, w);
    vbe_write(VBE_YRES, h);
    vbe_write(VBE_BPP, 32);
    vbe_write(VBE_VIRT_W, w);
    vbe_write(VBE_X_OFF, 0);
    vbe_write(VBE_Y_OFF, 0);
    vbe_write(VBE_ENABLE, 0x01 | 0x40); /* enabled, linear framebuffer */
    if (vbe_read(VBE_XRES) != w || vbe_read(VBE_YRES) != h)
        return false;

    g_screen.fb = (uint32_t *)lfb;
    g_screen.pitch = w;
    ksnprintf(g_sys.video, sizeof g_sys.video, "Bochs VBE adapter (BGA %x)", id);
    return true;
}

bool video_init(multiboot_info_t *mbi)
{
    int w = SCREEN_W, h = SCREEN_H;
    bool ok = false;
    if (mbi && (mbi->flags & MBI_FRAMEBUFFER) && mbi->framebuffer_type == 1 && mbi->framebuffer_bpp == 32 &&
        mbi->framebuffer_addr < 0x100000000ull) {
        g_screen.fb = (uint32_t *)(uintptr_t)mbi->framebuffer_addr;
        g_screen.pitch = mbi->framebuffer_pitch / 4;
        w = mbi->framebuffer_width;
        h = mbi->framebuffer_height;
        strlcpy(g_sys.video, "Multiboot linear framebuffer", sizeof g_sys.video);
        ok = true;
    } else {
        ok = bga_init(w, h);
    }
    if (!ok)
        return false;
    g_screen.w = w;
    g_screen.h = h;
    g_screen.back = kmalloc((size_t)w * h * 4);
    if (!g_screen.back)
        return false;
    memset32(g_screen.back, 0xFF000000, (size_t)w * h);
    g_sys.screen_w = w;
    g_sys.screen_h = h;
    vga_text_disable();
    return true;
}

void video_flush(rect_t r)
{
    r = rect_intersect(r, rect_make(0, 0, g_screen.w, g_screen.h));
    if (rect_empty(r))
        return;
    for (int y = r.y; y < r.y + r.h; y++)
        memcpy(&g_screen.fb[y * g_screen.pitch + r.x], &g_screen.back[y * g_screen.w + r.x], r.w * 4);
}

gfx_t video_gfx(void) { return gfx_for_buffer(g_screen.back, g_screen.w, g_screen.h, g_screen.w); }
