/*
    vmm.c
    Serotonin Virtual Memory Manager

    Virtual Memory Layout:
        0x00000000 – 0x003FFFFF - Identity map
        0x00400000 – 0x1FFFFFFF - User text/data/bss
        0x20000000 – 0x5FFFFFFF - User heap
        0x60000000 - 0x9FFFFFFF - User shared memory
        0xA0000000 – 0xBEFFFFFF - User stack
        0xBF000000 – 0xBFFFFFFF - USER RESERVED
        0xC0000000 – 0xCFFFFFFF - Kernel
        0xD0000000 – 0xDFFFFFFF - Kernel heap
        0xE0000000 – 0xE03FFFFF - Framebuffer
        0xF0000000 – 0xF03FFFFF - Kernel stack
*/

#include "vmm.h"
#include "paging_init.h"
#include "../stdio/stdio.h"
#include "../stdlib/stdlib.h"
#include "../multiboot.h"
#include "../kernel.h"
#include "../string.h"
#include "../io/io.h"


/**
 * @brief Global buddy memory allocator state
 *
 * This is a very simple buddy allocator implementation. It supports multiple zones,
 * each with its own free list and page descriptors.
 */
buddy_state_t g_buddy = {0};
shm_object_t* shm_table[MAX_SHM_OBJECTS] = {0};
uint32_t kmap_pt_phys = 0;

/**
 * @brief Get the virtual address of the kernel mapping page table
 *
 * @return vmm_page_table_t* Pointer to the kernel mapping page table
 */
static inline vmm_page_table_t *kmap_pt_va(void) { return pt_va(KMAP_PDE_BASE); }

// compute largest order such that:
// (idx % (1<<order)) == 0 (alignment)
// (idx + (1<<order)) <= limit
// order <= zone->max_order

/**
 * @brief Compute the largest order that fits within a given range
 *
 * @param idx The starting index
 * @param limit The ending limit
 * @param max_order The maximum order
 * @return int The largest order that fits, or -1 if none fits
 *
 * This function computes the largest power-of-two block size (order) that can fit
 * starting from the given index `idx` without exceeding the `limit`, while also
 * ensuring that the block is aligned to its size. The order must not exceed `max_order`.
 *
 * Used in buddy memory allocation to determine the largest block of pages that can be
 * allocated from a given starting index within a memory zone.
 */
int largest_order_fit(uint32_t idx, uint32_t limit, uint8_t max_order) {
    uint32_t remain = limit - idx;
    int o = 0;
    // largest power-of-two <= remain
    while (o < (int)max_order && ((1u << (o+1)) <= remain)) o++;
    // fix alignment
    while (o > 0 && (idx & ((1u << o) - 1)) != 0) o--;
    if ((1u << o) > remain) return -1;
    return o;
}

/**
 * @brief Initialize the free list for a memory zone
 *
 * @param z Pointer to the memory zone
 *
 * This function initializes the free list of a buddy memory zone by setting all
 * entries to NULL, indicating that there are no free blocks of any order initially.
 */
void zone_freelist_init(buddy_zone_t *z) {
    for (int i = 0; i <= MAX_ORDER; ++i) z->free_list[i] = NULL;
}

/**
 * @brief Push a free block onto the free list of a memory zone
 *
 * @param z Pointer to the memory zone
 * @param head_idx The index of the head page
 * @param order The order of the block
 *
 * This function adds a free block of a specified order to the free list of the given
 * memory zone. The block is represented by its head page index, and the function
 * updates the page descriptor to mark it as free and sets its order.
 *
 * Used in buddy memory allocation to manage free blocks of pages, mainly during
 * initialization and when freeing pages.
 */
void zone_push_free(buddy_zone_t *z, uint32_t head_idx, int order) {
    page_desc_t *head = &z->pages[head_idx];
    head->order = (uint8_t)order;
    head->used  = 0;
    head->next  = z->free_list[order];
    z->free_list[order] = head;
}

/**
 * @brief Pop a free block from the free list of a memory zone
 *
 * @param z Pointer to the memory zone
 * @param order The order of the block
 * @return page_desc_t* Pointer to the popped block, or NULL if none available
 *
 * This function removes and returns a free block of the specified order from the
 * free list of the given memory zone. If no block of that order is available, it
 * returns NULL.
 *
 * Used in buddy memory allocation to allocate blocks of pages, mainly during
 * allocation requests.
 */
page_desc_t *zone_pop_free(buddy_zone_t *z, int order) {
    page_desc_t *h = z->free_list[order];
    if (!h) return NULL;
    z->free_list[order] = h->next;
    h->next = NULL;
    return h;
}

/**
 * @brief Compute the physical address of a page given its index and order
 *
 * @param idx The index of the page
 * @param order The order of the block
 * @return uint32_t The physical address of the page
 *
 * This function computes the physical address of a page in a buddy memory allocation
 * system given its index and order. The address is calculated by aligning the index
 * to the block size (1 << order) and then shifting it by the page size (PAGE_SHIFT).
 *
 * Used in buddy memory allocation to determine the physical address of allocated
 * pages when returning them to the caller.
 */
static inline uint32_t buddy_index(uint32_t idx, int order) {
    return idx ^ (1u << order);
}

/**
 * @brief Build the initial free list for a memory zone
 *
 * @param z Pointer to the memory zone
 *
 * This function initializes the free list of a buddy memory zone by dividing the
 * zone into the largest possible blocks of pages and adding them to the free list.
 * It also determines the maximum order of blocks that can fit in the zone based on
 * its size.
 */
