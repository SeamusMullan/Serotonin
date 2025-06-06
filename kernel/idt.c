#include <stdint.h>
#include "idt.h"
#include "stdio/stdio.h"
#include "kernel.h"

#define IDT_ENTRIES 256

struct idt_entry idt[256];

// lol
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

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

// Division by zero exception interrupt handler
void isr0_handler() {
    printfs(PRINT_STATUS_ERROR,"Divide by zero exception!\n");

    // For now just panic as we shouldn't be dividing by zero in kmode, will handle better in userland.
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

    void (*isrs[32])() = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };

    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint32_t)isrs[i], 0x08, 0x8E);
    }


    idt_load(&idtp);
}
