/*
    vmm.c
    Serotonin Virtual Memory Manager

    Virtual Memory Layout:
        0x00000000 – 0x003FFFFF - Identity map
        0x00400000 – 0x013FFFFF - User text/data/bss
        0x01400000 – 0x0EFFFFFF - User heap
        0x0F000000 – 0x103FFFFF - User stack
        0x10400000 – 0xBFFFFFFF - RESERVED
        0xC0000000 – 0xCFFFFFFF - Kernel
        0xD0000000 – 0xDFFFFFFF - Kernel heap
        0xE0000000 – 0xEFFFFFFF - Framebuffer (might reallocate this)
        0xF0000000 – 0xF03FFFFF - Kernel stack
*/

#include "vmm.h"
#include "paging.h"
#include "../stdlib/stdlib.h"
#include "../multiboot.h"
#include "../kernel.h"

buddy_state_t g_buddy = {0};

// compute largest order such that:
// (idx % (1<<order)) == 0 (alignment)
// (idx + (1<<order)) <= limit
// order <= zone->max_order
static int largest_order_fit(uint32_t idx, uint32_t limit, uint8_t max_order) {
    uint32_t remain = limit - idx;
    int o = 0;
    // largest power-of-two <= remain
    while (o < (int)max_order && ((1u << (o+1)) <= remain)) o++;
    // fix alignment
    while (o > 0 && (idx & ((1u << o) - 1)) != 0) o--;
    if ((1u << o) > remain) return -1;
    return o;
}

static void zone_freelist_init(buddy_zone_t *z) {
    for (int i = 0; i <= MAX_ORDER; ++i) z->free_list[i] = NULL;
}

static void zone_push_free(buddy_zone_t *z, uint32_t head_idx, int order) {
    page_desc_t *head = &z->pages[head_idx];
    head->order = (uint8_t)order;
    head->used  = 0;
    head->next  = z->free_list[order];
    z->free_list[order] = head;
}

static page_desc_t *zone_pop_free(buddy_zone_t *z, int order) {
    page_desc_t *h = z->free_list[order];
    if (!h) return NULL;
    z->free_list[order] = h->next;
    h->next = NULL;
    return h;
}

static inline uint32_t buddy_index(uint32_t idx, int order) {
    return idx ^ (1u << order);
}

static void zone_build(buddy_zone_t *z) {
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

static page_desc_t* zone_split_to(buddy_zone_t *z, page_desc_t *blk, int have_order, int want_order) {
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

static void *zone_alloc_pages(buddy_zone_t *z, int order) {
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

static void zone_free_pages(buddy_zone_t *z, void *phys_addr, int order) {
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

static int find_zone_by_phys(uint32_t phys) {
    for (uint32_t i = 0; i < g_buddy.zone_count; ++i) {
        buddy_zone_t *z = &g_buddy.zones[i];
        if (phys >= z->base_phys &&
            phys <  z->base_phys + ((uint32_t)z->num_pages << PAGE_SHIFT)) {
            return (int)i;
        }
    }
    return -1;
}

void *alloc_pages(int order) {
    for (uint32_t i = 0; i < g_buddy.zone_count; ++i) {
        void *p = zone_alloc_pages(&g_buddy.zones[i], order);
        if (p) return p;
    }
    kernel_panic("out of memory (buddy alloc)");
}

void free_pages(void *phys_addr, int order) {
    uint32_t phys = (uint32_t)phys_addr;
    int zi = find_zone_by_phys(phys);
    if (zi < 0) return;
    zone_free_pages(&g_buddy.zones[zi], phys_addr, order);
}

uint32_t buddy_total_pages(void) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < g_buddy.zone_count; ++i)
        sum += g_buddy.zones[i].num_pages;
    return sum;
}

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

static void add_zone(uint64_t base, uint64_t end) {
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
static int carve_exclusion(uint64_t in_start, uint64_t in_end,
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

void buddy_init(multiboot_info_t *mbi, uint32_t kernel_phys_start, uint32_t kernel_phys_end, uint32_t fb_phys_base, uint32_t  fb_length) {
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