.section .text
.extern current_task
.equ    OFF_ESP,   4
.equ    OFF_K_FPU, 112

.global isr0
isr0:
    cli
    pushl $0
    call div_zero_fault_handler
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
1:
	jmp 1b
    sti
    iret

.global isr4
isr4:
    cli
    pushl $4
    call fault_handler
    add $4, %esp
    iret

.global isr5
isr5:
    cli
    pushl $5
    call fault_handler
    add $4, %esp
    iret

.global isr6
isr6:
    cli
    pushl $6
    call fault_handler
    add $4, %esp
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

    mov 44(%esp), %eax
    push %eax
    call gp_fault_handler

    add $4, %esp
    popl %ds
    popa
    iret

.global isr14
isr14:
    cli
    pusha
    pushl %ds

    mov 36(%esp), %eax
    push %eax
    call page_fault_handler

    add $4, %esp
    popl %ds
    popa
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
    pushl $17
    call fault_handler
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
    pushl $19
    call fault_handler
    add $4, %esp
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

.global irq0
irq0:
    pushfl
    pushal
    cli

    pushl   %gs
    pushl   %fs
    pushl   %es
    pushl   %ds

    movl    current_task, %edx
    test    %edx, %edx
    jz      1f
    fxsave  OFF_K_FPU(%edx)

1:

    movl    %esp, %eax
    pushl   %eax
    pushl   $0
    call    irq_handler
    addl    $8,   %esp

    movl    current_task, %edx
    test    %edx, %edx
    jz      2f
    fxrstor OFF_K_FPU(%edx)

2:

    popl    %ds
    popl    %es
    popl    %fs
    popl    %gs
    popal
    #orl $0x200, (%esp)
    popfl

    iret

.global irq1
irq1:
    pushfl
    pushal

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
    jz      1f
    fxsave  OFF_K_FPU(%edx)

1:

    movl    %esp, %eax
    pushl   %eax
    pushl   $1
    call    irq_handler
    addl    $8,   %esp

    movl    current_task, %edx
    test    %edx, %edx
    jz      2f
    fxrstor OFF_K_FPU(%edx)

2:

    popl    %ds
    popl    %es
    popl    %fs
    popl    %gs
    popal
    popfl

    iret

.global irq2
irq2:
    pusha
    pushl $2
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq3
irq3:
    pusha
    pushl $3
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq4
irq4:
    pusha
    pushl $4
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq5
irq5:
    pusha
    pushl $5
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq6
irq6:
    pusha
    pushl $6
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq7
irq7:
    pusha
    pushl $7
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq8
irq8:
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
    jz      1f
    fxsave  OFF_K_FPU(%edx)

1:

    movl    %esp, %eax
    pushl   %eax
    pushl   $8
    call    irq_handler
    addl    $8,   %esp

    movl    current_task, %edx
    test    %edx, %edx
    jz      2f
    fxrstor OFF_K_FPU(%edx)

2:

    popl    %ds
    popl    %es
    popl    %fs
    popl    %gs
    popal
    popfl

    iret

.global irq9
irq9:
    pusha
    pushl $9
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq10
irq10:
    pusha
    pushl $10
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq11
irq11:
    pusha
    pushl $11
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq12
irq12:
    pusha
    pushl $12
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq13
irq13:
    pusha
    pushl $13
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq14
irq14:
    pusha
    pushl $14
    call irq_handler
    add $4, %esp
    popa
    iret

.global irq15
irq15:
    pusha
    pushl $15
    call irq_handler
    add $4, %esp
    popa
    iret
