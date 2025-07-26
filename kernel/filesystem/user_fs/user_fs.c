#include "user_fs.h"
#include <stddef.h>
#include "../vfs.h"
#include "../../kernel.h"

file_handle_t *file_handle_create(file_handle_list_t *lst, vfs_node_t *node, uint32_t flags)
{
    file_handle_t *fh = kernel_malloc(sizeof(*fh));
    if (!fh) return NULL;
    fh->node     = node;
    fh->flags    = flags;
    fh->offset   = 0;
    fh->refcount = 1;

    node->refcount++;
    _fh_list_append(lst, fh);
    return fh;
}


void file_handle_close(file_handle_list_t *lst, file_handle_t *fh)
{
    if (!fh) return;
    // only free when last reference
    if (fh->refcount > 1) {
        fh->refcount--;
        return;
    }
    _fh_list_remove(lst, fh);
    if (fh->node && --fh->node->refcount == 0) {
        vfs_close(fh->node);
    }
    kernel_free(fh);
}