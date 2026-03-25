#include <kernel/device/rtl8139/dev_rtl8139.h>
#include <kernel/device/rtl8139/rtl8139.h>
#include <kernel/kernel.h>
#include <kernel/filesystem/devfs/devfs.h>
#include <kernel/filesystem/vfs.h>
#include <kernel/io/io.h>
#include <kernel/stdio/stdio.h>
#include <kernel/stdlib/stdlib.h>
#include <kernel/string.h>
#include <kernel/syscall/sys/file.h>
#include <kernel/syscall/sys/errno.h>
#include <kernel/schedule/schedule.h>
#include <kernel/vmm/vmm.h>

static rtl8139_packet_t tun_buffer[RTL8139_TUN_BUFFER_SIZE];
static volatile uint32_t tun_head = 0;
static volatile uint32_t tun_tail = 0;
static volatile uint32_t tun_count = 0;

static vfs_node_t *dev_rtl8139_tun_node = NULL;

int dev_rtl8139_read_tun(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)offset;

    if (!node || !buffer) return -1;
    if (size < sizeof(rtl8139_packet_t)) return -EINVAL;

    devfs_wait_queue_t *wq = devfs_get_wait_queue(node);
    if (!wq) return -1;

    uint32_t flags = current_task->current_fd_flags;

    lock_scheduler();

    if (tun_count > 0) {
        rtl8139_packet_t *pkt = &tun_buffer[tun_tail];
        memcpy(buffer, pkt, sizeof(rtl8139_packet_t));
        tun_tail = (tun_tail + 1) % RTL8139_TUN_BUFFER_SIZE;
        tun_count--;
        current_task->processor_context->eax = sizeof(rtl8139_packet_t);
        unlock_scheduler();
        return sizeof(rtl8139_packet_t);
    }

    if (flags & O_NONBLOCK) {
        unlock_scheduler();
        return -EAGAIN;
    }

    devfs_waiter_t *waiter = kernel_malloc(sizeof(*waiter));
    if (!waiter) {
        unlock_scheduler();
        return -ENOMEM;
    }
    waiter->task = current_task;
    waiter->next = NULL;
    waiter->buffer = (char *)(uintptr_t)current_task->current_user_buf;
    waiter->buffer_size = size;

    if (wq->tail) {
        wq->tail->next = waiter;
        wq->tail = waiter;
    } else {
        wq->head = wq->tail = waiter;
    }

    current_task->processor_context->eax = (uint32_t)(-EINTR);

    current_task->state = PROCESS_STATE_BLOCKED;
    unlock_scheduler();
    task_yield(1);
}

int dev_rtl8139_write_tun(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    (void)node;
    (void)offset;

    if (!buffer || size < sizeof(rtl8139_packet_t)) return -EINVAL;
    if (!rtl8139_is_up()) return -ENODEV;

    const rtl8139_packet_t *pkt = (const rtl8139_packet_t *)buffer;

    if (pkt->length == 0 || pkt->length > ETH_FRAME_MAX) return -EINVAL;

    uint8_t frame[ETH_FRAME_MAX];
    uint16_t frame_len = 0;

    memcpy(frame, pkt->dst_mac, ETH_ALEN);
    frame_len += ETH_ALEN;

    static const uint8_t zero_mac[ETH_ALEN] = {0};
    if (memcmp(pkt->src_mac, zero_mac, ETH_ALEN) == 0) {
        uint8_t mac[ETH_ALEN];
        rtl8139_get_mac(mac);
        memcpy(frame + frame_len, mac, ETH_ALEN);
    } else {
        memcpy(frame + frame_len, pkt->src_mac, ETH_ALEN);
    }
    frame_len += ETH_ALEN;

    uint16_t payload_len = pkt->length;
    if (payload_len > ETH_FRAME_MAX - 12)
        payload_len = ETH_FRAME_MAX - 12;

    memcpy(frame + frame_len, pkt->data, payload_len);
    frame_len += payload_len;

    int ret = rtl8139_send(frame, frame_len);
    if (ret != 0) return -EIO;

    return (int)size;
}

