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
#include "paging_init.h"
#include "../stdlib/stdlib.h"
#include "../multiboot.h"
#include "../kernel.h"

buddy_state_t g_buddy = {0};
uint32_t kmap_pt_phys = 0;

static inline vmm_page_table_t *kmap_pt_va(void) { return pt_va(KMAP_PDE_BASE); }

// compute largest order such that:
// (idx % (1<<order)) == 0 (alignment)
// (idx + (1<<order)) <= limit
// order <= zone->max_order
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

void zone_freelist_init(buddy_zone_t *z) {
    for (int i = 0; i <= MAX_ORDER; ++i) z->free_list[i] = NULL;
}

void zone_push_free(buddy_zone_t *z, uint32_t head_idx, int order) {
    page_desc_t *head = &z->pages[head_idx];
    head->order = (uint8_t)order;
    head->used  = 0;
    head->next  = z->free_list[order];
    z->free_list[order] = head;
}

page_desc_t *zone_pop_free(buddy_zone_t *z, int order) {
    page_desc_t *h = z->free_list[order];
    if (!h) return NULL;
    z->free_list[order] = h->next;
    h->next = NULL;
    return h;
}

static inline uint32_t buddy_index(uint32_t idx, int order) {
    return idx ^ (1u << order);
}

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

void *kmap(uint32_t phys) {
    if (!kmap_pt_phys) {
        kernel_panic("kmap: not initialized");
    }
    vmm_page_table_t *pt = kmap_pt_va();
    pt[KMAP_PTE_BASE] = (phys & PAGE_MASK) | PAGE_FLAGS;
    invlpg((void*)KMAP_BASE);
    return (void*)KMAP_BASE;
}

void kunmap(void) {
    vmm_page_table_t *pt = kmap_pt_va();
    pt[KMAP_PTE_BASE] = 0;
    invlpg((void*)KMAP_BASE);
}

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

void unmap_page(address_space_t *as, uint32_t vaddr, int free_frame_flag)
{
    vaddr &= PAGE_MASK;
    uint32_t pdi = vmm_pdi(vaddr);
    uint32_t pti = vmm_pti(vaddr);

    uint32_t cur = read_cr3() & PAGE_MASK;
    uint32_t as_cr3 = as->phys_pdir & PAGE_MASK;

    if (cur == as_cr3) {
        uint32_t *pt = pt_va(pdi);
        uint32_t entry = pt[pti];
        if (!(entry & PAGE_PRESENT)) return;

        if (free_frame_flag) free_frame((void*)(entry & PAGE_MASK));
        pt[pti] = 0;
        invlpg((void*)vaddr);

        uint32_t *pd = cur_pd_va();
        if (pd[pdi] & PAGE_PRESENT) {
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
}

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


uint32_t alloc_map_page(address_space_t *as, uint32_t vaddr, uint32_t flags) {
    void *phys = alloc_frame();
    if (!phys) kernel_panic("alloc_map_page: OOM");
    map_page(as, vaddr, (uint32_t)phys, flags, /*overwrite=*/0);
    return (uint32_t)phys;
}

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
    return as;
}

void destroy_address_space(address_space_t *as) {
    if (!as) return;

    // free all user pts and their frames (pdes < KERNEL_PDE_BASE)
    vmm_page_directory_t *pd = (vmm_page_directory_t*)kmap(as->phys_pdir);

    for (uint32_t pdi = 0; pdi < KERNEL_PDE_BASE; ++pdi) {
        if (!(pd[pdi] & PAGE_PRESENT)) continue;
        uint32_t pt_phys = pd[pdi] & PAGE_MASK;
        vmm_page_table_t *pt = (vmm_page_table_t*)kmap(pt_phys);

        for (uint32_t i = 0; i < PAGE_ENTRIES; ++i) {
            if (pt[i] & PAGE_PRESENT) {
                uint32_t p = pt[i] & PAGE_MASK;
                free_frame((void*)p);
                pt[i] = 0;
            }
        }

        kunmap();
        free_frame((void*)pt_phys);
        pd[pdi] = 0;
    }

    kunmap();
    free_frame((void*)as->phys_pdir);
    kernel_free_align(as);
}

void switch_address_space(address_space_t *as) {
    if (!as) return;
    uint32_t new_cr3 = as->phys_pdir & PAGE_MASK;
    if ((read_cr3() & PAGE_MASK) != new_cr3)
        write_cr3(new_cr3);
}
