#ifndef _USER_FS
#define _USER_FS

#include "../vfs.h"
#include <stddef.h>

typedef struct file_handle {
    vfs_node_t    *node;      // VFS node
    uint32_t       flags;     // open flags
    uint32_t       offset;    // current file offset for reads/writes
    uint32_t       refcount;  // # of FDs/share this same handle
} file_handle_t;

static inline void file_handle_list_init(file_handle_list_t *lst) {
    lst->head = lst->tail = NULL;
}

static inline void _fh_list_append(file_handle_list_t *lst, file_handle_t *fh) {
    fh->next = NULL;
    if (lst->tail) {
        lst->tail->next = fh;
    } else {
        lst->head = fh;
    }
    lst->tail = fh;
}

static void _fh_list_remove(file_handle_list_t *lst, file_handle_t *fh) {
    file_handle_t *prev = NULL, *cur = lst->head;
    while (cur && cur != fh) {
        prev = cur;
        cur  = cur->next;
    }
    if (!cur) return;
    if (prev) {
        prev->next = cur->next;
    } else {
        lst->head = cur->next;
    }
    if (lst->tail == cur) {
        lst->tail = prev;
    }
}

file_handle_t *file_handle_create(file_handle_list_t *lst, vfs_node_t *node, uint32_t flags);
void file_handle_close(file_handle_list_t *lst,file_handle_t *fh);

#endif