void dev_rtl8139_push_packet(const uint8_t *data, uint16_t length) {
    if (length < 12) return;

    if (dev_rtl8139_tun_node) {
        devfs_wait_queue_t *wq = devfs_get_wait_queue(dev_rtl8139_tun_node);
        if (wq && wq->head) {
            devfs_waiter_t *waiter = wq->head;
            process_control_block_t *task = waiter->task;

            wq->head = waiter->next;
            if (!wq->head)
                wq->tail = NULL;

            if (waiter->buffer && waiter->buffer_size >= sizeof(rtl8139_packet_t)) {
                rtl8139_packet_t pkt;
                memset(&pkt, 0, sizeof(pkt));
                memcpy(pkt.dst_mac, data, ETH_ALEN);
                memcpy(pkt.src_mac, data + ETH_ALEN, ETH_ALEN);
                pkt.length = (length > 12) ? length - 12 : 0;
                if (pkt.length > ETH_FRAME_MAX)
                    pkt.length = ETH_FRAME_MAX;
                if (pkt.length > 0)
                    memcpy(pkt.data, data + 12, pkt.length);

                if (copy_to_user(task->address_space, (uint32_t)waiter->buffer, &pkt, sizeof(rtl8139_packet_t)) == 0) {
                    task->processor_context->eax = sizeof(rtl8139_packet_t);
                } else {
                    task->processor_context->eax = (uint32_t)-EIO;
                }
            }

            kernel_free(waiter);
            task_unblock(task);
            return;
        }
    }

    if (tun_count >= RTL8139_TUN_BUFFER_SIZE) {
        tun_tail = (tun_tail + 1) % RTL8139_TUN_BUFFER_SIZE;
        tun_count--;
    }

    rtl8139_packet_t *slot = &tun_buffer[tun_head];
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->dst_mac, data, ETH_ALEN);
    memcpy(slot->src_mac, data + ETH_ALEN, ETH_ALEN);
    slot->length = (length > 12) ? length - 12 : 0;
    if (slot->length > ETH_FRAME_MAX)
        slot->length = ETH_FRAME_MAX;
    if (slot->length > 0)
        memcpy(slot->data, data + 12, slot->length);

    tun_head = (tun_head + 1) % RTL8139_TUN_BUFFER_SIZE;
    tun_count++;
}

static int dev_rtl8139_read_mac(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)node;
    if (offset >= ETH_ALEN) return 0;
    uint8_t mac[ETH_ALEN];
    rtl8139_get_mac(mac);
    uint32_t remaining = ETH_ALEN - offset;
    uint32_t count = size < remaining ? size : remaining;
    memcpy(buffer, mac + offset, count);
    return (int)count;
}

static int dev_rtl8139_poll_tun(vfs_node_t *node) {
    (void)node;
    int revents = POLLOUT;  /* write never blocks */
    if (tun_count > 0)
        revents |= POLLIN;
    return revents;
}

static vfs_ops_t dev_rtl8139_tun_ops = {
    .read    = dev_rtl8139_read_tun,
    .write   = dev_rtl8139_write_tun,
    .truncate = NULL, .unlink = NULL, .rmdir = NULL,
    .open = NULL, .close = NULL,
    .readdir = NULL, .finddir = NULL,
    .create = NULL, .mkdir = NULL,
    .poll = dev_rtl8139_poll_tun
};

static vfs_ops_t dev_rtl8139_mac_ops = {
    .read    = dev_rtl8139_read_mac,
    .write   = NULL,
    .truncate = NULL, .unlink = NULL, .rmdir = NULL,
    .open = NULL, .close = NULL,
    .readdir = NULL, .finddir = NULL,
    .create = NULL, .mkdir = NULL,
    .poll = NULL
};

void dev_rtl8139_init(void) {
    devfs_register_device("rtl8139/tun", S_IFREG | 0600, &dev_rtl8139_tun_ops, NULL);
    devfs_register_device("rtl8139/mac", S_IFREG | 0444, &dev_rtl8139_mac_ops, NULL);

    vfs_node_t *devfs_root = vfs_lookup_mount("/dev");
    if (devfs_root) {
        vfs_node_t *rtl_dir = devfs_finddir(devfs_root, "rtl8139");
        if (rtl_dir)
            dev_rtl8139_tun_node = devfs_finddir(rtl_dir, "tun");
    }

    rtl8139_set_rx_callback(dev_rtl8139_push_packet);
}
