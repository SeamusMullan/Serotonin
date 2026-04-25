/*
 * lapic.c
 * Local APIC Serotonin driver
 */

#include "lapic.h"
#include <kernel/cpu/msr.h>
#include <kernel/io/io.h>
#include <kernel/vmm/paging_init.h>
#include <kernel/vmm/vmm.h>
#include <kernel/stdio/stdio.h>
#include <kernel/stdlib/stdlib.h>

volatile int lapic_enabled = 0;

/* this is identity mapped */
volatile uint32_t* lapic_base = (volatile uint32_t*)LAPIC_VIRTUAL_BASE;

uint32_t lapic_get_base() {
    uint32_t eax, edx;
    rdmsr(MSR_IA32_APIC_BASE, &eax, &edx);
    return (uint32_t*)(eax & 0xFFFFF000);
}

inline uint32_t lapic_read(uint32_t reg) {
    return lapic_base[reg / 4];
}

inline uint32_t lapic_write(uint32_t reg, uint32_t val) {
    lapic_base[reg / 4] = val;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

void lapic_init(void) {
    uint32_t lapic_phys_base = lapic_get_base();
    map_page(NULL, lapic_base, lapic_phys_base, PAGE_RW | PAGE_PCD | PAGE_PWT);

    /* enable LAPIC via the SVR */
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | 0x100);

    /* clear error status register */
    lapic_write(LAPIC_ESR, 0);

    /* task priority = 0, accept all interrupts */
    lapic_write(LAPIC_TPR, 0);

    /* mask LINT0 and LINT1 */
    lapic_write(LAPIC_LVT_LINT0, LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LAPIC_TIMER_MASKED);

    /* mask the timer */
    lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_MASKED);

    /* set error vector */
    lapic_write(LAPIC_LVT_ERROR, LAPIC_ERROR_VECTOR);

    lapic_eoi();
    lapic_enabled = 1;
}

void pic_disable(void) {
    outb(0xA1, 0xFF);
    outb(0x21, 0xFF);
}
