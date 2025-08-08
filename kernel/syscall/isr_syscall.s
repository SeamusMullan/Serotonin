.global isr_syscall
.extern system_call

# System call interrupt handler
isr_syscall:
    cli
    pushfl
    pushal

    # Save segment registers
    pushl   %gs
    pushl   %fs
    pushl   %es
    pushl   %ds

    # Save the stack pointer
    movl    %esp, %eax
    pushl   %eax
    call    system_call
    addl    $4, %esp

    # Restore segment registers
    popl    %ds
    popl    %es
    popl    %fs
    popl    %gs
    popal
    popfl
    sti
    iret
