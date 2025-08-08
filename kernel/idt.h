#ifndef _KERNEL_IDT
#define _KERNEL_IDT

#include <stdint.h>

/**
 * @brief IDT entry structure.
 * Defines the structure of an IDT (Interrupt Descriptor Table) entry.
 */
struct idt_entry {
    uint16_t offset_low;   // Lower 16 bits of ISR address
    uint16_t selector;     // Code segment selector in GDT
    uint8_t  zero;         // Always zero
    uint8_t  type_attr;    // Flags
    uint16_t offset_high;  // Upper 16 bits of ISR address
} __attribute__((packed));

/**
 * @brief IDT pointer structure.
 * Defines the structure of an IDT (Interrupt Descriptor Table) pointer.
 */
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));


void idt_load(struct idt_ptr* idtp);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void init_idt();

#endif