void zone_build(buddy_zone_t *z) {
    zone_freelist_init(z);

    // determine z->max_order based on zone size
    uint8_t mo = MAX_ORDER;
    while (mo > 0 && ((1u << mo) > z->num_pages)) mo--;
    z->max_order = mo;

    memset(z->pages, 0, sizeof(page_desc_t) * z->num_pages);

    uint32_t idx = 0;
    while (idx < z->num_pages) {
        int o = largest_order_fit(idx, z->num_pages, z->max_order);
        if (o < 0) break;
        zone_push_free(z, idx, o);
        idx += (1u << o);
    }
}

/**
 * @brief Split a block into smaller blocks of a different order
 *
 * @param z Pointer to the memory zone
 * @param blk Pointer to the block to split
 * @param have_order The current order of the block
 * @param want_order The desired order of the block
 * @return page_desc_t* Pointer to the new block, or NULL if failed
 *
 * This function splits a block of memory into two smaller blocks of a different order.
 * It updates the free list and the block descriptors accordingly.
 *
 */
page_desc_t* zone_split_to(buddy_zone_t *z, page_desc_t *blk, int have_order, int want_order) {
    uint32_t idx = (uint32_t)(blk - z->pages);
    int o = have_order;
    while (o > want_order) {
        o--;
        // split into two buddies of order o
        uint32_t right_idx = idx + (1u << o);
        // push right half
        zone_push_free(z, right_idx, o);
        // continue with left half
        // left half remains at idx with implicit order o
    }
    blk = &z->pages[idx];
    blk->order = (uint8_t)want_order;
    return blk;
}

/**
 * @brief Allocate a block of memory pages
 *
 * @param z Pointer to the memory zone
 * @param order The order of the block to allocate
 * @return void* Pointer to the allocated memory, or NULL if failed
 *
 * This function allocates a block of memory pages of the specified order from the
 * given memory zone. It searches the free list for a suitable block, splits it if
 * necessary, marks it as used, and returns the physical address of the allocated block.
 */
void *zone_alloc_pages(buddy_zone_t *z, int order) {
    if (order > z->max_order) return NULL;

    // find first non-empty free-list at >= order
    int o = order;
    page_desc_t *blk = NULL;
    for (; o <= z->max_order; ++o) {
        if ((blk = zone_pop_free(z, o)) != NULL)
            break;
    }
    if (!blk) return NULL;

    // split down if needed
    if (o > order) {
        blk = zone_split_to(z, blk, o, order);
    }

    // mark used
    uint32_t idx = (uint32_t)(blk - z->pages);
    blk->used  = 1;
    blk->order = (uint8_t)order;

    // return physical address of head page
    uint32_t phys = z->base_phys + ((uint32_t)idx << PAGE_SHIFT);
    return (void*)phys;
}

/**
 * @brief Free a block of memory pages
 *
 * @param z Pointer to the memory zone
 * @param phys_addr Physical address of the block to free
 * @param order The order of the block to free
 *
 * This function frees a block of memory pages of the specified order back to the
 * buddy allocator. It marks the block as free and attempts to coalesce it with
 * adjacent free blocks.
 */
void zone_free_pages(buddy_zone_t *z, void *phys_addr, int order) {
    uint32_t phys = (uint32_t)phys_addr;
    if (phys < z->base_phys) return; // not in this zone
    uint32_t off = phys - z->base_phys;
    if (off & (PAGE_SIZE - 1)) return; // not page-aligned
    uint32_t idx = (uint32_t)(off >> PAGE_SHIFT);
    if (idx >= z->num_pages) return;

    page_desc_t *head = &z->pages[idx];
    int o = order;

    // mark free
    head->used  = 0;
    head->order = (uint8_t)o;

    // coalesce upward while buddy of same order is free and within zone
    while (o < z->max_order) {
        uint32_t bidx = buddy_index(idx, o);
        if (bidx >= z->num_pages) break;

        page_desc_t *buddy = &z->pages[bidx];

        // buddy must be free and be a block head of the same order
        if (buddy->used || buddy->order != o) break;

        // remove buddy from free list
        page_desc_t **pp = &z->free_list[o];
        int found = 0;
        while (*pp) {
            if (*pp == buddy) { *pp = buddy->next; buddy->next = NULL; found = 1; break; }
            pp = &((*pp)->next);
        }
        if (!found) break; // fucked, stop coalescing

        // new head is min(idx, bidx), order++
        idx = (bidx < idx) ? bidx : idx;
        head = &z->pages[idx];
        o++;
        head->order = (uint8_t)o;
    }

    // push merged block
    zone_push_free(z, idx, o);
}

/**
 * @brief Find the memory zone containing the given physical address
 *
 * @param phys Physical address to search for
 * @return int Index of the memory zone, or -1 if not found
 *
 * This function searches through the list of memory zones to find the one that
 * contains the given physical address. It returns the index of the zone if found,
 * or -1 if the address is not within any zone.
 */
