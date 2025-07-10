.global isr_syscall
.extern system_call

isr_syscall:
    cli
    pushfl
    pushal

    pushl   %gs
    pushl   %fs
    pushl   %es
    pushl   %ds

    movl    %esp, %eax
    pushl   %eax
    call    system_call
    addl    $4, %esp

    popl    %ds
    popl    %es
    popl    %fs
    popl    %gs
    popal
    popfl
    sti
    iret
