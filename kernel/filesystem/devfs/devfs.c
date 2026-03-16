/**
 * @file devfs.c
 * @brief Device filesystem implementation
 *
 * Implements a virtual filesystem for device access. Devices register
 * themselves and provide custom read/write operations. Supports blocking
 * I/O through wait queues for devices like input devices.
 */

#include "devfs.h"
#include "../vfs.h"
#include "../../kernel.h"
#include "../../stdlib/stdlib.h"
#include "../../string.h"
#include "../../schedule/schedule.h"
#include "../../syscall/sys/file.h"

/** Root node of the mounted devfs */
static vfs_node_t *devfs_root = NULL;

/** VFS operations for devfs nodes */
static vfs_ops_t devfs_ops = {
    .read = devfs_read,
    .write = devfs_write,
    .truncate = devfs_truncate,
    .open = devfs_open,
    .close = devfs_close,
    .readdir = devfs_readdir,
    .finddir = devfs_finddir,
    .unlink = NULL,
    .rmdir = NULL,
    .create = NULL,
    .mkdir = NULL
};

/** Filesystem descriptor for VFS registration */
static filesystem_t devfs_fs = {
    .name = "devfs",
    .mount = devfs_mount,
    .next = NULL
};

/**
 * @brief Check if mode allows read access
 */
static int devfs_mode_allows_read(mode_t mode) {
    return (mode & 0444) != 0;
}

/**
 * @brief Check if mode allows write access
 */
static int devfs_mode_allows_write(mode_t mode) {
    return (mode & 0222) != 0;
}

/**
 * @brief Create a new directory node
 * @param name Name of the directory
 * @return New directory node, or NULL on failure
 */
static vfs_node_t *devfs_create_dir_node(const char *name) {
    vfs_node_t *dir_node = kernel_malloc(sizeof(*dir_node));
    if (!dir_node) return NULL;
    memset(dir_node, 0, sizeof(*dir_node));

    strncpy(dir_node->name, name, sizeof(dir_node->name));
    dir_node->inode = (uint32_t)dir_node;
    dir_node->flags = VFS_FLAG_DIRECTORY;
    dir_node->size = 0;
    dir_node->refcount = 1;
    dir_node->ops = &devfs_ops;
    dir_node->uid = 0;
    dir_node->gid = 0;
    dir_node->mode = S_IFDIR | 0755;

    devfs_dir_t *dir_data = kernel_malloc(sizeof(*dir_data));
    if (!dir_data) {
        kernel_free(dir_node);
        return NULL;
    }
    memset(dir_data, 0, sizeof(*dir_data));
    dir_node->fs_data = dir_data;

    return dir_node;
}

/**
 * @brief Create a new device file node
 * @param name Name of the device
 * @param mode File mode and permissions
 * @param ops Device-specific operations
 * @return New file node, or NULL on failure
 */
static vfs_node_t *devfs_create_file_node(const char *name, mode_t mode, vfs_ops_t *ops) {
    vfs_node_t *file_node = kernel_malloc(sizeof(*file_node));
    if (!file_node) return NULL;
    memset(file_node, 0, sizeof(*file_node));

    strncpy(file_node->name, name, sizeof(file_node->name));
    file_node->inode = (uint32_t)file_node;
    file_node->flags = VFS_FLAG_FILE;
    file_node->size = 0;
    file_node->refcount = 1;
    file_node->ops = &devfs_ops;
    file_node->uid = 0;
    file_node->gid = 0;
    file_node->mode = mode;

    devfs_file_t *file_data = kernel_malloc(sizeof(*file_data));
    if (!file_data) {
        kernel_free(file_node);
        return NULL;
    }
    file_data->mode = mode;
    file_data->ops = ops;
    file_data->wait_queue.head = NULL;
    file_data->wait_queue.tail = NULL;
    file_node->fs_data = file_data;

    return file_node;
}

/**
 * @brief Find a child node by name
 */
