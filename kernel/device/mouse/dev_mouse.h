/**
 * @file dev_mouse.h
 * @brief PS/2 mouse device driver interface
 *
 * Provides mouse input through two device files:
 * - /dev/mouse/pos: Current mouse position as "x,y" text
 * - /dev/mouse/event: Mouse button events (blocking/non-blocking)
 *
 * Button events are delivered as mouse_event_t structures. Reads can be
 * blocking (wait for event) or non-blocking (return EAGAIN if no event).
 */

#ifndef _DEVICE_MOUSE_H
#define _DEVICE_MOUSE_H

#include "../../filesystem/devfs/devfs.h"
#include "../../filesystem/vfs.h"
#include <stdint.h>

/** Size of the internal event ring buffer */
#define MOUSE_EVENT_BUFFER_SIZE 64

/** @name Mouse button masks
 * @{
 */
#define MOUSE_BTN_LEFT   (1 << 0)  /**< Left button */
#define MOUSE_BTN_RIGHT  (1 << 1)  /**< Right button */
#define MOUSE_BTN_MIDDLE (1 << 2)  /**< Middle button */
/** @} */

/**
 * @brief Mouse event structure
 *
 * Represents a mouse button state change. The structure is packed to
 * ensure consistent layout between kernel and userspace.
 */
typedef struct mouse_event {
    int16_t x;          /**< X coordinate at time of event */
    int16_t y;          /**< Y coordinate at time of event */
    uint8_t buttons;    /**< Button state bitmask (MOUSE_BTN_*) */
    uint8_t event_type; /**< Event type (MOUSE_EVENT_*) */
} __attribute__((packed)) mouse_event_t;

/**
 * @brief Mouse event types
 */
enum {
    MOUSE_EVENT_MOVE = 0,        /**< Mouse moved (not currently generated) */
    MOUSE_EVENT_BUTTON_DOWN = 1, /**< Button was pressed */
    MOUSE_EVENT_BUTTON_UP = 2    /**< Button was released */
};

/**
 * @brief Read current mouse position as text
 * @param node VFS node (unused)
 * @param offset Byte offset to start reading from
 * @param size Maximum bytes to read
 * @param buffer Output buffer
 * @return Number of bytes read, or negative on error
 *
 * Returns position as "x,y" string (e.g., "123,456").
 */
int dev_mouse_read_pos(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);

/**
 * @brief Read a mouse button event
 * @param node VFS node
 * @param offset Unused
 * @param size Must be >= sizeof(mouse_event_t)
 * @param buffer Output buffer for mouse_event_t
 * @return sizeof(mouse_event_t) on success, negative on error
 *
 * Behavior depends on O_NONBLOCK flag:
 * - Blocking: Waits until an event is available
 * - Non-blocking: Returns -EAGAIN if no event available
 */
int dev_mouse_read_event(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);

/**
 * @brief Initialize the mouse device driver
 *
 * Registers /dev/mouse/pos and /dev/mouse/event devices.
 * Must be called after devfs is mounted.
 */
void dev_mouse_init(void);

/**
 * @brief Push a mouse event to the event queue
 * @param event Event to push
 *
 * Called by the IRQ handler when button state changes. If a task is
 * waiting for an event, it is delivered directly and the task is woken.
 * Otherwise, the event is added to the ring buffer.
 */
void dev_mouse_push_event(mouse_event_t *event);

/** VFS node for /dev/mouse/event, used for direct event delivery */
extern vfs_node_t *dev_mouse_event_node;

#endif