int find_zone_by_phys(uint32_t phys) {
    for (uint32_t i = 0; i < g_buddy.zone_count; ++i) {
        buddy_zone_t *z = &g_buddy.zones[i];
        if (phys >= z->base_phys &&
            phys <  z->base_phys + ((uint32_t)z->num_pages << PAGE_SHIFT)) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Allocate a block of memory pages
 *
 * @param order The order of the block to allocate
 * @return void* Pointer to the allocated memory block, or NULL on failure
 *
 * This function allocates a block of memory pages of the specified order from the
 * global buddy allocator. It searches through all memory zones to find a suitable
 * block and returns its physical address. If no block is available, it triggers a kernel panic.
 */
void *alloc_pages(int order) {
    for (uint32_t i = 0; i < g_buddy.zone_count; ++i) {
        void *p = zone_alloc_pages(&g_buddy.zones[i], order);
        if (p) return p;
    }
    kernel_panic("out of memory (buddy alloc)");
    __builtin_unreachable();
}

/**
 * @brief Free a block of memory pages
 *
 * @param phys_addr Physical address of the block to free
 * @param order The order of the block to free
 *
 * This function frees a block of memory pages of the specified order back to the
 * buddy allocator. It marks the block as free and attempts to coalesce it with
 * adjacent free blocks.
 */
void free_pages(void *phys_addr, int order) {
    uint32_t phys = (uint32_t)phys_addr;
    int zi = find_zone_by_phys(phys);
    if (zi < 0) return;
    zone_free_pages(&g_buddy.zones[zi], phys_addr, order);
}

/**
 * @brief Get the total number of pages in the buddy allocator
 *
 * @return uint32_t Total number of pages
 *
 * This function computes the total number of pages managed by the buddy allocator
 * by summing the number of pages in each memory zone.
 */
uint32_t buddy_total_pages(void) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < g_buddy.zone_count; ++i)
        sum += g_buddy.zones[i].num_pages;
    return sum;
}

/**
 * @brief Get the total number of free pages in the buddy allocator
 *
 * @return uint32_t Total number of free pages
 *
 * This function computes the total number of free pages available in the buddy
 * allocator by traversing the free lists of all memory zones and summing the sizes
 * of all free blocks.
 */
uint32_t buddy_free_pages(void) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < g_buddy.zone_count; ++i) {
        buddy_zone_t *z = &g_buddy.zones[i];
        for (int o = 0; o <= z->max_order; ++o) {
            for (page_desc_t *p = z->free_list[o]; p; p = p->next)
                sum += (1u << o);
        }
    }
    return sum;
}

/**
 * @brief Add a new memory zone to the buddy allocator
 *
 * @param base The starting physical address of the zone
 * @param end The ending physical address of the zone
 *
 * This function adds a new memory zone to the buddy allocator by aligning the
 * provided base and end addresses to page boundaries, allocating metadata for
 * the zone, and initializing its free list.
 */
void add_zone(uint64_t base, uint64_t end) {
    if (g_buddy.zone_count >= MAX_ZONES) return;
    // align to pages
    base = align_up64(base, PAGE_SIZE);
    end  = align_dn64(end,  PAGE_SIZE);
    if (end <= base) return;

    buddy_zone_t *z = &g_buddy.zones[g_buddy.zone_count];

    z->base_phys = (uint32_t)base;
    z->num_pages = (uint32_t)((end - base) >> PAGE_SHIFT);

    // allocate metadata in kernel heap (virt), page aligned
    z->pages = (page_desc_t*)kernel_malloc_align(16, z->num_pages * sizeof(page_desc_t));
    if (!z->pages)
        kernel_panic("out of memory when allocating buddy metadata");

    zone_build(z);
    g_buddy.zone_count++;
}

// subtract [rsv_start, rsv_end) from [*in_start, *in_end); may emit up to 2 child ranges
/**
 * @brief Carve out a reserved range from an input range
 *
 * @param in_start Start of the input range
 * @param in_end End of the input range
 * @param rsv_start Start of the reserved range
 * @param rsv_end End of the reserved range
 * @param out Output array to hold the resulting ranges
 * @return int Number of resulting ranges
 *
 * This function takes an input range and a reserved range, and carves out the reserved
 * range from the input range, emitting up to two child ranges. It returns the number of resulting
 * ranges (0, 1, or 2).
 */
int carve_exclusion(uint64_t in_start, uint64_t in_end,
                           uint64_t rsv_start, uint64_t rsv_end,
                           range64_t out[2]) {
    // No overlap
    if (rsv_end <= in_start || rsv_start >= in_end) {
        out[0] = (range64_t){ in_start, in_end };
        return 1;
    }
    // Clip
    int n = 0;
    if (rsv_start > in_start)
        out[n++] = (range64_t){ in_start, rsv_start };
    if (rsv_end < in_end)
        out[n++] = (range64_t){ rsv_end, in_end };
    return n;
}

/**
 * @brief Initialize the buddy allocator
 *
 * @param mbi Multiboot information structure
 * @param kernel_phys_start Physical start address of the kernel
 * @param kernel_phys_end Physical end address of the kernel
 * @param fb_phys_base Physical base address of the framebuffer
 * @param fb_length Length of the framebuffer
 *
 * This function initializes the buddy memory allocator by parsing the memory map
 * provided by the bootloader, excluding reserved regions such as the kernel image
 * and framebuffer, and adding the available memory zones to the allocator.
 */
