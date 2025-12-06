.section .text
.global switch_task
.type   switch_task, @function
.global switch_task_iret
.type   switch_task_iret, @function
.extern current_task
.extern kernel_panic

# PCB offsets
.equ    OFF_ESP,      4
.equ    OFF_ESP0,     8 
.equ    OFF_CR3,      12
.equ    OFF_ENTRY,    56
.equ    OFF_PRIV,     61
.equ    OFF_CTX,      64
.equ    OFF_K_EBX,    68
.equ    OFF_K_EBP,    72
.equ    OFF_K_ESI,    76
.equ    OFF_K_EDI,    80
.equ    OFF_K_EFLAGS, 84
.equ    OFF_K_FPU,    112

# context offsets
.equ OFF_GS,            0
.equ OFF_FS,            4
.equ OFF_ES,            8
.equ OFF_DS,           12
.equ OFF_EDI,          16
.equ OFF_ESI,          20
.equ OFF_EBP,          24
.equ OFF_ESP_AT_PUSHAL,28
.equ OFF_EBX,          32
.equ OFF_EDX,          36
.equ OFF_ECX,          40
.equ OFF_EAX,          44
.equ OFF_STUB_EFLAGS,  48
.equ OFF_EIP,          52
.equ OFF_CS,           56
.equ OFF_EFLAGS,       60
.equ OFF_ESP_AT_TRAP,  64
.equ OFF_SS,           68

switch_task:
    # PIC EOI
    movb $0x20, %al
    outb %al, $0x20

    # edx = next PCB
    movl    4(%esp), %edx
    testl   %edx, %edx
    jz      .fail

    # user mode switch
    cmpb $3, OFF_PRIV(%edx)
    jz switch_user_mode

    # switch to the new PCB
    movl    %edx, current_task
    movl    OFF_ESP(%edx), %esp
    movl    OFF_CR3(%edx), %eax
    movl    %eax,       %cr3

    # restore FPU state
    fxrstor OFF_K_FPU(%edx)

    # finally jump back to its saved EIP
    pushl   OFF_K_EFLAGS(%edx)
    pushl   $0x08
    pushl   OFF_ENTRY(%edx)

    # restore caller-saved registers
    movl    OFF_K_EBP(%edx), %ebp
    movl    OFF_K_EBX(%edx), %ebx
    movl    OFF_K_EDI(%edx), %edi
    movl    OFF_K_ESI(%edx), %esi

    iret

.fail:
    pushl   $panic_msg
    call    kernel_panic
    hlt

switch_user_mode:
    # switch to the new PCB
    movl    %edx, current_task
    movl    OFF_CTX(%edx), %ecx
    
    # restore FPU state
    fxrstor OFF_K_FPU(%edx)

    # restore data segment regs
    movw    OFF_DS(%ecx), %dx
    movw    %dx,   %ds
    movw    OFF_ES(%ecx), %dx
    movw    %dx,   %es
    movw    OFF_FS(%ecx), %dx
    movw    %dx,   %fs
    movw    OFF_GS(%ecx), %dx
    movw    %dx,   %gs

    # build iret frame
    pushl   OFF_SS(%ecx)
    pushl   OFF_ESP_AT_TRAP(%ecx)
    orl     $0x200,OFF_STUB_EFLAGS(%ecx)
    pushl   OFF_STUB_EFLAGS(%ecx)
    pushl   OFF_CS(%ecx)
    pushl   OFF_EIP(%ecx)

    # restore the general purpose registers
    movl    OFF_EDI(%ecx), %edi
    movl    OFF_ESI(%ecx), %esi
    movl    OFF_EBP(%ecx), %ebp
    movl    OFF_EBX(%ecx), %ebx
    movl    OFF_EDX(%ecx), %edx
    movl    OFF_EAX(%ecx), %eax
    movl    OFF_ECX(%ecx), %ecx

    iret

.section .rodata
panic_msg:
    .asciz  "switch_task: NULL task pointer"
