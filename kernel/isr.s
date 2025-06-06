    .section .text
    .global isr0
    .type isr0, @function

    .extern isr0_handler

isr0:
    cli
    pusha

    call isr0_handler

    popa
    iret
