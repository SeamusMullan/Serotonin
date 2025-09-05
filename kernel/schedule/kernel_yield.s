.section .text
.global kernel_yield
.type   switch_task, @function
.extern current_task

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

kernel_yield:
    cli

    # save return address
    movl 0(%esp), %eax

    # save callee saved registers and stack pointer
    movl %esp, %ecx          # ecx = esp
    movl %ebx, %edx          # edx = ebx
    movl %ebp, %ebx          # ebx = ebp
    movl %esi, %ebp          # ebp = esi
    movl %edi, %esi          # esi = edi

    movl current_task, %edi  # edi = current_task

    # save ESP, EIP and registers into PCB
    movl %ecx, OFF_ESP(%edi)
    movl %eax, OFF_ENTRY(%edi)
    movl %edx, OFF_K_EBX(%edi)
    movl %ebx, OFF_K_EBP(%edi)
    movl %ebp, OFF_K_ESI(%edi)
    movl %esi, OFF_K_EDI(%edi)

    # save eflags
    pushfl
    popl %eax
    movl %eax, OFF_K_EFLAGS(%edi)

    # save FPU state
    fxsave OFF_K_FPU(%edi)

    sti
    call task_yield
