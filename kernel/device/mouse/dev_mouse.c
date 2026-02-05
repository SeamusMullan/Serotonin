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

static mouse_event_t event_buffer[MOUSE_EVENT_BUFFER_SIZE];
static volatile uint32_t event_head = 0;
static volatile uint32_t event_tail = 0;
static volatile uint32_t event_count = 0;

vfs_node_t *dev_mouse_event_node = NULL;

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
    char mouse_pos[32];
    snprintf(mouse_pos, 31,"%d,%d",mouse_x,mouse_y);
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

    printfs(PRINT_STATUS_DEBUG, "dev_mouse_read_event: sizeof(mouse_event_t)=%d, size=%d\n",
            (int)sizeof(mouse_event_t), (int)size);

    if (!node || !buffer) return -1;
    if (size < sizeof(mouse_event_t)) return -EINVAL;

    devfs_wait_queue_t *wq = devfs_get_wait_queue(node);
    if (!wq) {
        printfs(PRINT_STATUS_DEBUG, "dev_mouse_read_event: wq is NULL, returning -1\n");
        return -1;
    }

    uint32_t flags = current_task->current_fd_flags;
    printfs(PRINT_STATUS_DEBUG, "dev_mouse_read_event: flags=0x%x, event_count=%d\n", flags, event_count);

    while (1) {
        lock_scheduler();

        printfs(PRINT_STATUS_DEBUG, "dev_mouse_read_event: in loop, event_count=%d\n", event_count);

        if (event_count > 0) {
            mouse_event_t *ev = &event_buffer[event_tail];
            memcpy(buffer, ev, sizeof(mouse_event_t));
            event_tail = (event_tail + 1) % MOUSE_EVENT_BUFFER_SIZE;
            event_count--;
            current_task->processor_context->eax = sizeof(mouse_event_t);
            unlock_scheduler();
            printfs(PRINT_STATUS_DEBUG, "dev_mouse_read_event: returning %d\n", (int)sizeof(mouse_event_t));
            return sizeof(mouse_event_t);
        }

        if (flags & O_NONBLOCK) {
            printfs(PRINT_STATUS_DEBUG, "dev_mouse_read_event: non-blocking, returning -EAGAIN\n");
            unlock_scheduler();
            return -EAGAIN;
        }

        printfs(PRINT_STATUS_DEBUG, "dev_mouse_read_event: blocking, adding to wait queue\n");

        devfs_waiter_t *waiter = kernel_malloc(sizeof(*waiter));
        if (!waiter) {
            printfs(PRINT_STATUS_DEBUG, "dev_mouse_read_event: malloc failed, returning -ENOMEM\n");
            unlock_scheduler();
            return -ENOMEM;
        }
        waiter->task = current_task;
        waiter->next = NULL;

        if (wq->tail) {
            wq->tail->next = waiter;
            wq->tail = waiter;
        } else {
            wq->head = wq->tail = waiter;
        }

        current_task->state = PROCESS_STATE_BLOCKED;
        unlock_scheduler();
        task_yield(1);
    }
}

void dev_mouse_push_event(mouse_event_t *event) {
    printfs(PRINT_STATUS_DEBUG, "dev_mouse_push_event: adding event, event_count=%d\n", event_count);

    if (event_count >= MOUSE_EVENT_BUFFER_SIZE) {
        event_tail = (event_tail + 1) % MOUSE_EVENT_BUFFER_SIZE;
        event_count--;
    }

    memcpy(&event_buffer[event_head], event, sizeof(mouse_event_t));
    event_head = (event_head + 1) % MOUSE_EVENT_BUFFER_SIZE;
    event_count++;

    printfs(PRINT_STATUS_DEBUG, "dev_mouse_push_event: event added, event_count=%d\n", event_count);

    if (dev_mouse_event_node) {
        devfs_wait_queue_t *wq = devfs_get_wait_queue(dev_mouse_event_node);
        if (wq && wq->head) {
            printfs(PRINT_STATUS_DEBUG, "dev_mouse_push_event: waking task %s\n", wq->head->task->name);
            devfs_waiter_t *waiter = wq->head;
            process_control_block_t *task = waiter->task;
            wq->head = waiter->next;
            if (!wq->head) {
                wq->tail = NULL;
            }
            kernel_free(waiter);
            task_unblock(task);
        } else {
            printfs(PRINT_STATUS_DEBUG, "dev_mouse_push_event: no waiters\n");
        }
    }
}

void dev_mouse_init(void) {
    devfs_register_device("mouse/pos", S_IFREG | 0400, &dev_mouse_read_pos_ops);
    devfs_register_device("mouse/event", S_IFREG | 0400, &dev_mouse_event_ops);

    vfs_node_t *devfs_root = vfs_lookup_mount("/dev");
    if (devfs_root) {
        vfs_node_t *mouse_dir = devfs_finddir(devfs_root, "mouse");
        if (mouse_dir) {
            dev_mouse_event_node = devfs_finddir(mouse_dir, "event");
        }
    }
}
