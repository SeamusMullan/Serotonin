#ifndef _KERNEL_PAGING
#define _KERNEL_PAGING
#include <stdint.h>

// if you cannot understand this i dont know why you're even here..
// go consume and further capitalism or something...
#define PAGE_SIZE      4096
#define PAGE_ENTRIES   1024
#define PAGE_SHIFT     12
#define PAGE_MASK      0xFFFFF000
#define PAGE_PRESENT   0x1
#define PAGE_RW        0x2
#define PAGE_USER      0x4
#define PAGE_PWT       0x8
#define PAGE_PCD       0x10
#define PAGE_ACCESSED  0x20
#define PAGE_DIRTY     0x40

#define KERNEL_PDE_BASE  768
#define SELF_PDE_BASE   1023
#define KMAP_PDE_BASE   1022

#define SELF_PD_VA         0xFFFFF000
#define SELF_PT_BASE_VA    0xFFC00000

#define KMAP_BASE (KMAP_PDE_BASE << 22)
#define KMAP_PTE_BASE 0

typedef uint32_t page_table_entry_t;
typedef page_table_entry_t page_table_t[PAGE_ENTRIES];
typedef uint32_t page_directory_entry_t;
typedef page_directory_entry_t page_directory_t[PAGE_ENTRIES];

typedef uint32_t vmm_page_table_t;
typedef uint32_t vmm_page_directory_t;

extern page_directory_t page_directory;

#define KERNEL_PHYS_BASE 0x00200000U   // linked at physical 1 MiB
#define KERNEL_VMA_BASE  0xC0000000U   // where code/data live after paging

#define KERNEL_HEAP_VMA   0xC1000000U   // heap virtual base
#define KERNEL_HEAP_PHYS  0x10200000U   // heap physical base
#define KERNEL_HEAP_SIZE  ((uint32_t)(256 * 1024 * 1024U)) // 256 MB heap
#define KERNEL_PDE_COUNT  ((KERNEL_HEAP_SIZE + 0x3FFFFFU) / 0x400000U)
#define KERNEL_HEAP_PDA   (KERNEL_HEAP_VMA >> 22)   // heap virtual base

#define FB_VMA_BASE      0xE0000000U   // virtual base for the framebuffer mapping
#define FB_MAX_SIZE      (2560U * 1440U * 4U) // max framebuffer size in bytes
#define FB_PDE_COUNT     ((FB_MAX_SIZE + 0x3FFFFFU) / 0x400000U)
#define LAYER_PDE_BASE   ((FB_VMA_BASE >> 22) + FB_PDE_COUNT)
#define LAYER_PDE_COUNT  ((KERNEL_STACK_VMA >> 22) - LAYER_PDE_BASE)

#define USER_SPACE_START 0x00400000U
#define USER_SPACE_END   0xBFFFFFFFU

#define USER_HEAP_START  0x20000000U
#define USER_HEAP_MAX    0x5FFFFFFFU

#define SHMEM_START 0x60000000U
#define SHMEM_END   0x9FFFFFFFU

#define USER_STACK_TOP   0xBF000000U
#define USER_STACK_SIZE  0x00100000U
#define USER_STACK_BOTTOM 0xA0000000U

#define KERNEL_STACK_VMA    0xF0000000U
#define KERNEL_STACK_PHYS   0x30000000U
#define KERNEL_STACK_SIZE   16384
#define KERNEL_STACK_TOP    0xF03FC000U
#define KERNEL_STACK_BOTTOM 0xF0000000U

#define SIGNAL_TRAMPOLINE_ADDR 0xBFF00000

#define PAGE_FLAGS (PAGE_PRESENT | PAGE_RW)
#define USER_PAGE_FLAGS (PAGE_PRESENT | PAGE_RW | PAGE_USER)

#define BLOCK_ALIGN 8
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define ALIGN_UP(x,a)  (((x) + ((a)-1)) & ~((a)-1))


extern uintptr_t page_dir_ptr;
void paging_init(uintptr_t fb_phys_base);

static inline uint32_t virt_to_phys(void *virt) {
    uintptr_t v = (uintptr_t)virt;
    if (v >= KERNEL_HEAP_VMA && v < KERNEL_HEAP_VMA + KERNEL_HEAP_SIZE)
        return (uint32_t)(v - KERNEL_HEAP_VMA + KERNEL_HEAP_PHYS);
    if (v >= KERNEL_VMA_BASE && v < KERNEL_HEAP_VMA)
        return (uint32_t)(v - KERNEL_VMA_BASE + KERNEL_PHYS_BASE);
    return (uint32_t)v;
}

static inline void *phys_to_virt(uintptr_t phys) {
    if (phys >= KERNEL_HEAP_PHYS && phys < KERNEL_HEAP_PHYS + KERNEL_HEAP_SIZE)
        return (void *)(phys - KERNEL_HEAP_PHYS + KERNEL_HEAP_VMA);
    if (phys >= KERNEL_PHYS_BASE && phys < KERNEL_HEAP_PHYS)
        return (void *)(phys - KERNEL_PHYS_BASE + KERNEL_VMA_BASE);
    return (void *)phys;
}

#endif
