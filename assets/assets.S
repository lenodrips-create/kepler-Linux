/* Binary assets linked straight into the kernel image. */

.section .rodata
.balign 16
.global wallpaper_jpg
.global wallpaper_jpg_end
wallpaper_jpg:
    .incbin "assets/stockwallpaper"
wallpaper_jpg_end:
