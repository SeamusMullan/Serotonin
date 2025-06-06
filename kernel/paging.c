#include <stdint.h>
#include "stdio/stdio.h"
#include "paging.h"

#define PAGE_SIZE    4096
#define PAGE_ENTRIES 1024

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_FLAGS   (PAGE_PRESENT | PAGE_RW)

// Exactly one 4 KiB page directory and two 4 KiB tables:
__attribute__((aligned(PAGE_SIZE)))
page_directory_t page_directory;

__attribute__((aligned(PAGE_SIZE)))
page_table_t first_page_table;

__attribute__((aligned(PAGE_SIZE)))
page_table_t kernel_page_table;

__attribute__((aligned(PAGE_SIZE)))
page_table_t heap_page_table0;
__attribute__((aligned(PAGE_SIZE)))
page_table_t heap_page_table1;
__attribute__((aligned(PAGE_SIZE)))
page_table_t heap_page_table2;
__attribute__((aligned(PAGE_SIZE)))
page_table_t heap_page_table3;

void paging_init(void) {
    uintptr_t addr_pd = (uintptr_t)&page_directory;
    uintptr_t addr_pt1 = (uintptr_t)&first_page_table;
    uintptr_t addr_pt2 = (uintptr_t)&kernel_page_table;

    uintptr_t addr_heap_ptr0 = (uintptr_t)&heap_page_table0;
    uintptr_t addr_heap_ptr1 = (uintptr_t)&heap_page_table1;
    uintptr_t addr_heap_ptr2 = (uintptr_t)&heap_page_table2;
    uintptr_t addr_heap_ptr3 = (uintptr_t)&heap_page_table3;

    // Print where they live (must be < 4 MiB)
    printf("PD @ %p, PT1 @ %p,PT2 (kernel) @ %p\nHEAP_PT0 (kernel) @ %p, HEAP_PT1 (kernel) @ %p,\nHEAP_PT2 (kernel) @ %p, HEAP_PT3 (kernel) @ %p\n",
           (void*)addr_pd, (void*)addr_pt1, (void*)addr_pt2, (void*)addr_heap_ptr0,(void*)addr_heap_ptr1,(void*)addr_heap_ptr2,(void*)addr_heap_ptr3);

    // Build first_page_table: identity map first 4 MiB exactly
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_FLAGS;
    }

    // Build kernel_page_table: map 0xC0000000→0x00200000 … up to 4 MiB
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        kernel_page_table[i] = (KERNEL_PHYS_BASE + i * PAGE_SIZE) | PAGE_FLAGS;
    }

    // Build heap tables: map 0x00800000..0x017FFFFF
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        heap_page_table0[i] = ((KERNEL_HEAP_PHYS +   0 * 0x400000) + i * PAGE_SIZE) | PAGE_FLAGS;
        heap_page_table1[i] = ((KERNEL_HEAP_PHYS +   1 * 0x400000) + i * PAGE_SIZE) | PAGE_FLAGS;
        heap_page_table2[i] = ((KERNEL_HEAP_PHYS +   2 * 0x400000) + i * PAGE_SIZE) | PAGE_FLAGS;
        heap_page_table3[i] = ((KERNEL_HEAP_PHYS +   3 * 0x400000) + i * PAGE_SIZE) | PAGE_FLAGS;
    }


    // Zero out the rest of the page directory
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    // PDE[0] → first_page_table (identity 0…4 MiB)
    page_directory[0] = ((uint32_t)&first_page_table) | PAGE_FLAGS;

    // PDE[768] = 0xC0000000 >> 22 = 768 → kernel_page_table
    page_directory[768] = ((uint32_t)&kernel_page_table) | PAGE_FLAGS;
    page_directory[769]   = ((uint32_t)&heap_page_table0[0])  | PAGE_FLAGS;
    page_directory[770]   = ((uint32_t)&heap_page_table1[0])  | PAGE_FLAGS;
    page_directory[771]   = ((uint32_t)&heap_page_table2[0])  | PAGE_FLAGS;
    page_directory[772]   = ((uint32_t)&heap_page_table3[0])  | PAGE_FLAGS;

    asm volatile (
        "mov %0, %%cr3    \n\t"  // Load page directory base
        "mov %%cr0, %%eax \n\t"  // Read CR0
        "or  $0x80000000, %%eax \n\t" // Set PG (paging) bit
        "mov %%eax, %%cr0 \n\t"  // Write CR0 back
        :
        : "r"(&page_directory)
        : "eax", "memory"
    );

    // If we reach here, paging is now on. Print back CR3 and CR0:
    uint32_t check_cr3, check_cr0;
    asm volatile ("mov %%cr3, %0" : "=r"(check_cr3));
    asm volatile ("mov %%cr0, %0" : "=r"(check_cr0));
    printf("CR3 loaded with 0x%08x\n", check_cr3);
    printf("CR0 now = 0x%08x\n", check_cr0);
}