void buddy_init(multiboot_info_t *mbi, uint32_t kernel_phys_start, uint32_t kernel_phys_end, uint32_t fb_phys_base, uint32_t fb_length) {
    memset(&g_buddy, 0, sizeof(g_buddy));

    // Collect exclusions
    range64_t excl[4];
    int excl_n = 0;

    // exclude <1 MiB (identity map)
    excl[excl_n++] = (range64_t){ 0, MIN_MANAGED_PHYS };

    // exclude kernel image (physical)
    if (kernel_phys_end > kernel_phys_start)
        excl[excl_n++] = (range64_t){ kernel_phys_start, kernel_phys_end };

    // exclude framebuffer
    if (fb_length)
        excl[excl_n++] = (range64_t){ fb_phys_base, (uint64_t)fb_phys_base + fb_length };

    uint32_t mmap_len  = mbi->mmap_length;
    uint32_t mmap_addr = mbi->mmap_addr;

    for (uint32_t i = 0; i < mmap_len; ) {
        multiboot_memory_map_t *m = (multiboot_memory_map_t*)(uint32_t)(mmap_addr + i);
        uint64_t base = m->addr;
        uint64_t len  = m->len;
        uint64_t end  = m->addr + m->len;
        i += m->size + sizeof(m->size);

        if (m->type != MULTIBOOT_MEMORY_AVAILABLE || len == 0) continue; // only "available"

        range64_t todo[2] = { {base, end} };
        int todo_n = 1;

        for (int e = 0; e < excl_n; ++e) {
            range64_t next[2];
            int next_n = 0;
            for (int t = 0; t < todo_n; ++t) {
                next_n += carve_exclusion(todo[t].start, todo[t].end, excl[e].start, excl[e].end, &next[next_n]);
            }
            // copy back
            todo[0] = next[0];
            todo[1] = (next_n > 1) ? next[1] : (range64_t){0,0};
            todo_n  = next_n;
            if (todo_n == 0) break;
        }

        for (int t = 0; t < todo_n; ++t) {
            if (todo[t].end > todo[t].start) {
                add_zone(todo[t].start, todo[t].end);
            }
        }
    }
}

/**
 * @brief Map a physical address to a virtual address
 *
 * @param phys Physical address to map
 * @return void* Virtual address mapped to the physical address
 *
 * This function maps a physical address to a predefined virtual address (KMAP_BASE)
 * using a dedicated page table. It updates the page table entry, invalidates the
 * TLB for the mapped address, and returns the virtual address. If the mapping system
 * is not initialized, it triggers a kernel panic.
 */
void *kmap(uint32_t phys) {
    if (!kmap_pt_phys) {
        kernel_panic("kmap: not initialized");
    }
    vmm_page_table_t *pt = kmap_pt_va();
    pt[KMAP_PTE_BASE] = (phys & PAGE_MASK) | PAGE_FLAGS;
    invlpg((void*)KMAP_BASE);
    return (void*)KMAP_BASE;
}

/**
 * @brief Unmap a virtual address
 *
 */
void kunmap(void) {
    vmm_page_table_t *pt = kmap_pt_va();
    pt[KMAP_PTE_BASE] = 0;
    invlpg((void*)KMAP_BASE);
}

/**
 * @brief Initialize the virtual memory manager
 *
 * This function initializes the virtual memory manager by setting up the self-mapping
 * and kernel mapping page tables. It ensures that the necessary page directory entries
 * are in place and flushes the TLB to apply the changes.
 */
void vmm_init(void) {
    uint32_t pd_phys = read_cr3() & PAGE_MASK;

    // Map the PD temporarily to inspect / modify
    uint32_t *pd_tmp = (uint32_t*)&page_directory;

    // Install self-map if not already
    if (!(pd_tmp[SELF_PDE_BASE] & PAGE_PRESENT)) {
        pd_tmp[SELF_PDE_BASE] = (pd_phys & PAGE_MASK) | PAGE_FLAGS;
    }

    // Install kmap PDE if not already
    if (!(pd_tmp[KMAP_PDE_BASE] & PAGE_PRESENT)) {
        kmap_pt_phys = (uint32_t)alloc_frame();
        if (!kmap_pt_phys) kernel_panic("vmm_init: out of memory for kmap page table");
        pd_tmp[KMAP_PDE_BASE] = (kmap_pt_phys & PAGE_MASK) | PAGE_FLAGS;
    }

    // Flush TLB so the new mappings take effect
    write_cr3(pd_phys);

    kunmap(); // drop temp mapping of PD

    // Now safe to use cur_pd_va()/pt_va()
    memset(kmap_pt_va(), 0, PAGE_SIZE);
}

/**
 * @brief Get the page table for a specific address space and page directory entry
 *
 * @param as Address space to query
 * @param pde_index Page directory entry index
 * @return vmm_page_table_t* Pointer to the page table, or NULL if not present
 *
 * This function retrieves the page table corresponding to a specific page directory
 * entry within the given address space. If the address space is currently active,
 * it returns a direct pointer to the page table. If the address space is not active,
 * it temporarily maps the page directory to access the page table and returns a
 * pointer to the mapped page table. If the page table is not present, it returns NULL.
 */
vmm_page_table_t *map_get_pt_for_as(address_space_t *as, uint32_t pde_index) {
    if ((read_cr3() & PAGE_MASK) == (as->phys_pdir & PAGE_MASK))
        return pt_va(pde_index);

    vmm_page_directory_t *pd_tmp = (vmm_page_directory_t*)kmap(as->phys_pdir);
    uint32_t pde = pd_tmp[pde_index];
    kunmap();
    if (!(pde & PAGE_PRESENT)) return NULL;

    uint32_t pt_phys = pde & PAGE_MASK;
    return (vmm_page_table_t*)kmap(pt_phys);
}

