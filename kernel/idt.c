#include <stdint.h>
#include "idt.h"
#include "stdio/stdio.h"
#include "kernel.h"

#define IDT_ENTRIES 256

struct idt_entry idt[256];

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low = base & 0xFFFF;
    idt[num].selector   = sel;
    idt[num].zero       = 0;
    idt[num].type_attr  = flags;
    idt[num].offset_high = (base >> 16) & 0xFFFF;
}

void idt_load(struct idt_ptr* idtp) {
    asm volatile (
        "lidt (%0)"     // Load IDT pointer from memory
        :
        : "r" (idtp)
        : "memory"
    );
}

extern void isr0(void);

void isr0_handler() {
    printfs(PRINT_STATUS_ERROR,"Divide by zero exception!\n");
    kernel_panic("exception - div by zero");
}

void init_idt() {
    struct idt_ptr idtp;

    idtp.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    idtp.base  = (uint32_t)&idt;

    // Clear IDT entries
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);

    idt_load(&idtp);
}
