.global isr_syscall
.extern system_call

isr_syscall:
    cli
    pusha                # push all general-purpose registers
    push %ds
    push %es
    push %fs
    push %gs

    pushl %eax
    mov $0x10, %ax       # kernel data segment
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    popl %eax

    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    call system_call
    add $16, %esp

    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    sti
    iret