static vfs_node_t *devfs_find_child(devfs_dir_t *dir, const char *name) {
    devfs_dir_entry_t *entry = dir->entries;
    while (entry) {
        if (strcmp(entry->node->name, name) == 0) {
            return entry->node;
        }
        entry = entry->next;
    }
    return NULL;
}

/**
 * @brief Add a child node to a directory
 */
static int devfs_add_child(devfs_dir_t *dir, vfs_node_t *child) {
    devfs_dir_entry_t *entry = kernel_malloc(sizeof(*entry));
    if (!entry) return -1;
    entry->node = child;
    entry->next = dir->entries;
    dir->entries = entry;
    return 0;
}

void devfs_init(void) {
    vfs_register_fs(&devfs_fs);
}

vfs_node_t *devfs_mount(const char *device) {
    (void)device;

    vfs_node_t *root = devfs_create_dir_node("/");
    if (!root) return NULL;
    devfs_root = root;
    return root;
}

int devfs_open(vfs_node_t *node) {
    if (!node || !(node->flags & VFS_FLAG_FILE)) {
        return 0;
    }

    devfs_file_t *file = (devfs_file_t *)node->fs_data;
    if (!file || !file->ops || !file->ops->open) {
        return 0;
    }
    return file->ops->open(node);
}

int devfs_close(vfs_node_t *node) {
    if (!node || !(node->flags & VFS_FLAG_FILE)) {
        return 0;
    }

    devfs_file_t *file = (devfs_file_t *)node->fs_data;
    if (!file || !file->ops || !file->ops->close) {
        return 0;
    }
    return file->ops->close(node);
}

int devfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    if (!node || !(node->flags & VFS_FLAG_FILE)) return -1;

    devfs_file_t *file = (devfs_file_t *)node->fs_data;
    if (!file || !file->ops || !file->ops->read) return -1;
    if (!devfs_mode_allows_read(file->mode)) return -1;

    return file->ops->read(node, offset, size, buffer);
}

int devfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    if (!node || !(node->flags & VFS_FLAG_FILE)) return -1;

    devfs_file_t *file = (devfs_file_t *)node->fs_data;
    if (!file || !file->ops || !file->ops->write) return -1;
    if (!devfs_mode_allows_write(file->mode)) return -1;

    return file->ops->write(node, offset, size, buffer);
}

int devfs_truncate(vfs_node_t *node, uint32_t size) {
    if (!node || !(node->flags & VFS_FLAG_FILE)) return -1;

    devfs_file_t *file = (devfs_file_t *)node->fs_data;
    if (!file || !file->ops || !file->ops->truncate) return -1;
    if (!devfs_mode_allows_write(file->mode)) return -1;

    return file->ops->truncate(node, size);
}

vfs_node_t *devfs_readdir(vfs_node_t *node, uint32_t index) {
    if (!node || !(node->flags & VFS_FLAG_DIRECTORY)) return NULL;

    devfs_dir_t *dir = (devfs_dir_t *)node->fs_data;
    if (!dir) return NULL;

    devfs_dir_entry_t *entry = dir->entries;
    uint32_t i = 0;
    while (entry) {
        if (i == index) {
            return entry->node;
        }
        entry = entry->next;
        i++;
    }

    return NULL;
}

vfs_node_t *devfs_finddir(vfs_node_t *node, const char *name) {
    if (!node || !(node->flags & VFS_FLAG_DIRECTORY)) return NULL;
    if (!name || name[0] == '\0') return NULL;

    devfs_dir_t *dir = (devfs_dir_t *)node->fs_data;
    if (!dir) return NULL;

    return devfs_find_child(dir, name);
}

