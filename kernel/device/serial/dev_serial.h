#ifndef _DEVICE_SERIAL_H
#define _DEVICE_SERIAL_H

#include <kernel/filesystem/devfs/devfs.h>
#include <kernel/filesystem/vfs.h>
#include <stdint.h>

#define SERIAL_RING_SIZE 4096

void dev_serial_init(void);
void dev_serial_irq_handler(void);

int dev_serial_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int dev_serial_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
int dev_serial_poll(vfs_node_t *node);

#endif
