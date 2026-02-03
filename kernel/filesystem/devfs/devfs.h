#ifndef _FS_DEVFS
#define _FS_DEVFS

#include "../vfs.h"
#include "../../syscall/sys/types.h"
#include "../../syscall/sys/file.h"

typedef struct devfs_dir_entry {
    vfs_node_t *node;
    struct devfs_dir_entry *next;
} devfs_dir_entry_t;

typedef struct devfs_dir {
    devfs_dir_entry_t *entries;
} devfs_dir_t;

typedef struct devfs_file {
    mode_t mode;
    vfs_ops_t *ops;
} devfs_file_t;

void devfs_init(void);
vfs_node_t *devfs_mount(const char *device);
int devfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int devfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
int devfs_truncate(vfs_node_t *node, uint32_t size);
int devfs_open(vfs_node_t *node);
int devfs_close(vfs_node_t *node);
vfs_node_t *devfs_readdir(vfs_node_t *node, uint32_t index);
vfs_node_t *devfs_finddir(vfs_node_t *node, const char *name);
int devfs_register_device(const char *path, mode_t mode, vfs_ops_t *ops);

#endif
