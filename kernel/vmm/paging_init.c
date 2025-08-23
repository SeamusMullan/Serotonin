#include "paging_init.h"
#include <stdint.h>

// Page structures in identity-mapped memory
__attribute__((aligned(PAGE_SIZE), section(".identity_data")))
page_directory_t page_directory;

__attribute__((aligned(PAGE_SIZE), section(".identity_data")))
page_table_t first_page_table;

__attribute__((aligned(PAGE_SIZE), section(".identity_data")))
page_table_t kernel_page_tables[64];

__attribute__((aligned(PAGE_SIZE), section(".identity_data")))
page_table_t heap_page_tables[64];

__attribute__((aligned(PAGE_SIZE), section(".identity_data")))
page_table_t fb_page_table;

__attribute__((aligned(PAGE_SIZE), section(".identity_data")))
page_table_t kernel_stack_page_table;

// Global paging info
uintptr_t page_dir_ptr;
uintptr_t fb_addr_ptr;

/**
 * @brief Create a page table entry.
 * 
 * @param phys The physical address.
 * @param flags The flags for the entry.
 * @return uint32_t The page table entry.
 */
static inline uint32_t mk_entry(uint32_t phys, uint32_t flags)
{
    return (phys & 0xFFFFF000) | flags;   //bits 0-11 must be 0 except legal flags
}

/**
 * @brief Initialize the paging system.
 */
__attribute__((section(".identity")))
void paging_init(uintptr_t fb_phys_base) {
    fb_addr_ptr = fb_phys_base;
    page_dir_ptr = (uintptr_t)&page_directory;

    // Clear page directory
    for (uint32_t i = 0; i < PAGE_ENTRIES; ++i)
        page_directory[i] = 0;

    // Identity map first 4 MiB
    for (uint32_t i = 0; i < PAGE_ENTRIES; ++i)
        first_page_table[i] = mk_entry(i * PAGE_SIZE, PAGE_FLAGS);
    page_directory[0] = mk_entry((uintptr_t)first_page_table, PAGE_FLAGS);

    // Map kernel higher half: 256 MiB via 64 page tables at PDE[768..831]
    for (uint32_t pd_idx = 0; pd_idx < 64; ++pd_idx) {
        for (uint32_t i = 0; i < PAGE_ENTRIES; ++i) {
            kernel_page_tables[pd_idx][i] =
                mk_entry(KERNEL_PHYS_BASE + pd_idx * 0x400000 + i * PAGE_SIZE, PAGE_FLAGS);
        }
        page_directory[768 + pd_idx] =
            mk_entry((uintptr_t)&kernel_page_tables[pd_idx],PAGE_FLAGS);
    }

    // Map framebuffer: 4 MiB at FB_VMA_BASE
    for (uint32_t i = 0; i < PAGE_ENTRIES; ++i)
        fb_page_table[i] = (fb_phys_base + i * PAGE_SIZE) | PAGE_FLAGS;
    page_directory[FB_VMA_BASE >> 22] = ((uintptr_t)&fb_page_table) | PAGE_FLAGS;

    // Map heap: 256 MiB via 64 page tables at PDE[832..895]
    for (uint32_t pd_idx = 0; pd_idx < 64; ++pd_idx) {
        for (uint32_t i = 0; i < PAGE_ENTRIES; ++i) {
            heap_page_tables[pd_idx][i] =
                (KERNEL_HEAP_PHYS + pd_idx * 0x400000 + i * PAGE_SIZE) | PAGE_FLAGS;
        }
        page_directory[832 + pd_idx] =
            ((uintptr_t)&heap_page_tables[pd_idx]) | PAGE_FLAGS;
    }

    // Map 4 MiB at 0xF0000000 for kernel stack
    for (uint32_t i = 0; i < PAGE_ENTRIES; ++i) {
        kernel_stack_page_table[i] = (KERNEL_STACK_PHYS + i * PAGE_SIZE) | PAGE_FLAGS;
    }
    page_directory[KERNEL_STACK_VMA >> 22] = mk_entry((uintptr_t)&kernel_stack_page_table, PAGE_FLAGS);


    // Enable paging
    asm volatile (
        "mov %0, %%cr3\n\t"
        "mov %%cr0, %%eax\n\t"
        "or $0x80000000, %%eax\n\t"
        "mov %%eax, %%cr0"
        :
        : "r"(page_dir_ptr)
        : "eax", "memory"
    );
}