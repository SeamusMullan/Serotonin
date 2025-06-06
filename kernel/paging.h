#ifndef _KERNEL_PAGING
#define _KERNEL_PAGING
#include <stdint.h>

#define PAGE_SIZE      4096
#define PAGE_ENTRIES   1024
#define PAGE_PRESENT   0x1
#define PAGE_RW        0x2
#define PAGE_USER      0x4

typedef uint32_t page_table_entry_t;
typedef page_table_entry_t page_table_t[PAGE_ENTRIES];

typedef uint32_t page_directory_entry_t;
typedef page_directory_entry_t page_directory_t[PAGE_ENTRIES];

#define KERNEL_PHYS_BASE 0x00200000U  // kernel was linked at physical 2 MiB
#define KERNEL_VMA_BASE  0xC0000000U  // where we expect to run after paging
#define IDENTITY_MAP_MB    8 
#define PAGE_FLAGS         (PAGE_PRESENT | PAGE_RW)

#define KERNEL_HEAP_VMA     0xC0400000U
#define KERNEL_HEAP_PHYS    0x00800000U
#define KERNEL_HEAP_SIZE    (16 * 1024 * 1024U)
#define HEAP_NUM_TABLES     (KERNEL_HEAP_SIZE / (4 * 1024 * 1024))
#define BLOCK_ALIGN 8

#define PAGE_SIZE    4096
#define PAGE_ENTRIES 1024

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_FLAGS   (PAGE_PRESENT | PAGE_RW)

#define TEST_PAGE_PHYS    PAGE_SIZE*8

extern uintptr_t page_dir_ptr;

void paging_init(void);
void *phys_to_virt(uintptr_t phys_addr);

#endif