#ifndef _KERNEL_VMM
#define _KERNEL_VMM

#include <stdint.h>
#include "paging_init.h"
#include "../multiboot.h"

#define PAGE_SIZE        4096
#define PAGE_SHIFT       12
#define MAX_ZONES        8
#define MAX_ORDER        20
#define MIN_MANAGED_PHYS 0x00200000

typedef struct page_desc {
    uint8_t order;
    uint8_t used;
    struct page_desc *next;
} page_desc_t;

typedef struct buddy_zone {
    uint32_t base_phys;
    uint32_t num_pages;
    page_desc_t *pages;
    page_desc_t *free_list[MAX_ORDER+1];
    uint8_t max_order;
} buddy_zone_t;

typedef struct buddy_state {
    buddy_zone_t zones[MAX_ZONES];
    uint32_t zone_count;
} buddy_state_t;

typedef struct range64 {
    uint64_t start, end;
} range64_t;

typedef struct address_space {
    uint32_t phys_pdir; // phys_pdiddy
} address_space_t;

extern buddy_state_t g_buddy;

void buddy_init(multiboot_info_t *mbi, uint32_t kernel_phys_start, uint32_t kernel_phys_end, uint32_t fb_phys_base, uint32_t fb_length);

void *alloc_pages(int order);
void free_pages(void *phys_addr, int order);
uint32_t buddy_total_pages(void);
uint32_t buddy_free_pages(void);

void vmm_init(void);
void map_page(address_space_t *as, uint32_t vaddr, uint32_t paddr, uint32_t flags, int overwrite);
void unmap_page(address_space_t *as, uint32_t vaddr, int free_frame_flag);
uint32_t get_mapping(address_space_t *as, uint32_t vaddr);
uint32_t alloc_map_page(address_space_t *as, uint32_t vaddr, uint32_t flags);

address_space_t *create_address_space(void);
void destroy_address_space(address_space_t *as);
void switch_address_space(address_space_t *as);
vmm_page_table_t *ensure_pt(address_space_t *as, uint32_t pde_index, uint32_t pde_flags);

void *kmap(uint32_t phys);
void kunmap(void);

static inline uint32_t vmm_pdi(uint32_t va) { return (uint32_t)(va >> 22); }
static inline uint32_t vmm_pti(uint32_t va) { return (uint32_t)((va >> 12) & 0x3FF); }

static inline void *alloc_frame(void) { return alloc_pages(0); }
static inline void  free_frame(void *p){ free_pages(p, 0); }

static inline uint32_t align_up(uint32_t v, uint32_t a)   { return (v + (a-1)) & ~(a-1); }
static inline uint64_t align_up64(uint64_t v, uint64_t a) { return (v + (a-1)) & ~(a-1); }
static inline uint64_t align_dn64(uint64_t v, uint64_t a) { return v & ~(a-1); }

static inline int is_power_of_two(uint32_t x) { return x && !(x & (x-1)); }

static inline uint32_t read_cr3(void) {
    uint32_t v; asm volatile ("mov %%cr3,%0":"=r"(v)::"memory");
    return v;
}
static inline void write_cr3(uint32_t v) {
    asm volatile ("mov %0,%%cr3"::"r"(v):"memory");
}
static inline void invlpg(void *va) {
    asm volatile ("invlpg (%0)"::"r"(va):"memory");
}

static inline vmm_page_directory_t *cur_pd_va(void) {
    return (vmm_page_directory_t*)SELF_PD_VA;
}

static inline vmm_page_table_t *pt_va(uint32_t pde_index) {
    return (vmm_page_table_t*)(SELF_PT_BASE_VA + (pde_index * PAGE_SIZE));
}

#endif