.section .text
.extern system_call
.extern current_task
.equ    OFF_K_FPU, 112

# System call interrupt handler
.global isr_syscall
isr_syscall:
    pushfl
    pushal

    # Save segment registers
    pushl   %gs
    pushl   %fs
    pushl   %es
    pushl   %ds

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    # Save FPU if multitasking (should be multitasking here, BIG problem if not)
    movl    current_task, %edx
    test    %edx, %edx
    jz      1f
    fxsave  OFF_K_FPU(%edx)

1:

    # Save the stack pointer
    movl    %esp, %eax
    pushl   %eax
    call    system_call
    addl    $4, %esp

    # Restore FPU if multitasking
    movl    current_task, %edx
    test    %edx, %edx
    jz      2f
    fxrstor OFF_K_FPU(%edx)

2:

    # Restore segment registers
    popl    %ds
    popl    %es
    popl    %fs
    popl    %gs
    popal
    popfl
    iret
