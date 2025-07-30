#ifndef _USER_FS
#define _USER_FS

#include "../vfs.h"
#include <stddef.h>

#define FD_MAX 64
#define FIRST_FD 3

// terry davis was right, gcc is a piece of shit
typedef struct process_control_block process_control_block_t;

typedef struct file_handle {
    vfs_node_t    *node;      // VFS node
    uint32_t       flags;     // open flags
    uint32_t       offset;    // current file offset for reads/writes
    uint32_t       refcount;  // # of FDs/share this same handle
} file_handle_t;

int alloc_fd(process_control_block_t *pcb, file_handle_t *handle);
int close_fd(process_control_block_t *pcb, int fd);

#endif