#include "user_fs.h"
#include <stddef.h>
#include "../vfs.h"
#include "../../kernel.h"
#include "../../schedule/schedule.h"

/**
 * @brief Allocates a file descriptor for a process.
 *
 * @param pcb The process control block of the process requesting the file descriptor.
 * @param handle The file handle to associate with the new file descriptor.
 * @return int The allocated file descriptor, or -1 on failure.
 */
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

/**
 * @brief Closes a file descriptor for a process.
 *
 * @param pcb The process control block of the process requesting the file descriptor.
 * @param fd The file descriptor to close.
 * @return int 0 on success, -1 on failure.
 */
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