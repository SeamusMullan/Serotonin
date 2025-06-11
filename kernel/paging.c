#include "paging.h"
#include <stdint.h>
#include "kernel.h"   // for kernel_panic()

__attribute__((aligned(PAGE_SIZE)))
page_directory_t page_directory;

__attribute__((aligned(PAGE_SIZE)))
page_table_t first_page_table;

__attribute__((aligned(PAGE_SIZE)))
page_table_t kernel_page_tables[64];

__attribute__((aligned(PAGE_SIZE)))
page_table_t heap_page_tables[64];

__attribute__((aligned(PAGE_SIZE)))
page_table_t fb_page_table;

uintptr_t page_dir_ptr;
uintptr_t fb_addr_ptr;  // stores the _physical_ framebuffer base

/**
 * @brief Initialize the paging system.
 *
 * This function sets up the initial page tables and directory for the kernel's
 * virtual memory management.
 *
 * @param fb_phys_base The physical address of the framebuffer.
 */
void paging_init(uintptr_t fb_phys_base) {
    fb_addr_ptr = fb_phys_base;

    uintptr_t pd_phys    = (uintptr_t)&page_directory;
    page_dir_ptr = pd_phys;

    // 1) Zero out entire page directory
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    // 2) Identity‐map first 4 MiB (0 … 0x003FFFFF)
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_FLAGS;
    }

    // 3) Map kernel space: 256 MB → 64 page tables → PDE[768..831]
    for (uint32_t pd_idx = 0; pd_idx < 64; pd_idx++) {
        for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
            kernel_page_tables[pd_idx][i] = (KERNEL_PHYS_BASE + (pd_idx * 0x400000) + i * PAGE_SIZE) | PAGE_FLAGS;
        }

        page_directory[768 + pd_idx] = ((uint32_t)&kernel_page_tables[pd_idx]) | PAGE_FLAGS;
    }

    // 4) Map exactly 4 MiB worth of framebuffer pages:
    //    from fb_phys_base … fb_phys_base + 0x003FFFFF → to FB_VMA_BASE … (FB_VMA_BASE+4 MiB−1)
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        fb_page_table[i] = (fb_phys_base + i * PAGE_SIZE) | PAGE_FLAGS;
    }

    // Map heap space: 256 MB → 64 page tables → PDE[832..895]
    for (uint32_t pd_idx = 0; pd_idx < 64; pd_idx++) {
        for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
            heap_page_tables[pd_idx][i] = (KERNEL_HEAP_PHYS + (pd_idx * 0x400000) + i * PAGE_SIZE) | PAGE_FLAGS;
        }

        page_directory[832 + pd_idx] = ((uint32_t)&heap_page_tables[pd_idx]) | PAGE_FLAGS;
    }

    // 5) Install PDEs:
    //    PDE[0] → first_page_table (identity 0…4 MiB)
    page_directory[0]   = ((uint32_t)&first_page_table) | PAGE_FLAGS;
    //    PDE[10] → fb_page_table  (0x02800000/0x00400000 = 10)
    page_directory[10]  = ((uint32_t)&fb_page_table) | PAGE_FLAGS;

    // 6) Load CR3 and enable paging
    asm volatile (
        "mov %0, %%cr3      \n\t"
        "mov %%cr0, %%eax   \n\t"
        "or  $0x80000000, %%eax  \n\t" // set PG = 1
        "mov %%eax, %%cr0   \n\t"
        : 
        : "r"(pd_phys) 
        : "eax", "memory"
    );
}

/**
 * @brief Convert a physical address to a virtual address.
 * 
 * This function maps physical addresses to their corresponding
 * virtual addresses based on the kernel's paging setup.
 * 
 * @param pa The physical address to convert.
 * @return void* The corresponding virtual address.
 */
void *phys_to_virt(uintptr_t pa) {
    // 0…4 MiB (identity)
    if (pa < 0x00400000U) {
        return (void *)pa;
    }

    // 4 MiB…8 MiB?  (not used here) 
    // …

    // Framebuffer region (one 4 MiB block):
    if (pa >= fb_addr_ptr && pa < (fb_addr_ptr + 4 * 1024 * 1024U)) {
        return (void *)(FB_VMA_BASE + (pa - fb_addr_ptr));
    }

    // Kernel higher half (first 4 MiB of kernel text/data):
    if (pa >= KERNEL_PHYS_BASE && pa < (KERNEL_PHYS_BASE + (256 * 1024 * 1024U))) {
        return (void *)(KERNEL_VMA_BASE + (pa - KERNEL_PHYS_BASE));
    }

    // Kernel heap (16 MiB):
    if (pa >= KERNEL_HEAP_PHYS && pa < (KERNEL_HEAP_PHYS + (256 * 1024 * 1024U))) {
        return (void *)(KERNEL_HEAP_VMA + (pa - KERNEL_HEAP_PHYS));
    }

    kernel_panic("phys_to_virt: attempted to map an unmapped physical address");
    return (void *)0;
}
