/*
 * KeplerOS entry point.
 *
 * The kernel is a Multiboot (v1) ELF, so QEMU's -kernel flag and GRUB can
 * both load it. The loader drops us in 32-bit protected mode with paging
 * off; all we do here is set up a stack and hand over to kmain().
 */

.set MB_MAGIC,    0x1BADB002
.set MB_ALIGN,    1 << 0          /* page-align modules */
.set MB_MEMINFO,  1 << 1          /* we want mem_lower/mem_upper and the map */
.set MB_VIDEO,    1 << 2          /* ask GRUB for a linear framebuffer */
.set MB_FLAGS,    MB_ALIGN | MB_MEMINFO | MB_VIDEO
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM
/* a.out kludge fields (unused because bit 16 is clear) */
.long 0, 0, 0, 0, 0
/* preferred video mode: linear, 1280x800, 32 bpp */
.long 0
.long 1280
.long 800
.long 32

.section .bss
.align 16
stack_bottom:
.skip 128 * 1024
.global stack_top
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli
    mov $stack_top, %esp
    xor %ebp, %ebp

    /* x87 FPU: clear EM/TS, set MP and NE, then reset it */
    mov %cr0, %ecx
    and $~((1 << 2) | (1 << 3)), %ecx
    or $((1 << 1) | (1 << 5)), %ecx
    mov %ecx, %cr0
    fninit

    push %ebx               /* multiboot info pointer */
    push %eax               /* multiboot magic */
    call kmain

.Lhang:
    cli
    hlt
    jmp .Lhang
.size _start, . - _start
