#ifndef _FS_DEVFS
#define _FS_DEVFS

#include "../vfs.h"
#include "../../schedule/schedule.h"
#include "../../syscall/sys/types.h"
#include "../../syscall/sys/file.h"

typedef struct devfs_waiter {
    struct process_control_block *task;
    struct devfs_waiter *next;
} devfs_waiter_t;

typedef struct devfs_wait_queue {
    devfs_waiter_t *head;
    devfs_waiter_t *tail;
} devfs_wait_queue_t;

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
    devfs_wait_queue_t wait_queue;
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
void devfs_wait_queue_init(devfs_wait_queue_t *queue);
int devfs_wait_enqueue(devfs_wait_queue_t *queue, process_control_block_t *task);
process_control_block_t *devfs_wait_dequeue(devfs_wait_queue_t *queue);
void devfs_wait_wake_one(devfs_wait_queue_t *queue);
void devfs_wait_wake_all(devfs_wait_queue_t *queue);

devfs_wait_queue_t *devfs_get_wait_queue(vfs_node_t *node);

#endif
