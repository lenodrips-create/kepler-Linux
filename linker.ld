ENTRY(_start)

SECTIONS
{
    . = 1M;

    /* the Multiboot header has to sit in the first 8 KiB of the image */
    .multiboot : ALIGN(4) { KEEP(*(.multiboot)) }

    .text : ALIGN(4K) { *(.text .text.*) }
    .rodata : ALIGN(4K) { *(.rodata .rodata.*) }
    .data : ALIGN(4K) { *(.data .data.*) }
    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    . = ALIGN(4K);
    kernel_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
