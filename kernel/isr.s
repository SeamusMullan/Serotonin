.section .text
.extern current_task
.equ    OFF_ESP,   4
.equ    OFF_K_FPU, 112

# ============================================================
# CPU Exception Handlers (ISR 0-31)
# ============================================================

.global isr0
isr0:
    cli
    pusha
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds
    mov %esp, %eax
    push %eax
    call div_zero_fault_handler
    add $4, %esp
    popl %ds
    popa
    iret

.global isr1
isr1:
    cli
    pushl $1
    call fault_handler
    add $4, %esp
    iret

.global isr2
isr2:
    cli
    pushl $2
    call fault_handler
    add $4, %esp
    iret

.global isr3
isr3:
    cli
    pusha
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds
    mov %esp, %eax
    push %eax
    call breakpoint_fault_handler
    add $4, %esp
    popl %ds
    popa
    iret

.global isr4
isr4:
    cli
    pusha
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds
    mov %esp, %eax
    push %eax
    call overflow_fault_handler
    add $4, %esp
    popl %ds
    popa
    iret

.global isr5
isr5:
    cli
    pusha
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds
    mov %esp, %eax
    push %eax
    call bound_range_fault_handler
    add $4, %esp
    popl %ds
    popa
    iret

.global isr6
isr6:
    cli
    pusha
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds
    mov %esp, %eax
    push %eax
    call invalid_opcode_handler
    add $4, %esp
    popl %ds
    popa
    iret

.global isr7
isr7:
    cli
    pushl $7
    call fault_handler
    add $4, %esp
    iret

.global isr8
isr8:
    cli
    pushl $8
    call fault_handler
    add $4, %esp
    iret

.global isr9
isr9:
    cli
    pushl $9
    call fault_handler
    add $4, %esp
    iret

.global isr10
isr10:
    cli
    pushl $10
    call fault_handler
    add $4, %esp
    iret

.global isr11
isr11:
    cli
    pushl $11
    call fault_handler
    add $4, %esp
    iret

.global isr12
isr12:
    cli
    pushl $12
    call fault_handler
    add $4, %esp
    iret

.global isr13
isr13:
    cli
    pusha
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds

    mov %esp, %eax
    push %eax
    call gp_fault_handler

    add $4, %esp
    popl %ds
    popa
    add $4, %esp
    iret

.global isr14
isr14:
    cli
    pusha
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds

    mov %esp, %eax
    push %eax
    call page_fault_handler

    add $4, %esp
    popl %ds
    popa
    add $4, %esp
    iret

.global isr15
isr15:
    cli
    pushl $15
    call fault_handler
    add $4, %esp
    iret

.global isr16
isr16:
    cli
    pushl $16
    call fault_handler
    add $4, %esp
    iret

.global isr17
isr17:
    cli
    pusha
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds
    mov %esp, %eax
    push %eax
    call alignment_check_fault_handler
    add $4, %esp
    popl %ds
    popa
    add $4, %esp
    iret

.global isr18
isr18:
    cli
    pushl $18
    call fault_handler
    add $4, %esp
    iret

.global isr19
isr19:
    cli
    pusha
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds
    mov %esp, %eax
    push %eax
    call simd_fp_exception_handler
    add $4, %esp
    popl %ds
    popa
    iret

.global isr20
isr20:
    cli
    pushl $20
    call fault_handler
    add $4, %esp
    iret

.global isr21
isr21:
    cli
    pushl $21
    call fault_handler
    add $4, %esp
    iret

.global isr22
isr22:
    cli
    pushl $22
    call fault_handler
    add $4, %esp
    iret

.global isr23
isr23:
    cli
    pushl $23
    call fault_handler
    add $4, %esp
    iret

.global isr24
isr24:
    cli
    pushl $24
    call fault_handler
    add $4, %esp
    iret

.global isr25
isr25:
    cli
    pushl $25
    call fault_handler
    add $4, %esp
    iret

.global isr26
isr26:
    cli
    pushl $26
    call fault_handler
    add $4, %esp
    iret

.global isr27
isr27:
    cli
    pushl $27
    call fault_handler
    add $4, %esp
    iret

.global isr28
isr28:
    cli
    pushl $28
    call fault_handler
    add $4, %esp
    iret

.global isr29
isr29:
    cli
    pushl $29
    call fault_handler
    add $4, %esp
    iret

.global isr30
isr30:
    cli
    pushl $30
    call fault_handler
    add $4, %esp
    iret

.global isr31
isr31:
    cli
    pushl $31
    call fault_handler
    add $4, %esp
    iret

.macro IRQ_HANDLER num
.global irq\num
irq\num:
    pushfl
    pushal
    cli

    pushl   %gs
    pushl   %fs
    pushl   %es
    pushl   %ds

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    movl    current_task, %edx
    test    %edx, %edx
    jz      .Lirq\num\()_skip_save
    fxsave  OFF_K_FPU(%edx)
.Lirq\num\()_skip_save:

    movl    %esp, %eax
    pushl   %eax
    pushl   $\num
    call    irq_handler
    addl    $8,   %esp

    movl    current_task, %edx
    test    %edx, %edx
    jz      .Lirq\num\()_skip_restore
    fxrstor OFF_K_FPU(%edx)
.Lirq\num\()_skip_restore:

    popl    %ds
    popl    %es
    popl    %fs
    popl    %gs
    popal
    popfl

    iret
.endm

IRQ_HANDLER 0
IRQ_HANDLER 1
IRQ_HANDLER 2
IRQ_HANDLER 3
IRQ_HANDLER 4
IRQ_HANDLER 5
IRQ_HANDLER 6
IRQ_HANDLER 7
IRQ_HANDLER 8
IRQ_HANDLER 9
IRQ_HANDLER 10
IRQ_HANDLER 11
IRQ_HANDLER 12
IRQ_HANDLER 13
IRQ_HANDLER 14
IRQ_HANDLER 15
