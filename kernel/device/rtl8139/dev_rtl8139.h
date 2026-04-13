#ifndef _DEV_RTL8139_H
#define _DEV_RTL8139_H

#include <kernel/device/rtl8139/rtl8139.h>
#include <kernel/filesystem/devfs/devfs.h>
#include <kernel/filesystem/vfs.h>
#include <stdint.h>

#define RTL8139_TUN_BUFFER_SIZE 4096

typedef struct rtl8139_packet {
    uint8_t  src_mac[ETH_ALEN];
    uint8_t  dst_mac[ETH_ALEN];
    uint16_t length;
    uint8_t  data[ETH_FRAME_MAX];
} __attribute__((packed)) rtl8139_packet_t;

int dev_rtl8139_read_tun(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int dev_rtl8139_write_tun(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);

void dev_rtl8139_init(void);
void dev_rtl8139_push_packet(const uint8_t *data, uint16_t length);

#endif
