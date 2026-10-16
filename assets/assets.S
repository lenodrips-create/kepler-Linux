/* Binary assets linked straight into the kernel image. */

.section .rodata
.balign 16
.global wallpaper_jpg
.global wallpaper_jpg_end
wallpaper_jpg:
    .incbin "assets/stockwallpaper"
wallpaper_jpg_end:

/* the Kepler browser: its app icon and start-page logo */
.balign 16
.global kepler_icon_jpg
.global kepler_icon_jpg_end
kepler_icon_jpg:
    .incbin "assets/kepler-icon.jpg"
kepler_icon_jpg_end:

.balign 16
.global kepler_logo_jpg
.global kepler_logo_jpg_end
kepler_logo_jpg:
    .incbin "assets/kepler-logo.jpg"
kepler_logo_jpg_end:

/* Finder: the Explorer folders icon, 256x256 ARGB (one 32-bit word per pixel) */
.balign 16
.global finder_icon_argb
finder_icon_argb:
    .incbin "assets/finder-icon.bgra"

.section .note.GNU-stack,"",@progbits
