.globl gdt_flush
gdt_flush:
    movl 4(%esp), %eax     # Load argument (gdt_ptr*) into eax
    lgdt (%eax)            # Load GDT using address in eax

    movw $0x10, %ax        # Load data segment selector
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    ljmp $0x08, $reload_cs # Far jump to set CS
reload_cs:
    ret
