.global isr_syscall
.extern system_call

isr_syscall:
    cli
    pusha                # push all general-purpose registers
    push %ds
    push %es
    push %fs
    push %gs

    mov $0x10, %ax       # kernel data segment
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    call system_call

    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    sti
    iret