#ifndef _DEVICE_MOUSE_H
#define _DEVICE_MOUSE_H

#include "../../filesystem/devfs/devfs.h"
#include "../../filesystem/vfs.h"
#include <stdint.h>

#define MOUSE_EVENT_BUFFER_SIZE 64

#define MOUSE_BTN_LEFT   (1 << 0)
#define MOUSE_BTN_RIGHT  (1 << 1)
#define MOUSE_BTN_MIDDLE (1 << 2)

typedef struct mouse_event {
    int16_t x;
    int16_t y;
    uint8_t buttons;
    uint8_t event_type;
} __attribute__((packed)) mouse_event_t;

enum {
    MOUSE_EVENT_MOVE = 0,
    MOUSE_EVENT_BUTTON_DOWN = 1,
    MOUSE_EVENT_BUTTON_UP = 2
};

int dev_mouse_read_pos(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int dev_mouse_read_event(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
void dev_mouse_init(void);
void dev_mouse_push_event(mouse_event_t *event);

extern vfs_node_t *dev_mouse_event_node;

#endif
