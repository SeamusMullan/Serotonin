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

void paging_init(void);