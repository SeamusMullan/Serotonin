/**
 * @file dev_mouse.c
 * @brief PS/2 mouse device driver implementation
 *
 * Implements mouse input devices for reading position and button events.
 * Button events are stored in a ring buffer and can be read with blocking
 * or non-blocking semantics. When a task blocks waiting for an event,
 * the event is delivered directly to user space via copy_to_user when
 * it becomes available.
 */

#include "dev_mouse.h"
#include "../../kernel.h"
#include "../../filesystem/devfs/devfs.h"
#include "../../filesystem/vfs.h"
#include "../../io/io.h"
#include "../../stdio/stdio.h"
#include "../../stdlib/stdlib.h"
#include "../../string.h"
#include "../../syscall/sys/file.h"
#include "../../syscall/sys/errno.h"
#include "../../schedule/schedule.h"
#include "../../vmm/vmm.h"

/** Ring buffer for mouse events */
static mouse_event_t event_buffer[MOUSE_EVENT_BUFFER_SIZE];
static volatile uint32_t event_head = 0;   /**< Next write position */
static volatile uint32_t event_tail = 0;   /**< Next read position */
static volatile uint32_t event_count = 0;  /**< Number of events in buffer */

/** VFS node for /dev/mouse/event, used for wake queue access */
vfs_node_t *dev_mouse_event_node = NULL;

/** VFS operations for /dev/mouse/pos */
static vfs_ops_t dev_mouse_read_pos_ops = {
    .read = dev_mouse_read_pos,
    .write = NULL,
    .truncate = NULL,
    .unlink = NULL,
    .rmdir = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .mkdir = NULL
};

/** VFS operations for /dev/mouse/event */
static vfs_ops_t dev_mouse_event_ops = {
    .read = dev_mouse_read_event,
    .write = NULL,
    .truncate = NULL,
    .unlink = NULL,
    .rmdir = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .mkdir = NULL
};

int dev_mouse_read_pos(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)node;

    // Format position as "x,y" string
    char mouse_pos[32];
    snprintf(mouse_pos, 31, "%d,%d", mouse_x, mouse_y);
    mouse_pos[31] = '\0';

    uint32_t len = (uint32_t)strlen(mouse_pos);
    if (offset >= len) return 0;

    uint32_t remaining = len - offset;
    uint32_t to_copy = size < remaining ? size : remaining;
    memcpy(buffer, mouse_pos + offset, to_copy);
    return (int)to_copy;
}

int dev_mouse_read_event(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)offset;

    if (!node || !buffer) return -1;
    if (size < sizeof(mouse_event_t)) return -EINVAL;

    devfs_wait_queue_t *wq = devfs_get_wait_queue(node);
    if (!wq) return -1;

    uint32_t flags = current_task->current_fd_flags;

    while (1) {
        lock_scheduler();

        // Check if an event is available in the buffer
        if (event_count > 0) {
            mouse_event_t *ev = &event_buffer[event_tail];
            memcpy(buffer, ev, sizeof(mouse_event_t));
            event_tail = (event_tail + 1) % MOUSE_EVENT_BUFFER_SIZE;
            event_count--;
            current_task->processor_context->eax = sizeof(mouse_event_t);
            unlock_scheduler();
            return sizeof(mouse_event_t);
        }

        // No event available - check if we should block
        if (flags & O_NONBLOCK) {
            unlock_scheduler();
            return -EAGAIN;
        }

        // Block until an event arrives
        devfs_waiter_t *waiter = kernel_malloc(sizeof(*waiter));
        if (!waiter) {
            unlock_scheduler();
            return -ENOMEM;
        }
        waiter->task = current_task;
        waiter->next = NULL;
        // Store user buffer address for direct delivery when event arrives
        waiter->buffer = (char *)(uintptr_t)current_task->current_user_buf;
        waiter->buffer_size = size;

        // Add to wait queue
        if (wq->tail) {
            wq->tail->next = waiter;
            wq->tail = waiter;
        } else {
            wq->head = wq->tail = waiter;
        }

        current_task->state = PROCESS_STATE_BLOCKED;
        unlock_scheduler();
        task_yield(1);
        // Note: task_yield does not return here - when woken, the task
        // resumes in userspace with the result already set in eax
    }
}

void dev_mouse_push_event(mouse_event_t *event) {
    // Check if there's a waiting task - deliver event directly to it
    if (dev_mouse_event_node) {
        devfs_wait_queue_t *wq = devfs_get_wait_queue(dev_mouse_event_node);
        if (wq && wq->head) {
            devfs_waiter_t *waiter = wq->head;
            process_control_block_t *task = waiter->task;

            // Remove from queue
            wq->head = waiter->next;
            if (!wq->head) {
                wq->tail = NULL;
            }

            // Copy event directly to user space and set return value
            if (waiter->buffer && waiter->buffer_size >= sizeof(mouse_event_t)) {
                if (copy_to_user(task->address_space, (uint32_t)waiter->buffer, event, sizeof(mouse_event_t)) == 0) {
                    task->processor_context->eax = sizeof(mouse_event_t);
                } else {
                    task->processor_context->eax = (uint32_t)-EIO;
                }
            }

            kernel_free(waiter);
            task_unblock(task);
            return;
        }
    }

    // No waiting task - add event to ring buffer
    // Coalesce consecutive move events: only the latest position matters
    if (event->event_type == MOUSE_EVENT_MOVE && event_count > 0) {
        uint32_t last = (event_head + MOUSE_EVENT_BUFFER_SIZE - 1) % MOUSE_EVENT_BUFFER_SIZE;
        if (event_buffer[last].event_type == MOUSE_EVENT_MOVE) {
            memcpy(&event_buffer[last], event, sizeof(mouse_event_t));
            return;
        }
    }

    if (event_count >= MOUSE_EVENT_BUFFER_SIZE) {
        // Buffer full - drop oldest event
        event_tail = (event_tail + 1) % MOUSE_EVENT_BUFFER_SIZE;
        event_count--;
    }

    memcpy(&event_buffer[event_head], event, sizeof(mouse_event_t));
    event_head = (event_head + 1) % MOUSE_EVENT_BUFFER_SIZE;
    event_count++;
}

void dev_mouse_init(void) {
    // Register device files
    devfs_register_device("mouse/pos", S_IFREG | 0400, &dev_mouse_read_pos_ops, NULL);
    devfs_register_device("mouse/event", S_IFREG | 0400, &dev_mouse_event_ops, NULL);

    // Cache the event node for direct access from IRQ handler
    vfs_node_t *devfs_root = vfs_lookup_mount("/dev");
    if (devfs_root) {
        vfs_node_t *mouse_dir = devfs_finddir(devfs_root, "mouse");
        if (mouse_dir) {
            dev_mouse_event_node = devfs_finddir(mouse_dir, "event");
        }
    }
}
