.section .text

.global isr0
isr0:
    cli
    pushl $0
    call fault_handler
    add $4, %esp
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
    pushl $3
    call fault_handler
    add $4, %esp
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
    pushl $13
    call fault_handler
    add $4, %esp
    iret

.global isr14
isr14:
    cli
    pushl $14
    call fault_handler
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
