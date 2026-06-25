#ifndef _KERNEL_VMM
#define _KERNEL_VMM

#include <stdint.h>
#include <stddef.h>
#include <kernel/vmm/paging_init.h>
#include <kernel/multiboot.h>

typedef struct process_control_block process_control_block_t;

#define PAGE_SIZE        4096
#define PAGE_SHIFT       12
#define MAX_ZONES        8
#define MAX_ORDER        20
#define MIN_MANAGED_PHYS 0x00200000
#define MAX_SHM_OBJECTS  256

/**
 * @brief Page descriptor structure
 * 
 * This structure represents a single page descriptor in the buddy memory allocator.
 * It contains information about the page's order, usage status, and the next free page
 * in the list.
 */
typedef struct page_desc {
    uint8_t order;
    uint8_t used;
    struct page_desc *next;
} page_desc_t;

/**
 * @brief Buddy memory zone structure
 * 
 * This structure represents a memory zone managed by the buddy allocator. It contains
 * information about the zone's physical base address, the number of pages it covers,
 * and the free list of available blocks.
 */
typedef struct buddy_zone {
    uint32_t base_phys;
    uint32_t num_pages;
    page_desc_t *pages;
    page_desc_t *free_list[MAX_ORDER+1];
    uint8_t max_order;
} buddy_zone_t;

/**
 * @brief Buddy memory allocator state
 * 
 * This structure represents the state of the buddy memory allocator, including
 * the various memory zones it manages and the current zone count.
 */
typedef struct buddy_state {
    buddy_zone_t zones[MAX_ZONES];
    uint32_t zone_count;
} buddy_state_t;

/**
 * @brief 64-bit range structure
 * 
 * This structure represents a range of 64-bit addresses, with a start and end point.
 */
typedef struct range64 {
    uint64_t start, end;
} range64_t;

typedef struct shm_object {
    uint32_t size;
    uint32_t npages;
    uint32_t *phys_pages;
    uint32_t refcount;
    uint32_t kernel_addr;
} shm_object_t;

typedef struct shmem_map {
    uint32_t start;
    uint32_t size;
    shm_object_t *shm;
    struct shmem_map *next;
} shmem_map_t;

/**
 * @brief Address space structure
 * 
 * This structure represents an address space for a process, including its page directory.
 */
typedef struct address_space {
    uint32_t phys_pdir; // phys_pdiddy
    shmem_map_t *shmem_list;
} address_space_t;

extern buddy_state_t g_buddy;
extern shm_object_t* shm_table[MAX_SHM_OBJECTS];

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

int copy_to_user(address_space_t *as, uint32_t dst, const void *src, size_t len);
int copy_from_user(address_space_t *as, void *dst, uint32_t src, size_t len);

shm_object_t* shm_create(uint32_t size);
uint32_t shm_map(process_control_block_t* pcb, shm_object_t *shm);
void shm_unmap(address_space_t *as, uint32_t vaddr);
int shm_alloc_id(void);

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
