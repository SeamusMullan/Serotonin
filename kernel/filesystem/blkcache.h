#ifndef _BLKCACHE_H
#define _BLKCACHE_H

#include <stdint.h>

#define BLKCACHE_NUM_BUCKETS  64
#define BLKCACHE_MAX_ENTRIES  256
#define BLKCACHE_MEMDRIVE_BASE 0x80

typedef struct blkcache_entry {
    uint8_t  drive;
    uint32_t lba;
    uint8_t  data[512];
    uint8_t  valid;

    struct blkcache_entry *lru_prev;
    struct blkcache_entry *lru_next;
    struct blkcache_entry *hash_next;
} blkcache_entry_t;

void blkcache_init(void);
int  blkcache_read_sector(uint8_t drive, uint32_t lba, uint8_t *buf);
int  blkcache_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t *buf);
int  blkcache_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buf);
void blkcache_invalidate(uint8_t drive);
void blkcache_flush(uint8_t drive);
int  blkcache_register_memdrive(uint8_t slot, const void *base, uint32_t size_bytes);
int  blkcache_is_memdrive(uint8_t drive);

#endif
