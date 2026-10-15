/* Temporary bring-up sequence for the graphics stack. */

#include "font.h"
#include "input.h"
#include "jpeg.h"
#include "video.h"

extern const uint8_t wallpaper_jpg[], wallpaper_jpg_end[];

void desktop_main(multiboot_info_t *mbi)
{
    if (!video_init(mbi))
        panic("no usable framebuffer");
    gfx_t g = video_gfx();
    int W = g_screen.w, H = g_screen.h;

    /* stage 1: primitives test card */
    gfx_vgrad(&g, 0, 0, W, H, HEX(0x0B1E3F), HEX(0x2A5CAA));
    static const uint32_t bars[] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };
    for (int i = 0; i < 8; i++)
        gfx_fill(&g, 140 + i * 125, 90, 125, 220, HEX(bars[i]));
    gfx_hgrad(&g, 140, 330, 1000, 40, HEX(0x000000), HEX(0xFFFFFF));
    gfx_hgrad(&g, 140, 380, 1000, 40, HEX(0xFF0000), HEX(0x0000FF));
    for (int i = 0; i < 6; i++)
        gfx_round_rect_vgrad(&g, 140 + i * 170, 450, 150, 90, 4 + i * 6, HEX(0x6FC36F), HEX(0x2F7F2F), CORNER_ALL);
    for (int i = 0; i < 6; i++)
        gfx_circle(&g, 215 + i * 170, 640, 20 + i * 8, HEX(0xF2B51C));
    for (int i = 0; i < 20; i++)
        gfx_line(&g, 140, 760, 140 + i * 50, 720, HEX(0xFFFFFF));
    video_flush(rect_make(0, 0, W, H));
    kprintf("stage: test card\n");
    sleep_ms(4000);

    /* stage 2: decode and scale the wallpaper */
    uint32_t t0 = timer_ms();
    image_t *src;
    if (!jpeg_decode(wallpaper_jpg, wallpaper_jpg_end - wallpaper_jpg, &src))
        panic("wallpaper decode failed");
    uint32_t t1 = timer_ms();
    image_t *wall = image_cover(src, W, H);
    image_sharpen(wall, 90);
    uint32_t t2 = timer_ms();
    kprintf("jpeg %dx%d decoded in %u ms, scaled in %u ms\n", src->w, src->h, t1 - t0, t2 - t1);
    gfx_blit(&g, 0, 0, wall);
    video_flush(rect_make(0, 0, W, H));
    kprintf("stage: wallpaper\n");
    sleep_ms(4000);

    /* stage 3: text */
    gfx_t panel = gfx_sub(&g, 60, 60, 760, 520);
    gfx_round_rect(&panel, 0, 0, 760, 520, 10, RGBA(255, 255, 255, 215), CORNER_ALL);
    int y = 20;
    gfx_text(&panel, &font_logo, 24, y, "KeplerOS", HEX(0x1D4FB8));
    y += 70;
    gfx_text(&panel, &font_big, 26, y, "Antialiased text, rendered by the kernel", HEX(0x222222));
    y += 36;
    const struct {
        const font_t *f;
        const char *name;
    } samples[] = {
        { &font_ui, "UI 11px: The quick brown fox jumps over the lazy dog 0123456789" },
        { &font_ui_bold, "UI Bold 11px: The quick brown fox jumps over the lazy dog" },
        { &font_title, "Title 12px: Notepad - Untitled  |  My Computer  |  Command Prompt" },
        { &font_mono, "Mono 13px: C:\\> dir /w   int main(void) { return 0; }" },
        { &font_heading, "Heading 15px: System Tasks  -  Other Places  -  Details" },
        { &font_big_bold, "Big Bold 22px: Welcome to KeplerOS" },
    };
    for (int i = 0; i < ARRAY_LEN(samples); i++) {
        gfx_text(&panel, samples[i].f, 26, y, samples[i].name, HEX(0x111111));
        y += samples[i].f->height + 14;
    }
    gfx_round_rect_vgrad(&panel, 26, y + 10, 104, 30, 12, HEX(0x5EB65E), HEX(0x2F8A2F), CORNER_ALL);
    gfx_text_shadow(&panel, &font_start, 50, y + 13, "start", HEX(0xFFFFFF), RGBA(0, 0, 0, 120));
    char buf[96];
    ksnprintf(buf, sizeof buf, "Wallpaper: %dx%d JPEG decoded in %u ms, scaled to %dx%d in %u ms", src->w, src->h, t1 - t0, W,
              H, t2 - t1);
    gfx_text(&panel, &font_ui, 150, y + 20, buf, HEX(0x333333));
    video_flush(rect_make(0, 0, W, H));
    kprintf("stage: fonts\n");

    for (;;)
        cpu_hlt();
}
