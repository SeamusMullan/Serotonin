#ifndef _KERNEL_VMM
#define _KERNEL_VMM

#include <stdint.h>
#include "../multiboot.h"

#define PAGE_SIZE        4096
#define PAGE_SHIFT       12
#define MAX_ZONES        8
#define MAX_ORDER        20
#define MIN_MANAGED_PHYS 0x00100000

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

extern buddy_state_t g_buddy;

void buddy_init(multiboot_info_t *mbi, uint32_t kernel_phys_start, uint32_t kernel_phys_end, uint32_t fb_phys_base, uint32_t fb_length);

void *alloc_pages(int order);
void free_pages(void *phys_addr, int order);

static inline void *alloc_frame(void) { return alloc_pages(0); }
static inline void  free_frame(void *p){ free_pages(p, 0); }

uint32_t buddy_total_pages(void);
uint32_t buddy_free_pages(void);

static inline uint32_t align_up(uint32_t v, uint32_t a)   { return (v + (a-1)) & ~(a-1); }
static inline uint64_t align_up64(uint64_t v, uint64_t a) { return (v + (a-1)) & ~(a-1); }
static inline uint64_t align_dn64(uint64_t v, uint64_t a) { return v & ~(a-1); }

static inline int is_power_of_two(uint32_t x) { return x && !(x & (x-1)); }

#endif