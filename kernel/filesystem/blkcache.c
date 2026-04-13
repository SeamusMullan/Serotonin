#include <kernel/filesystem/blkcache.h>
#include <kernel/device/ide/ide_pci.h>
#include <kernel/kernel.h>
#include <kernel/stdlib/stdlib.h>

static blkcache_entry_t entries[BLKCACHE_MAX_ENTRIES];
static blkcache_entry_t *hash_table[BLKCACHE_NUM_BUCKETS];
static blkcache_entry_t *lru_head;
static blkcache_entry_t *lru_tail;
static uint32_t used_count;

static inline uint32_t blkcache_hash(uint8_t drive, uint32_t lba) {
    return (drive * 2654435761u + lba) % BLKCACHE_NUM_BUCKETS;
}

static void lru_remove(blkcache_entry_t *e) {
    if (e->lru_prev) e->lru_prev->lru_next = e->lru_next;
    else             lru_head = e->lru_next;
    if (e->lru_next) e->lru_next->lru_prev = e->lru_prev;
    else             lru_tail = e->lru_prev;
    e->lru_prev = e->lru_next = NULL;
}

static void lru_push_front(blkcache_entry_t *e) {
    e->lru_prev = NULL;
    e->lru_next = lru_head;
    if (lru_head) lru_head->lru_prev = e;
    else          lru_tail = e;
    lru_head = e;
}

static void hash_remove(blkcache_entry_t *e) {
    uint32_t bucket = blkcache_hash(e->drive, e->lba);
    blkcache_entry_t **pp = &hash_table[bucket];
    while (*pp) {
        if (*pp == e) {
            *pp = e->hash_next;
            e->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

static void hash_insert(blkcache_entry_t *e) {
    uint32_t bucket = blkcache_hash(e->drive, e->lba);
    e->hash_next = hash_table[bucket];
    hash_table[bucket] = e;
}

static blkcache_entry_t *cache_lookup(uint8_t drive, uint32_t lba) {
    uint32_t bucket = blkcache_hash(drive, lba);
    blkcache_entry_t *e = hash_table[bucket];
    while (e) {
        if (e->valid && e->drive == drive && e->lba == lba)
            return e;
        e = e->hash_next;
    }
    return NULL;
}

static blkcache_entry_t *cache_alloc(uint8_t drive, uint32_t lba) {
    blkcache_entry_t *e;

    if (used_count < BLKCACHE_MAX_ENTRIES) {
        e = &entries[used_count++];
        e->valid = 0;
    } else {
        // evict LRU tail
        e = lru_tail;
        if (!e) return NULL;
        lru_remove(e);
        if (e->valid)
            hash_remove(e);
    }

    e->drive = drive;
    e->lba = lba;
    e->valid = 1;
    hash_insert(e);
    lru_push_front(e);
    return e;
}

void blkcache_init(void) {
    memset(entries, 0, sizeof(entries));
    memset(hash_table, 0, sizeof(hash_table));
    lru_head = NULL;
    lru_tail = NULL;
    used_count = 0;
}

int blkcache_read_sector(uint8_t drive, uint32_t lba, uint8_t *buf) {
    blkcache_entry_t *e = cache_lookup(drive, lba);
    if (e) {
        lru_remove(e);
        lru_push_front(e);
        memcpy(buf, e->data, 512);
        return 0;
    }

    // cache miss - read from disk
    e = cache_alloc(drive, lba);
    if (!e)
        return ide_read_sector(drive, lba, buf);

    int r = ide_read_sector(drive, lba, e->data);
    if (r != 0) {
        // failed - invalidate entry
        e->valid = 0;
        return r;
    }

    memcpy(buf, e->data, 512);
    return 0;
}

int blkcache_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t *buf) {
    if (count == 0) return 0;
    if (count == 1) return blkcache_read_sector(drive, lba, buf);

    // check if all sectors are cached
    int all_cached = 1;
    for (uint8_t i = 0; i < count; i++) {
        if (!cache_lookup(drive, lba + i)) {
            all_cached = 0;
            break;
        }
    }

    if (all_cached) {
        for (uint8_t i = 0; i < count; i++)
            blkcache_read_sector(drive, lba + i, buf + i * 512);
        return 0;
    }

    // batch read from disk, then populate cache
    int r = ide_read_sectors(drive, lba, count, buf);
    if (r != 0) return r;

    for (uint8_t i = 0; i < count; i++) {
        blkcache_entry_t *e = cache_lookup(drive, lba + i);
        if (e) {
            // update existing entry
            memcpy(e->data, buf + i * 512, 512);
            lru_remove(e);
            lru_push_front(e);
        } else {
            e = cache_alloc(drive, lba + i);
            if (e)
                memcpy(e->data, buf + i * 512, 512);
        }
    }

    return 0;
}

int blkcache_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buf) {
    // write-through: write to disk first
    int r = ide_write_sector(drive, lba, buf);
    if (r != 0) return r;

    // update cache
    blkcache_entry_t *e = cache_lookup(drive, lba);
    if (e) {
        memcpy(e->data, buf, 512);
        lru_remove(e);
        lru_push_front(e);
    } else {
        e = cache_alloc(drive, lba);
        if (e)
            memcpy(e->data, buf, 512);
    }

    return 0;
}

void blkcache_invalidate(uint8_t drive) {
    for (uint32_t i = 0; i < used_count; i++) {
        if (entries[i].valid && entries[i].drive == drive) {
            hash_remove(&entries[i]);
            entries[i].valid = 0;
        }
    }
}

void blkcache_flush(uint8_t drive) {
    // write-through cache: nothing to flush for data
    // but issue a device cache flush
    ide_cache_flush(drive);
}
