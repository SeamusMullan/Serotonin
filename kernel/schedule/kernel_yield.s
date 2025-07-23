.section .text
.global kernel_yield
.type   switch_task, @function
.extern current_task

# PCB offsets
.equ    OFF_ESP,    4
.equ    OFF_ESP0,   8 
.equ    OFF_CR3,    12
.equ    OFF_ENTRY,  56
.equ    OFF_PRIV,   61
.equ    OFF_CTX,    64
.equ    OFF_K_EBX,  68
.equ    OFF_K_EBP,  72
.equ    OFF_K_ESI,  76
.equ    OFF_K_EDI,  80


kernel_yield:
    # save return address
    call 1f
1:
    pop %eax 

    # save callee saved registers and stack pointer
    mov %esp, %ecx          # ecx = esp
    mov %ebx, %edx          # edx = ebx
    mov %ebp, %ebx          # ebx = ebp
    mov %esi, %ebp          # ebp = esi
    mov %edi, %esi          # esi = edi

    mov current_task, %edi  # edi = current_task

    # save ESP, EIP and registers into PCB
    mov %ecx, OFF_ESP(%edi)
    mov %eax, OFF_ENTRY(%edi)
    mov %edx, OFF_K_EBX(%edi)
    mov %ebx, OFF_K_EBP(%edi)
    mov %ebp, OFF_K_ESI(%edi)
    mov %esi, OFF_K_EDI(%edi)

    call task_yield
