/**
 * @file dev_keyboard.h
 * @brief PS/2 keyboard device driver interface
 *
 * Provides keyboard input through a device file:
 * - /dev/keyboard/event: Keyboard events (blocking/non-blocking)
 *
 * Key events are delivered as keyboard_event_t structures. Reads can be
 * blocking (wait for event) or non-blocking (return EAGAIN if no event).
 */

#ifndef _DEVICE_KEYBOARD_H
#define _DEVICE_KEYBOARD_H

#include "../../filesystem/devfs/devfs.h"
#include "../../filesystem/vfs.h"
#include <stdint.h>

/** Size of the internal event ring buffer */
#define KEYBOARD_EVENT_BUFFER_SIZE 64

/** @name Keyboard event flags
 * @{
 */
#define KEY_FLAG_RELEASED (1 << 0)  /**< Key was released (vs pressed) */
#define KEY_FLAG_SHIFT    (1 << 1)  /**< Shift was held */
#define KEY_FLAG_CTRL     (1 << 2)  /**< Ctrl was held */
/** @} */

/**
 * @brief Keyboard event structure
 *
 * Represents a key press or release. The structure is packed to
 * ensure consistent layout between kernel and userspace.
 */
typedef struct keyboard_event {
    uint8_t scancode;    /**< Raw PS/2 scancode (without release bit) */
    uint8_t ascii;       /**< Translated ASCII character (0 if none) */
    uint8_t flags;       /**< Event flags (KEY_FLAG_*) */
    uint8_t _pad;        /**< Padding for alignment */
} __attribute__((packed)) keyboard_event_t;

/**
 * @brief Read a keyboard event
 * @param node VFS node
 * @param offset Unused
 * @param size Must be >= sizeof(keyboard_event_t)
 * @param buffer Output buffer for keyboard_event_t
 * @return sizeof(keyboard_event_t) on success, negative on error
 *
 * Behavior depends on O_NONBLOCK flag:
 * - Blocking: Waits until an event is available
 * - Non-blocking: Returns -EAGAIN if no event available
 */
int dev_keyboard_read_event(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);

/**
 * @brief Initialize the keyboard device driver
 *
 * Registers /dev/keyboard/event device.
 * Must be called after devfs is mounted.
 */
void dev_keyboard_init(void);

/**
 * @brief Push a keyboard event to the event queue
 * @param event Event to push
 *
 * Called by the scancode handler on key press/release. If a task is
 * waiting for an event, it is delivered directly and the task is woken.
 * Otherwise, the event is added to the ring buffer.
 */
void dev_keyboard_push_event(keyboard_event_t *event);

/** VFS node for /dev/keyboard/event, used for direct event delivery */
extern vfs_node_t *dev_keyboard_event_node;

#endif