int devfs_register_device(const char *path, mode_t mode, vfs_ops_t *ops, void *device_data) {
    if (!devfs_root || !path) return -1;

    // Skip leading slashes
    const char *start = path;
    while (*start == '/') start++;
    if (*start == '\0') return -1;

    // Work with a copy for tokenization
    char temp[256];
    strncpy(temp, start, sizeof(temp));
    temp[sizeof(temp) - 1] = '\0';

    // Walk the path, creating directories as needed
    vfs_node_t *current = devfs_root;
    char *token = strtok(temp, "/");
    while (token) {
        char *next = strtok(NULL, "/");
        int is_last = (next == NULL);

        if (!(current->flags & VFS_FLAG_DIRECTORY)) {
            return -1;
        }

        devfs_dir_t *dir = (devfs_dir_t *)current->fs_data;
        if (!dir) return -1;

        vfs_node_t *child = devfs_find_child(dir, token);
        if (!child) {
            // Create new node
            if (is_last && ((mode & S_IFMT) == S_IFDIR)) {
                child = devfs_create_dir_node(token);
            } else if (is_last) {
                child = devfs_create_file_node(token, mode, ops);
                if (child && device_data) {
                    devfs_file_t *f = (devfs_file_t *)child->fs_data;
                    if (f) f->device_data = device_data;
                }
            } else {
                child = devfs_create_dir_node(token);
            }

            if (!child) return -1;

            if (devfs_add_child(dir, child) != 0) {
                kernel_free(child->fs_data);
                kernel_free(child);
                return -1;
            }
            child->parent = current;
        } else if (is_last && (child->flags & VFS_FLAG_FILE)) {
            // Update existing device
            devfs_file_t *file = (devfs_file_t *)child->fs_data;
            if (!file) return -1;
            file->mode = mode;
            file->ops = ops;
            file->device_data = device_data;
            devfs_wait_queue_init(&file->wait_queue);
        } else if (is_last && ((mode & S_IFMT) == S_IFDIR)) {
            if (!(child->flags & VFS_FLAG_DIRECTORY)) {
                return -1;
            }
        } else if (!is_last && !(child->flags & VFS_FLAG_DIRECTORY)) {
            return -1;
        }

        current = child;
        token = next;
    }

    return 0;
}

/*
 * Wait queue operations for blocking device I/O
 */

void devfs_wait_queue_init(devfs_wait_queue_t *queue) {
    if (!queue) return;
    queue->head = NULL;
    queue->tail = NULL;
}

int devfs_wait_enqueue(devfs_wait_queue_t *queue, process_control_block_t *task) {
    if (!queue || !task) return -1;

    devfs_waiter_t *node = kernel_malloc(sizeof(*node));
    if (!node) return -1;

    node->task = task;
    node->next = NULL;
    node->buffer = NULL;
    node->buffer_size = 0;

    lock_scheduler();
    if (queue->tail) {
        queue->tail->next = node;
        queue->tail = node;
    } else {
        queue->head = queue->tail = node;
    }

    task->state = PROCESS_STATE_BLOCKED;
    unlock_scheduler();

    task_yield(1);
    return 0;
}

process_control_block_t *devfs_wait_dequeue(devfs_wait_queue_t *queue) {
    if (!queue || !queue->head) return NULL;

    devfs_waiter_t *node = queue->head;
    process_control_block_t *task = node->task;

    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }

    kernel_free(node);
    return task;
}

void devfs_wait_wake_one(devfs_wait_queue_t *queue) {
    if (!queue) return;

    lock_scheduler();
    process_control_block_t *task = devfs_wait_dequeue(queue);
    if (task) {
        task_unblock(task);
    }
    unlock_scheduler();
}

void devfs_wait_wake_all(devfs_wait_queue_t *queue) {
    if (!queue) return;

    lock_scheduler();
    process_control_block_t *task;
    while ((task = devfs_wait_dequeue(queue)) != NULL) {
        task_unblock(task);
    }
    unlock_scheduler();
}

devfs_wait_queue_t *devfs_get_wait_queue(vfs_node_t *node) {
    if (!node || !(node->flags & VFS_FLAG_FILE)) return NULL;

    devfs_file_t *file = (devfs_file_t *)node->fs_data;
    if (!file) return NULL;

    return &file->wait_queue;
}
