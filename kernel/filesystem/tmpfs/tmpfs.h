#ifndef _FS_TMPFS
#define _FS_TMPFS

#include "../vfs.h"

typedef struct tmpfs_file {
    char *data;
    uint32_t size;
} tmpfs_file_t;

typedef struct tmpfs_dir_entry {
    vfs_node_t *node;
    struct tmpfs_dir_entry *next;
} tmpfs_dir_entry_t;

typedef struct tmpfs_dir {
    tmpfs_dir_entry_t *entries;
} tmpfs_dir_t;


void tmpfs_init(void);


#endif