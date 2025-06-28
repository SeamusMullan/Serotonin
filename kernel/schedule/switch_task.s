.section .text
.global switch_task
.type   switch_task, @function
.global switch_task_iret
.type   switch_task_iret, @function
.extern current_task
.extern kernel_panic
.extern sys_tss

# PCB offsets
.equ    OFF_ESP,    4
.equ    OFF_ESP0,   8 
.equ    OFF_CR3,   12
.equ    OFF_ENTRY, 56
.equ    OFF_PRIV,  61

switch_task:
    # edx = next PCB
    movl    4(%esp), %edx
    testl   %edx, %edx
    jz      .fail

    # save callee-saved in this task
    pushl   %ebx
    pushl   %esi
    pushl   %edi
    pushl   %ebp

    # store that stack into the PCB
    movl    current_task, %ecx
    movl    %esp, OFF_ESP(%ecx)
    movl    OFF_ESP0(%ecx), %ebx
    movl    %ebx, sys_tss+4 # sys_tss.esp0

    # switch to the new PCB
    movl    %edx, current_task
    movl    OFF_ESP(%edx), %esp
    movl    OFF_CR3(%edx), %eax
    movl    %eax,       %cr3

    # restore callee-saved regs
    popl    %ebp
    popl    %edi
    popl    %esi
    popl    %ebx

    # user mode switch
    cmpb $3, OFF_PRIV(%edx)
    jz .switch_user_mode

    # finally jump back to its saved EIP
    pushl   OFF_ENTRY(%edx)
    ret

switch_task_iret:
    # edx = next PCB
    movl    4(%esp), %edx
    testl   %edx, %edx
    jz      .fail

    # save callee-saved in this task
    pushl   %ebx
    pushl   %esi
    pushl   %edi
    pushl   %ebp

    # store that stack into the PCB
    movl    current_task, %ecx
    movl    %esp, OFF_ESP(%ecx)
    movl    OFF_ESP0(%ecx), %ebx
    movl    %ebx, sys_tss+4 # sys_tss.esp0

    # switch to the new PCB
    movl    %edx, current_task
    movl    OFF_ESP(%edx), %esp
    movl    OFF_CR3(%edx), %eax
    movl    %eax,       %cr3

    # restore callee-saved regs
    popl    %ebp
    popl    %edi
    popl    %esi
    popl    %ebx

    movb $0x20, %al
    outb %al, $0x20

    # user mode switch
    cmpb $3, OFF_PRIV(%edx)
    jz .switch_user_mode

    # finally jump back to its saved EIP
    pushf
    push    $0x08
    pushl   OFF_ENTRY(%edx)
    iret

.fail:
    pushl   $panic_msg
    call    kernel_panic
    hlt

.switch_user_mode:
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    pushl $0x23
    pushl %esp
    pushf
    pushl $0x1B
    pushl OFF_ENTRY(%edx)
    iret

.section .rodata
panic_msg:
    .asciz  "switch_task: NULL task pointer"
