/**
 * @file dev_keyboard.c
 * @brief PS/2 keyboard device driver implementation
 *
 * Implements keyboard input device for reading key events.
 * Events are stored in a ring buffer and can be read with blocking
 * or non-blocking semantics. When a task blocks waiting for an event,
 * the event is delivered directly to user space via copy_to_user when
 * it becomes available.
 */

#include "dev_keyboard.h"
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

/** Ring buffer for keyboard events */
static keyboard_event_t event_buffer[KEYBOARD_EVENT_BUFFER_SIZE];
static volatile uint32_t event_head = 0;   /**< Next write position */
static volatile uint32_t event_tail = 0;   /**< Next read position */
static volatile uint32_t event_count = 0;  /**< Number of events in buffer */

/** VFS node for /dev/keyboard/event, used for wake queue access */
vfs_node_t *dev_keyboard_event_node = NULL;

static int dev_keyboard_poll(vfs_node_t *node) {
    (void)node;
    if (event_count > 0)
        return POLLIN;
    return 0;
}

/** VFS operations for /dev/keyboard/event */
static vfs_ops_t dev_keyboard_event_ops = {
    .read = dev_keyboard_read_event,
    .write = NULL,
    .truncate = NULL,
    .unlink = NULL,
    .rmdir = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .mkdir = NULL,
    .poll = dev_keyboard_poll
};

int dev_keyboard_read_event(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)offset;

    if (!node || !buffer) return -1;
    if (size < sizeof(keyboard_event_t)) return -EINVAL;

    devfs_wait_queue_t *wq = devfs_get_wait_queue(node);
    if (!wq) return -1;

    uint32_t flags = current_task->current_fd_flags;

    while (1) {
        lock_scheduler();

        // Check if an event is available in the buffer
        if (event_count > 0) {
            keyboard_event_t *ev = &event_buffer[event_tail];
            memcpy(buffer, ev, sizeof(keyboard_event_t));
            event_tail = (event_tail + 1) % KEYBOARD_EVENT_BUFFER_SIZE;
            event_count--;
            current_task->processor_context->eax = sizeof(keyboard_event_t);
            unlock_scheduler();
            return sizeof(keyboard_event_t);
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

void dev_keyboard_push_event(keyboard_event_t *event) {
    // Check if there's a waiting task - deliver event directly to it
    if (dev_keyboard_event_node) {
        devfs_wait_queue_t *wq = devfs_get_wait_queue(dev_keyboard_event_node);
        if (wq && wq->head) {
            devfs_waiter_t *waiter = wq->head;
            process_control_block_t *task = waiter->task;

            // Remove from queue
            wq->head = waiter->next;
            if (!wq->head) {
                wq->tail = NULL;
            }

            // Copy event directly to user space and set return value
            if (waiter->buffer && waiter->buffer_size >= sizeof(keyboard_event_t)) {
                if (copy_to_user(task->address_space, (uint32_t)waiter->buffer, event, sizeof(keyboard_event_t)) == 0) {
                    task->processor_context->eax = sizeof(keyboard_event_t);
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
    if (event_count >= KEYBOARD_EVENT_BUFFER_SIZE) {
        // Buffer full - drop oldest event
        event_tail = (event_tail + 1) % KEYBOARD_EVENT_BUFFER_SIZE;
        event_count--;
    }

    memcpy(&event_buffer[event_head], event, sizeof(keyboard_event_t));
    event_head = (event_head + 1) % KEYBOARD_EVENT_BUFFER_SIZE;
    event_count++;
}

void dev_keyboard_init(void) {
    // Register device file
    devfs_register_device("keyboard/event", S_IFREG | 0400, &dev_keyboard_event_ops, NULL);

    // Cache the event node for direct access from IRQ handler
    vfs_node_t *devfs_root = vfs_lookup_mount("/dev");
    if (devfs_root) {
        vfs_node_t *keyboard_dir = devfs_finddir(devfs_root, "keyboard");
        if (keyboard_dir) {
            dev_keyboard_event_node = devfs_finddir(keyboard_dir, "event");
        }
    }
}