/**
 * @brief Get or create a page table for a specific address space and page directory entry
 *
 * @param as Address space to query
 * @param pde_index Page directory entry index
 * @param pde_flags Page directory entry flags
 * @return vmm_page_table_t* Pointer to the page table, or NULL if not present
 *
 * This function retrieves the page table corresponding to a specific page directory
 * entry within the given address space. If the address space is currently active,
 * it returns a direct pointer to the page table. If the address space is not active,
 * it temporarily maps the page directory to access the page table and returns a
 * pointer to the mapped page table. If the page table is not present, it returns NULL.
 */
vmm_page_table_t *ensure_pt(address_space_t *as, uint32_t pde_index, uint32_t pde_flags) {
    uint32_t cr3_phys = read_cr3() & PAGE_MASK;

    if ((cr3_phys == (as->phys_pdir & PAGE_MASK))) {
        // current as
        vmm_page_directory_t *pd = cur_pd_va();
        if (!(pd[pde_index] & PAGE_PRESENT)) {
            uint32_t pt_phys = (uint32_t)alloc_frame();
            if (!pt_phys) kernel_panic("ensure_pt: OOM for PT");
            pd[pde_index] = (pt_phys & PAGE_MASK) | (pde_flags & 0xFFF) | PAGE_PRESENT;
            vmm_page_table_t *pt = pt_va(pde_index);
            memset(pt, 0, PAGE_SIZE);
            write_cr3(cr3_phys);
            return pt;
        }
        return pt_va(pde_index);
    }

    // other as
    vmm_page_directory_t *pd_edit = (vmm_page_directory_t*)kmap(as->phys_pdir);
    vmm_page_table_t *pt_va_tmp = NULL;

    if (!(pd_edit[pde_index] & PAGE_PRESENT)) {
        uint32_t pt_phys = (uint32_t)alloc_frame();
        if (!pt_phys) kernel_panic("ensure_pt: out of memory for page table");
        pd_edit[pde_index] = (pt_phys & PAGE_MASK) | (pde_flags & 0xFFF) | PAGE_PRESENT;

        pt_va_tmp = (vmm_page_table_t*)kmap(pt_phys);
        memset(pt_va_tmp, 0, PAGE_SIZE);
        kunmap();
    }

    kunmap();

    // return a va to the pt
    vmm_page_directory_t *pd_check = (vmm_page_directory_t*)kmap(as->phys_pdir);
    uint32_t pt_phys_final = pd_check[pde_index] & PAGE_MASK;
    kunmap();
    return (vmm_page_table_t*)kmap(pt_phys_final);
}

/**
 * @brief Map a virtual address to a physical address in a given address space
 *
 * @param as Address space to modify
 * @param vaddr Virtual address to map
 * @param paddr Physical address to map to
 * @param flags Page table entry flags
 * @param overwrite Flag indicating whether to overwrite an existing mapping
 *
 * This function maps a virtual address to a physical address in the specified address
 * space. It ensures that the necessary page table exists, creates it if needed,
 * and updates the page table entry with the provided physical address and flags. If
 * the mapping already exists and the overwrite flag is not set, it triggers a kernel
 * panic. The function also handles the case where the address space is not currently
 * active by temporarily mapping the page directory to access and modify the page table.
 */
void map_page(address_space_t *as, uint32_t vaddr, uint32_t paddr, uint32_t flags, int overwrite)
{
    vaddr &= PAGE_MASK;
    paddr &= PAGE_MASK;

    uint32_t pdi = vmm_pdi(vaddr);
    uint32_t pti = vmm_pti(vaddr);

    uint32_t pde_flags = (flags & PAGE_USER) ? (PAGE_FLAGS | PAGE_USER) : PAGE_FLAGS;

    uint32_t cur_cr3 = read_cr3() & PAGE_MASK;
    uint32_t as_cr3  = as->phys_pdir & PAGE_MASK;

    if (cur_cr3 == as_cr3) {
        uint32_t *pd = cur_pd_va();
        if (!(pd[pdi] & PAGE_PRESENT)) {
            uint32_t pt_phys = (uint32_t)alloc_frame();
            if (!pt_phys) kernel_panic("map_page: out of memory while mapping page table");
            pd[pdi] = (pt_phys & PAGE_MASK) | pde_flags | PAGE_PRESENT;
            memset(pt_va(pdi), 0, PAGE_SIZE);
            write_cr3(cur_cr3);
        }

        uint32_t *pt = pt_va(pdi);
        uint32_t old = pt[pti];
        if ((old & PAGE_PRESENT) && !overwrite) {
            kernel_panic("map_page: remap without overwrite");
        }
        pt[pti] = (paddr | (flags & 0xFFF) | PAGE_PRESENT);
        invlpg((void*)vaddr);
        return;
    }

    // foreigner
    uint32_t *pd = (uint32_t*)kmap(as_cr3);
    uint32_t pde = pd[pdi];
    if (!(pde & PAGE_PRESENT)) {
        uint32_t pt_phys = (uint32_t)alloc_frame();
        if (!pt_phys) { kunmap(); kernel_panic("map_page: out of memory while mapping page table (foreign)"); }
        pd[pdi] = (pt_phys & PAGE_MASK) | pde_flags | PAGE_PRESENT;
        kunmap();


        uint32_t *newpt = (uint32_t*)kmap(pt_phys);
        memset(newpt, 0, PAGE_SIZE);
        kunmap();
        pde = (pt_phys & PAGE_MASK) | pde_flags | PAGE_PRESENT;
    } else {
        kunmap();
    }
    uint32_t pt_phys = pde & PAGE_MASK;
    uint32_t *pt = (uint32_t*)kmap(pt_phys);
    uint32_t old = pt[pti];
    if ((old & PAGE_PRESENT) && !overwrite) {
        kunmap();
        kernel_panic("map_page: remap without overwrite (foreign)");
    }
    pt[pti] = (paddr | (flags & 0xFFF) | PAGE_PRESENT);
    kunmap();
}

