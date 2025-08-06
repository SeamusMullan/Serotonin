#include "user_fs.h"
#include <stddef.h>
#include "../vfs.h"
#include "../../kernel.h"
#include "../../schedule/schedule.h"

int alloc_fd(process_control_block_t *pcb, file_handle_t *handle) {
    for (int i = FIRST_FD; i < FD_MAX; i++) {
        if (pcb->fd_table[i] == NULL) {
            pcb->fd_table[i] = handle;
            handle->refcount++;
            return i;
        }
    }
    return -1;
}

int close_fd(process_control_block_t *pcb, int fd) {
    if (fd < 0 || fd >= FD_MAX || pcb->fd_table[fd] == NULL)
        return -1;

    file_handle_t *handle = pcb->fd_table[fd];
    if (--handle->refcount == 0) {
        kernel_free(pcb->fd_table[fd]);
    }
    pcb->fd_table[fd] = NULL;
    return 0;
}