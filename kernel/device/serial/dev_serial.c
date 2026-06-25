#include <kernel/device/serial/dev_serial.h>
#include <kernel/kernel.h>
#include <kernel/filesystem/devfs/devfs.h>
#include <kernel/filesystem/vfs.h>
#include <kernel/io/io.h>
#include <kernel/io/serial.h>
#include <kernel/stdio/stdio.h>
#include <kernel/stdlib/stdlib.h>
#include <kernel/string.h>
#include <kernel/syscall/sys/file.h>
#include <kernel/syscall/sys/errno.h>
#include <kernel/schedule/schedule.h>
#include <kernel/vmm/vmm.h>

// recv ring buf
static char rx_ring[SERIAL_RING_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;
static volatile uint32_t rx_count = 0;

static vfs_node_t *dev_serial_node = NULL;

static vfs_ops_t dev_serial_ops = {
    .read    = dev_serial_read,
    .write   = dev_serial_write,
    .truncate = NULL,
    .unlink  = NULL,
    .rmdir   = NULL,
    .open    = NULL,
    .close   = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create  = NULL,
    .mkdir   = NULL,
    .poll    = dev_serial_poll,
};

int dev_serial_poll(vfs_node_t *node) {
    (void)node;
    int revents = POLLOUT;
    if (rx_count > 0)
        revents |= POLLIN;
    return revents;
}

int dev_serial_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    devfs_wait_queue_t *wq = devfs_get_wait_queue(node);
    if (!wq) return -EIO;

    uint32_t flags = current_task->current_fd_flags;

    while (1) {
        lock_scheduler();

        if (rx_count > 0) {
            uint32_t to_read = size < rx_count ? size : rx_count;
            for (uint32_t i = 0; i < to_read; i++) {
                buffer[i] = rx_ring[rx_tail];
                rx_tail = (rx_tail + 1) % SERIAL_RING_SIZE;
            }
            rx_count -= to_read;
            current_task->processor_context->eax = to_read;
            unlock_scheduler();
            return (int)to_read;
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

        current_task->state = PROCESS_STATE_BLOCKED;
        unlock_scheduler();
        task_yield(1);
        __builtin_unreachable();
    }
}

int dev_serial_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    (void)node;
    (void)offset;
    if (!buffer || size == 0)
        return 0;

    for (uint32_t i = 0; i < size; i++) {
        serial_putchar(COM1_BASE, buffer[i]);
    }
    return (int)size;
}

static void dev_serial_deliver_byte(char c) {
    // direct delivery to a blocked reader if there is one
    if (dev_serial_node) {
        devfs_wait_queue_t *wq = devfs_get_wait_queue(dev_serial_node);
        if (wq && wq->head) {
            devfs_waiter_t *waiter = wq->head;
            process_control_block_t *task = waiter->task;

            wq->head = waiter->next;
            if (!wq->head)
                wq->tail = NULL;

            if (waiter->buffer && waiter->buffer_size >= 1) {
                if (copy_to_user(task->address_space, (uint32_t)waiter->buffer, &c, 1) == 0) {
                    task->processor_context->eax = 1;
                } else {
                    task->processor_context->eax = (uint32_t)-EIO;
                }
            }

            kernel_free(waiter);
            task_unblock(task);
            return;
        }
    }

    // otherwise stash in the ring buffer, dropping oldest on overflow
    if (rx_count >= SERIAL_RING_SIZE) {
        rx_tail = (rx_tail + 1) % SERIAL_RING_SIZE;
        rx_count--;
    }
    rx_ring[rx_head] = c;
    rx_head = (rx_head + 1) % SERIAL_RING_SIZE;
    rx_count++;
}

// this is irq4
//
// Drive the handler off the IIR (Interrupt Identification Register) instead
// of just LSR.DR. A 16550 keeps its IRQ line asserted until *every* pending
// cause is cleared; if we exit while any cause is still pending (RLS error
// bits, THRE, modem-status, or a CTI with an empty FIFO race), the UART
// never re-edges the PIC and RX goes permanently deaf. That manifested as
// serial dying after a handful of seconds.
void dev_serial_irq_handler(void) {
    // Loop until IIR reports "no interrupt pending" (bit 0 == 1).
    // Cap iterations so a stuck UART can't wedge the kernel here.
    for (int i = 0; i < 64; i++) {
        uint8_t iir = inb(COM1_BASE + SERIAL_INT_ID);
        if (iir & 0x01)
            break;  // no interrupt pending

        switch (iir & 0x0E) {
            case 0x06:  // Receiver Line Status - reading LSR clears it
                (void)inb(COM1_BASE + SERIAL_LINE_STATUS);
                break;
            case 0x04:  // Received Data Available
            case 0x0C:  // Character Timeout Indication
                // Drain the RX FIFO until LSR.DR clears.
                while (inb(COM1_BASE + SERIAL_LINE_STATUS) & 0x01) {
                    char c = (char)inb(COM1_BASE + SERIAL_RECV_BUFFER);
                    dev_serial_deliver_byte(c);
                }
                break;
            case 0x02:  // Transmitter Holding Register Empty - reading IIR already cleared it
                break;
            case 0x00:  // Modem Status - reading MSR clears it
                (void)inb(COM1_BASE + 6);
                break;
            default:
                // Unknown cause; read LSR to make a best-effort clear.
                (void)inb(COM1_BASE + SERIAL_LINE_STATUS);
                break;
        }
    }
}

void dev_serial_init(void) {
    devfs_register_device("ttyS0", S_IFCHR | 0660, &dev_serial_ops, NULL);

    vfs_node_t *devfs_root = vfs_lookup_mount("/dev");
    if (devfs_root) {
        dev_serial_node = devfs_finddir(devfs_root, "ttyS0");
    }

    printfs(PRINT_STATUS_INFO, "serial: /dev/ttyS0 registered (COM1)\n");
}
