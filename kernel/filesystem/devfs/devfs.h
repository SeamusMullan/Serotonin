/**
 * @file devfs.h
 * @brief Device filesystem (devfs) interface
 *
 * Provides a virtual filesystem for device access. Devices are registered
 * as files under /dev and can be accessed using standard file operations.
 * Supports blocking I/O through wait queues for devices that need it.
 */

#ifndef _FS_DEVFS
#define _FS_DEVFS

#include "../vfs.h"
#include "../../schedule/schedule.h"
#include "../../syscall/sys/types.h"
#include "../../syscall/sys/file.h"

/**
 * @brief Node in a device wait queue
 *
 * Represents a task waiting for data from a device. Contains the task's
 * PCB and optionally a user-space buffer for direct data delivery.
 */
typedef struct devfs_waiter {
    struct process_control_block *task;  /**< Blocked task */
    struct devfs_waiter *next;           /**< Next waiter in queue */
    char *buffer;                        /**< User-space buffer for direct delivery */
    uint32_t buffer_size;                /**< Size of the buffer */
} devfs_waiter_t;

/**
 * @brief Wait queue for blocking device I/O
 *
 * A FIFO queue of tasks waiting for device data. When data becomes
 * available, tasks are woken in order.
 */
typedef struct devfs_wait_queue {
    devfs_waiter_t *head;  /**< First waiter (next to wake) */
    devfs_waiter_t *tail;  /**< Last waiter (most recently added) */
} devfs_wait_queue_t;

/**
 * @brief Directory entry in devfs
 */
typedef struct devfs_dir_entry {
    vfs_node_t *node;
    struct devfs_dir_entry *next;
} devfs_dir_entry_t;

/**
 * @brief Directory data for devfs
 */
typedef struct devfs_dir {
    devfs_dir_entry_t *entries;
} devfs_dir_t;

/**
 * @brief Device file data
 *
 * Contains device-specific operations, permissions, and a wait queue
 * for blocking I/O support.
 */
typedef struct devfs_file {
    mode_t mode;                    /**< File permissions */
    vfs_ops_t *ops;                 /**< Device-specific operations */
    devfs_wait_queue_t wait_queue;  /**< Queue for blocked readers */
} devfs_file_t;

/**
 * @brief Initialize the devfs subsystem
 *
 * Registers devfs with the VFS layer. Must be called before mounting.
 */
void devfs_init(void);

/**
 * @brief Mount a devfs instance
 * @param device Unused (devfs is virtual)
 * @return Root node of the mounted filesystem, or NULL on failure
 */
vfs_node_t *devfs_mount(const char *device);

/* Standard VFS operations */
int devfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int devfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
int devfs_truncate(vfs_node_t *node, uint32_t size);
int devfs_open(vfs_node_t *node);
int devfs_close(vfs_node_t *node);
vfs_node_t *devfs_readdir(vfs_node_t *node, uint32_t index);
vfs_node_t *devfs_finddir(vfs_node_t *node, const char *name);

/**
 * @brief Register a device in devfs
 * @param path Path relative to /dev (e.g., "mouse/event")
 * @param mode File mode and permissions
 * @param ops Device-specific VFS operations
 * @return 0 on success, -1 on failure
 *
 * Creates intermediate directories as needed. If the device already
 * exists, updates its mode and operations.
 */
int devfs_register_device(const char *path, mode_t mode, vfs_ops_t *ops);

/**
 * @brief Initialize a wait queue
 * @param queue Queue to initialize
 */
void devfs_wait_queue_init(devfs_wait_queue_t *queue);

/**
 * @brief Add current task to a wait queue and block
 * @param queue Queue to wait on
 * @param task Task to enqueue
 * @return 0 on success, -1 on failure
 *
 * The task is blocked until woken by devfs_wait_wake_one/all.
 */
int devfs_wait_enqueue(devfs_wait_queue_t *queue, process_control_block_t *task);

/**
 * @brief Remove and return the first task from a wait queue
 * @param queue Queue to dequeue from
 * @return Task PCB, or NULL if queue is empty
 *
 * Does not wake the task - caller must do so.
 */
process_control_block_t *devfs_wait_dequeue(devfs_wait_queue_t *queue);

/**
 * @brief Wake the first task waiting on a queue
 * @param queue Queue to wake from
 */
void devfs_wait_wake_one(devfs_wait_queue_t *queue);

/**
 * @brief Wake all tasks waiting on a queue
 * @param queue Queue to wake from
 */
void devfs_wait_wake_all(devfs_wait_queue_t *queue);

/**
 * @brief Get the wait queue for a device file
 * @param node VFS node of the device
 * @return Pointer to wait queue, or NULL if not a device file
 */
devfs_wait_queue_t *devfs_get_wait_queue(vfs_node_t *node);

#endif
