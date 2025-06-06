#include "paging.h"
#include <stdint.h>
#include "kernel.h"   // for kernel_panic()

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
    uintptr_t pt0_phys   = (uintptr_t)&first_page_table;
    uintptr_t kpt_phys   = (uintptr_t)&kernel_page_table;
    uintptr_t heap0_phys = (uintptr_t)&heap_page_table0;
    uintptr_t heap1_phys = (uintptr_t)&heap_page_table1;
    uintptr_t heap2_phys = (uintptr_t)&heap_page_table2;
    uintptr_t heap3_phys = (uintptr_t)&heap_page_table3;
    uintptr_t fbpt_phys  = (uintptr_t)&fb_page_table;
    page_dir_ptr = pd_phys;

    // 1) Identity‐map first 4 MiB (0 … 0x003FFFFF)
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_FLAGS;
    }

    // 2) Map kernel’s first 4 MiB → virtual 0xC0000000 … 
    //    (iterator i covers one 4 KiB page each)
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        kernel_page_table[i] = (KERNEL_PHYS_BASE + i * PAGE_SIZE) | PAGE_FLAGS;
    }

    // 3) Map exactly 4 MiB worth of framebuffer pages:
    //    from fb_phys_base … fb_phys_base + 0x003FFFFF → to FB_VMA_BASE … (FB_VMA_BASE+4 MiB−1)
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        fb_page_table[i] = (fb_phys_base + i * PAGE_SIZE) | PAGE_FLAGS;
    }

    // 4) Map heap (16 MiB = 4 page tables) at virtual 0xC0400000 … 0xC13FFFFF
    //    Physical heap is KERNEL_HEAP_PHYS (0x00800000) … 0x017FFFFF
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        heap_page_table0[i] = ((KERNEL_HEAP_PHYS + 0 * 0x00400000U) + i * PAGE_SIZE) | PAGE_FLAGS;
        heap_page_table1[i] = ((KERNEL_HEAP_PHYS + 1 * 0x00400000U) + i * PAGE_SIZE) | PAGE_FLAGS;
        heap_page_table2[i] = ((KERNEL_HEAP_PHYS + 2 * 0x00400000U) + i * PAGE_SIZE) | PAGE_FLAGS;
        heap_page_table3[i] = ((KERNEL_HEAP_PHYS + 3 * 0x00400000U) + i * PAGE_SIZE) | PAGE_FLAGS;
    }

    // 5) Zero out entire page directory
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    // 6) Install PDEs:
    //    PDE[0] → first_page_table (identity 0…4 MiB)
    page_directory[0]   = ((uint32_t)&first_page_table) | PAGE_FLAGS;
    //    PDE[10] → fb_page_table  (0x02800000/0x00400000 = 10)
    page_directory[10]  = ((uint32_t)&fb_page_table) | PAGE_FLAGS;
    //    PDE[768] → kernel_page_table  (0xC0000000/0x00400000 = 768)
    page_directory[768] = ((uint32_t)&kernel_page_table) | PAGE_FLAGS;
    //    PDE[769..772] → heap_page_table0..3
    page_directory[769] = ((uint32_t)&heap_page_table0) | PAGE_FLAGS;
    page_directory[770] = ((uint32_t)&heap_page_table1) | PAGE_FLAGS;
    page_directory[771] = ((uint32_t)&heap_page_table2) | PAGE_FLAGS;
    page_directory[772] = ((uint32_t)&heap_page_table3) | PAGE_FLAGS;

    // 7) Load CR3 and enable paging
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
    if (pa >= KERNEL_PHYS_BASE && pa < (KERNEL_PHYS_BASE + 4 * 1024 * 1024U)) {
        return (void *)(KERNEL_VMA_BASE + (pa - KERNEL_PHYS_BASE));
    }

    // Kernel heap (16 MiB):
    if (pa >= KERNEL_HEAP_PHYS && pa < (KERNEL_HEAP_PHYS + KERNEL_HEAP_SIZE)) {
        return (void *)(KERNEL_HEAP_VMA + (pa - KERNEL_HEAP_PHYS));
    }

    kernel_panic("phys_to_virt: attempted to map an unmapped physical address");
    return (void *)0;
}