/**
 * @brief Unmap a virtual address in a given address space
 *
 * @param as Address space to modify
 * @param vaddr Virtual address to unmap
 * @param free_frame_flag Flag indicating whether to free the physical frame
 *
 * This function unmaps a virtual address in the specified address space. It checks if
 * the address space is currently active and modifies the page table entry accordingly.
 * If the page table becomes empty after unmapping, it frees the page table frame. If
 * the address space is not currently active, it temporarily maps the page directory to
 * access and modify the page table. If the free_frame_flag is set, it also frees
 * the physical frame associated with the unmapped virtual address.
 */
void unmap_page(address_space_t *as, uint32_t vaddr, int free_frame_flag)
{
    vaddr &= PAGE_MASK;
    uint32_t pdi = vmm_pdi(vaddr);
    uint32_t pti = vmm_pti(vaddr);

    uint32_t cur = read_cr3() & PAGE_MASK;
    uint32_t as_cr3 = as->phys_pdir & PAGE_MASK;

    if (cur == as_cr3) {
        uint32_t *pd = cur_pd_va();
        if (!(pd[pdi] & PAGE_PRESENT)) return;

        uint32_t *pt = pt_va(pdi);
        uint32_t entry = pt[pti];
        if (!(entry & PAGE_PRESENT)) return;

        if (free_frame_flag) free_frame((void*)(entry & PAGE_MASK));
        pt[pti] = 0;
        invlpg((void*)vaddr);

        /* Only reclaim empty page tables for user-space PDEs.
           Kernel-space page tables (heap, FB, layers, stack) are
           statically allocated and shared across all address spaces —
           freeing them would corrupt every process. */
        if (pdi < KERNEL_PDE_BASE) {
            int empty = 1;
            for (int i = 0; i < PAGE_ENTRIES; ++i) {
                if (pt[i] & PAGE_PRESENT) { empty = 0; break; }
            }
            if (empty) {
                uint32_t pt_phys = pd[pdi] & PAGE_MASK;
                pd[pdi] = 0;
                write_cr3(cur);  // flush
                free_frame((void*)pt_phys);
            }
        }
        return;
    }

    // its a foreigner
    uint32_t *pd = (uint32_t*)kmap(as_cr3);
    uint32_t pde = pd[pdi];
    kunmap();
    if (!(pde & PAGE_PRESENT)) return;

    uint32_t pt_phys = pde & PAGE_MASK;

    uint32_t *pt = (uint32_t*)kmap(pt_phys);
    uint32_t entry = pt[pti];
    if (!(entry & PAGE_PRESENT)) { kunmap(); return; }

    if (free_frame_flag) free_frame((void*)(entry & PAGE_MASK));
    pt[pti] = 0;

    /* Only reclaim empty page tables for user-space PDEs (see above). */
    if (pdi < KERNEL_PDE_BASE) {
        int empty = 1;
        for (int i = 0; i < PAGE_ENTRIES; ++i) {
            if (pt[i] & PAGE_PRESENT) { empty = 0; break; }
        }
        kunmap();

        if (empty) {
            uint32_t *pd2 = (uint32_t*)kmap(as_cr3);
            pd2[pdi] = 0;
            kunmap();
            free_frame((void*)pt_phys);
        }
    } else {
        kunmap();
    }
}

/**
 * @brief Get the mapping object
 *
 * @param as Address space to modify
 * @param vaddr Virtual address to unmap
 * @return uint32_t Physical address mapped to the virtual address, or 0 if not mapped
 *
 * This function retrieves the physical address mapped to a given virtual address
 * in the specified address space. It checks if the address space is currently active
 * and accesses the page table entry directly. If the address space is not currently
 * active, it temporarily maps the page directory to access the page table. If the
 * virtual address is not mapped, it returns 0.
 */
uint32_t get_mapping(address_space_t *as, uint32_t vaddr) {
    vaddr &= PAGE_MASK;
    uint32_t pdi = vmm_pdi(vaddr);
    uint32_t pti = vmm_pti(vaddr);

    uint32_t cur_cr3 = read_cr3() & PAGE_MASK;
    uint32_t as_cr3  = as->phys_pdir & PAGE_MASK;

    if (cur_cr3 == as_cr3) {
        uint32_t *pd = cur_pd_va();
        uint32_t pde = pd[pdi];
        if (!(pde & PAGE_PRESENT)) return 0;
        uint32_t *pt = pt_va(pdi);
        uint32_t pte = pt[pti];
        return (pte & PAGE_PRESENT) ? (pte & PAGE_MASK) : 0;
    }

    // foreigner
    uint32_t *pd = (uint32_t*)kmap(as_cr3);
    uint32_t pde = pd[pdi];
    kunmap();
    if (!(pde & PAGE_PRESENT)) return 0;

    uint32_t pt_phys = pde & PAGE_MASK;
    uint32_t *pt = (uint32_t*)kmap(pt_phys);
    uint32_t pte = pt[pti];
    kunmap();

    return (pte & PAGE_PRESENT) ? (pte & PAGE_MASK) : 0;
}

