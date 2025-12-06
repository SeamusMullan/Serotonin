#include <stdint.h>
#include "kernel.h"
#include "gdt.h"

/**
 * @brief GDT entry structure.
 * Defines the structure of a GDT (Global Descriptor Table) entry.
 */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) __attribute__((section(".identity_data")));

/**
 * @brief GDT pointer structure.
 * Defines the structure of a GDT (Global Descriptor Table) pointer.
 */
struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) __attribute__((section(".identity_data")));

/**
 * @brief GDT entry structure.
 * Defines the structure of a GDT (Global Descriptor Table) entry.
 */
__attribute__((section(".identity_data"))) static struct gdt_entry gdt[6];

/**
 * @brief GDT (Global Descriptor Table) pointer structure.
 * Defines the structure of a GDT (Global Descriptor Table) pointer.
 */
__attribute__((section(".identity_data"))) static struct gdt_ptr gdtp;

/**
 * @brief TSS (Task State Segment) structure.
 * Defines the structure of a TSS (Task State Segment).
 */
__attribute__((section(".identity_data"))) tss_struct sys_tss; 

/**
 * @brief Flush the GDT (Global Descriptor Table).
 * 
 * @param gdtp The pointer to the GDT pointer structure.
 */
extern void gdt_flush(uint32_t);

/**
 * @brief Set a GDT (Global Descriptor Table) entry.
 * 
 * @param num The entry number.
 * @param base The base address of the segment.
 * @param limit The limit (size) of the segment.
 * @param access The access flags for the segment.
 * @param gran The granularity flags for the segment.
 */
__attribute__((section(".identity"))) static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = limit & 0xFFFF;
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access      = access;
}

/**
 * @brief Install the TSS (Task State Segment).
 * 
 */
__attribute__((section(".identity"))) void install_tss() {
    sys_tss.esp0 = KERNEL_ESP;
	sys_tss.ss0 = 0x10;
	sys_tss.iomap = ( unsigned short ) sizeof( tss_struct ); 
}
			
/**
 * @brief Initialize the GDT (Global Descriptor Table).
 * 
 * Sets up the GDT with the appropriate segments for:
 * - Kernel code segment
 * - Kernel data segment
 * - User code segment
 * - User data segment
 *
 * calls install_tss()
 * flushes the GDT.
 */
__attribute__((section(".identity"))) void init_gdt() {
    gdtp.limit = (sizeof(gdt) - 1);
    gdtp.base  = (uint32_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                 // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);  // Code segment
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);  // Data segment
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);  // User mode code segment
	gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);  // User mode data segment

    install_tss();

    unsigned int addr = (unsigned int)&sys_tss; 
	int size = sizeof(tss_struct);
    gdt_set_gate(5,addr,size - 1,0x89,0x40);

    gdt_flush((uint32_t)&gdtp);
}