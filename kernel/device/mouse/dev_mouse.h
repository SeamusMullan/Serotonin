#ifndef _DEVICE_MOUSE_H
#define _DEVICE_MOUSE_H

#include "../../filesystem/devfs/devfs.h"
#include "../../filesystem/vfs.h"

int dev_mouse_read_pos(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
void dev_mouse_init(void);

#endif