int copy_to_user(address_space_t *as, uint32_t dst, const void *src, size_t len) {
    if (!as || !src) {
        printfs(PRINT_STATUS_ERROR,"copy_to_user: Bad address space/source! as:%p, src:%p, dst:%p len:%d\n",as,src,dst,len);
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (dst < USER_SPACE_START || dst > USER_SPACE_END) {
        printfs(PRINT_STATUS_ERROR,"copy_to_user: Bad destination address! as:%p, src:%p, dst:%p len:%d\n",as,src,dst,len);
        return -1;
    }
    if (dst + len - 1 < dst || dst + len - 1 > USER_SPACE_END) {
        printfs(PRINT_STATUS_ERROR,"copy_to_user: Bad destination address! as:%p, src:%p, dst:%p len:%d\n",as,src,dst,len);
        return -1;
    }
    const uint8_t *src_bytes = (const uint8_t *)src;

    while (len) {
        uint32_t va = dst & PAGE_MASK;
        uint32_t off = dst & (PAGE_SIZE - 1);
        uint32_t phys = get_mapping(as, va);
        if (!phys) {
            printfs(PRINT_STATUS_ERROR,"copy_to_user: Unmapped page! as:%p, src:%p, dst:%p len:%d\n",as,src,dst,len);
            return -1;
        }

        size_t chunk = PAGE_SIZE - off;
        if (chunk > len)
            chunk = len;

        uint32_t eflags;
        asm volatile("pushfl; popl %0" : "=r"(eflags));
        int ints_were_on = eflags & 0x200;
        if (ints_were_on) clear_interrupts();
        uint8_t *dst_k = (uint8_t *)kmap(phys);
        memcpy(dst_k + off, src_bytes, chunk);
        kunmap();
        if (ints_were_on) enable_interrupts();

        dst += (uint32_t)chunk;
        src_bytes += chunk;
        len -= chunk;
    }

    return 0;
}

int copy_from_user(address_space_t *as, void *dst, uint32_t src, size_t len) {
    if (!as || !dst) {
        printfs(PRINT_STATUS_ERROR,"copy_from_user: Bad address space/src! as:%p, src:%p, dst:%p len:%d\n",as,src,dst,len);
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (src < USER_SPACE_START || src > USER_SPACE_END) {
        printfs(PRINT_STATUS_ERROR,"copy_from_user: Bad source address! as:%p, src:%p, dst:%p len:%d\n",as,src,dst,len);
        return -1;
    }
    if (src + len - 1 < src || src + len - 1 > USER_SPACE_END) {
        printfs(PRINT_STATUS_ERROR,"copy_from_user: Bad source address! as:%p, src:%p, dst:%p len:%d\n",as,src,dst,len);
        return -1;
    }
    uint8_t *dst_bytes = (uint8_t *)dst;

    while (len) {
        uint32_t va = src & PAGE_MASK;
        uint32_t off = src & (PAGE_SIZE - 1);
        uint32_t phys = get_mapping(as, va);
        if (!phys) {
            printfs(PRINT_STATUS_ERROR,"copy_from_user: Unmapped page! as:%p, src:%p, dst:%p len:%d\n",as,src,dst,len);
            return -1;
        }

        size_t chunk = PAGE_SIZE - off;
        if (chunk > len)
            chunk = len;

        uint32_t eflags;
        asm volatile("pushfl; popl %0" : "=r"(eflags));
        int ints_were_on = eflags & 0x200;
        if (ints_were_on) clear_interrupts();
        uint8_t *src_k = (uint8_t *)kmap(phys);
        memcpy(dst_bytes, src_k + off, chunk);
        kunmap();
        if (ints_were_on) enable_interrupts();

        src += (uint32_t)chunk;
        dst_bytes += chunk;
        len -= chunk;
    }

    return 0;
}

/**
 * @brief Allocate and map a new page
 *
 * @param as Address space to modify
 * @param vaddr Virtual address to map
 * @param flags Page table entry flags
 * @return uint32_t Physical address of the allocated page, or 0 on failure
 *
 * This function allocates a new physical page and maps it to the specified virtual address.
 */
uint32_t alloc_map_page(address_space_t *as, uint32_t vaddr, uint32_t flags) {
    void *phys = alloc_frame();
    if (!phys) kernel_panic("alloc_map_page: OOM");
    map_page(as, vaddr, (uint32_t)phys, flags, /*overwrite=*/0);
    return (uint32_t)phys;
}

/**
 * @brief Create a address space object
 *
 * @return address_space_t*  Pointer to the created address space
 *
 * This function creates a new address space by allocating a new page directory
 * and cloning the kernel half of the current page directory. It sets up the self-mapping
 * and kernel mapping entries in the new page directory. If memory allocation fails,
 * it triggers a kernel panic.
 */
address_space_t *create_address_space(void) {

    address_space_t *as = (address_space_t*)kernel_malloc_align(16, sizeof(*as));
    if (!as) kernel_panic("create_address_space: out of memory (address space)");

    uint32_t pd_phys = (uint32_t)alloc_frame();
    if (!pd_phys) kernel_panic("create_address_space: out of memory (page directory)");

    void *pd_tmp = kmap(pd_phys);
    memset(pd_tmp, 0, PAGE_SIZE);

    // clone kernel half pdes from current pd
    vmm_page_directory_t *cur = cur_pd_va();
    vmm_page_directory_t *newp = (vmm_page_directory_t*)pd_tmp;

    for (uint32_t i = KERNEL_PDE_BASE; i < PAGE_ENTRIES; ++i)
        newp[i] = cur[i];

    newp[SELF_PDE_BASE] = (pd_phys & PAGE_MASK) | PAGE_FLAGS;
    newp[KMAP_PDE_BASE] = (kmap_pt_phys & PAGE_MASK) | PAGE_FLAGS;

    newp[0] = cur[0] | PAGE_FLAGS; // identity

    kunmap();

    as->phys_pdir = pd_phys;
    as->shmem_list = NULL;
    return as;
}

/**
 * @brief Destroy an address space object
 *
 * @param as Address space to destroy
 *
 * This function frees all resources associated with the given address space,
 * including its page directory and any allocated page tables.
 *
 * If the address space pointer is NULL, the function does nothing.
 */
void destroy_address_space(address_space_t *as) {
    if (!as) return;

    uint32_t shmem_pdi_start = vmm_pdi(SHMEM_START);
    uint32_t shmem_pdi_end   = vmm_pdi(SHMEM_END);

    for (uint32_t pdi = 1; pdi < KERNEL_PDE_BASE; ++pdi) {
        vmm_page_directory_t *pd = (vmm_page_directory_t*)kmap(as->phys_pdir);
        uint32_t pde = pd[pdi];
        kunmap();
        if (!(pde & PAGE_PRESENT)) continue;
        uint32_t pt_phys = pde & PAGE_MASK;

        int in_shmem = (pdi >= shmem_pdi_start && pdi <= shmem_pdi_end);

        vmm_page_table_t *pt = (vmm_page_table_t*)kmap(pt_phys);
        for (uint32_t i = 0; i < PAGE_ENTRIES; ++i) {
            uint32_t pti = pt[i];
            if (!(pti & PAGE_PRESENT)) continue;
            if (!in_shmem) {
                uint32_t p = pt[i] & PAGE_MASK;
                free_frame((void*)p);
            }
            pt[i] = 0;
        }

        kunmap();
        free_frame((void*)pt_phys);
        pd = (vmm_page_directory_t*)kmap(as->phys_pdir);
        pd[pdi] = 0;
        kunmap();
    }

    free_frame((void*)as->phys_pdir);
    kernel_free_align(as);
}

/**
 * @brief Switch to a different address space
 *
 * @param as Address space to switch to
 *
 * This function switches the current address space to the specified one by updating
 * the CR3 register with the physical address of the new page directory. If the new
 * address space is already active, it does nothing.
 */
void switch_address_space(address_space_t *as) {
    if (!as) return;
    uint32_t new_cr3 = as->phys_pdir & PAGE_MASK;
    if ((read_cr3() & PAGE_MASK) != new_cr3)
        write_cr3(new_cr3);
}

shm_object_t* shm_create(uint32_t size) {
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t npages = size / PAGE_SIZE;

    shm_object_t *shm = kernel_malloc(sizeof(shm_object_t));
    shm->size = size;
    shm->npages = npages;
    shm->refcount = 1;
    shm->kernel_addr = 0;

    shm->phys_pages = kernel_malloc(sizeof(uint32_t) * npages);

    for (uint32_t i = 0; i < npages; i++) {
        uint32_t frame = (uint32_t)alloc_frame();
        shm->phys_pages[i] = frame;
    }

    return shm;
}

static uint32_t shm_find_free_region(address_space_t *as, uint32_t size) {
    uint32_t base = SHMEM_START;
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    shmem_map_t *m = as->shmem_list;

    while (m) {
        if (base + size <= m->start)
            return base;
        base = (m->start + m->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        m = m->next;
    }

    if (base + size <= SHMEM_END)
        return base;

    return 0;
}

uint32_t shm_map(process_control_block_t* pcb, shm_object_t *shm) {
    address_space_t *as = pcb->address_space;

    uint32_t va = shm_find_free_region(as, shm->size);
    if (!va) kernel_panic("shm_map: no free space");

    for (uint32_t i = 0; i < shm->npages; i++) {
        map_page(as, va + i * PAGE_SIZE, shm->phys_pages[i], USER_PAGE_FLAGS, 1);
    }

    shmem_map_t *m = kernel_malloc(sizeof(shmem_map_t));
    m->start = va;
    m->size = shm->size;
    m->shm = shm;

    m->next = as->shmem_list;
    as->shmem_list = m;

    shm->refcount++;

    return va;
}

void shm_unmap(address_space_t *as, uint32_t vaddr) {
    shmem_map_t **pp = &as->shmem_list;
    while (*pp) {
        if ((*pp)->start == vaddr) {
            shmem_map_t *m = *pp;
            shm_object_t *shm = m->shm;

            for (uint32_t i = 0; i < shm->npages; i++) {
                unmap_page(as, vaddr + i * PAGE_SIZE, 0);
            }

            *pp = m->next;
            kernel_free(m);

            shm->refcount--;
            if (shm->refcount == 0) {
                for (uint32_t i = 0; i < shm->npages; i++) {
                    free_frame((void*)shm->phys_pages[i]);
                }
                kernel_free(shm->phys_pages);

                for (int i = 0; i < MAX_SHM_OBJECTS; i++) {
                    if (shm_table[i] == shm) {
                        shm_table[i] = NULL;
                        break;
                    }
                }

                kernel_free(shm);
            }
            return;
        }
        pp = &(*pp)->next;
    }
}

int shm_alloc_id(void) {
    for (int i = 0; i < MAX_SHM_OBJECTS; i++) {
        if (shm_table[i] == NULL)
            return i;
    }
    return -1;
}
