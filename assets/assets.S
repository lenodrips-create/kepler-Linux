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

/* dock app icons supplied by the project owner, ARGB, square */
.balign 16
.global terminal_icon_argb
terminal_icon_argb:
    .incbin "assets/terminal-icon.bgra"
.balign 16
.global brady_icon_argb
brady_icon_argb:
    .incbin "assets/brady-icon.bgra"
.balign 16
.global brady_240_argb
brady_240_argb:
    .incbin "assets/brady-240.bgra"
.balign 16
.global welcome_bg_argb
welcome_bg_argb:
    .incbin "assets/welcome-bg.bgra"
.global welcome_icons_a8
welcome_icons_a8:
    .incbin "assets/welcome-icons.a8"
.balign 16
.global user_picture_argb
user_picture_argb:
    .incbin "assets/user-picture.bgra"
.balign 16
.global notepad_icon_argb
notepad_icon_argb:
    .incbin "assets/notepad-icon.bgra"
.balign 16
.global calc_icon_argb
calc_icon_argb:
    .incbin "assets/calc-icon.bgra"
.balign 16
.global activity_icon_argb
activity_icon_argb:
    .incbin "assets/activity-icon.bgra"
.balign 16
.global settings_icon_argb
settings_icon_argb:
    .incbin "assets/settings-icon.bgra"
.balign 16
.global launchpad_icon_argb
launchpad_icon_argb:
    .incbin "assets/launchpad-icon.bgra"
.balign 16
.global trash_icon_argb
trash_icon_argb:
    .incbin "assets/trash-icon.bgra"

.section .note.GNU-stack,"",@progbits
