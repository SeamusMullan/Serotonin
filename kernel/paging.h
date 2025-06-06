#ifndef _KERNEL_PAGING
#define _KERNEL_PAGING
#include <stdint.h>

#define PAGE_SIZE      4096
#define PAGE_ENTRIES   1024
#define PAGE_PRESENT   0x1
#define PAGE_RW        0x2

typedef uint32_t page_table_entry_t;
typedef page_table_entry_t page_table_t[PAGE_ENTRIES];

typedef uint32_t page_directory_entry_t;
typedef page_directory_entry_t page_directory_t[PAGE_ENTRIES];

#define KERNEL_PHYS_BASE 0x00200000U   // linked at physical 2 MiB
#define KERNEL_VMA_BASE  0xC0000000U   // where code/data live after paging

#define KERNEL_HEAP_VMA   0xC0400000U   // virtual base of heap
#define KERNEL_HEAP_PHYS  0x00800000U   // physical base of heap (8 MiB)
#define KERNEL_HEAP_SIZE ((uint32_t)(16 * 1024 * 1024U))  // 16 MiB heap

#define FB_VMA_BASE      0x02800000U   // virtual base for the framebuffer mapping

#define PAGE_FLAGS (PAGE_PRESENT | PAGE_RW)

#define BLOCK_ALIGN 8
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

extern uintptr_t page_dir_ptr;
void paging_init(uintptr_t fb_phys_base);
void *phys_to_virt(uintptr_t phys_addr);

#endif
