/*
 * Interrupt entry stubs. Every vector pushes (error code, vector number)
 * so the C dispatcher sees one uniform frame.
 */

.section .text

.macro ISR_NOERR n
isr\n:
    push $0
    push $\n
    jmp isr_common
.endm

.macro ISR_ERR n
isr\n:
    push $\n
    jmp isr_common
.endm

/* CPU exceptions: 8, 10-14, 17, 21, 29 and 30 push an error code */
.irp n, 0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,31
ISR_NOERR \n
.endr
.irp n, 8,10,11,12,13,14,17,21,29,30
ISR_ERR \n
.endr

/* remapped PIC lines 32..47 */
.irp n, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
ISR_NOERR \n
.endr

isr_common:
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    cld
    push %esp               /* struct regs * */
    call isr_dispatch
    add $4, %esp
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    add $8, %esp            /* vector + error code */
    iret

.section .data
.align 4
.global isr_stub_table
isr_stub_table:
.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
.long isr\n
.endr

/* lgdt + segment reload, called from gdt_init() */
.section .text
.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $1f
1:  ret
