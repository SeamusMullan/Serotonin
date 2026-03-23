#include "ide_pci.h"
#include "../../io/io.h"
#include "../../io/pci/pci.h"
#include "../../stdio/stdio.h"
#include "../../kernel.h"
#include "../../stdlib/stdlib.h"
#include "../../schedule/schedule.h"
#include "../../vmm/paging_init.h"
#include "../../vmm/vmm.h"
#include "../../filesystem/vfs.h"
#include "../../syscall/sys/file.h"
#include "../../syscall/sys/errno.h"

static ide_channel_t channels[2];
static ide_drive_t drives[IDE_MAX_DRIVES];
static prd_entry_t *prdt[2];
static uint32_t prdt_phys[2];
static uint8_t *dma_buf[2];
static uint32_t dma_buf_phys[2];
static uint8_t dma_capable[2];
static disk_work_t work_queue[IDE_WORK_QUEUE_SIZE];
static volatile uint32_t work_head;
static volatile uint32_t work_tail;
static lock_semaphore_t work_count;

static inline void ide_select_drive(ide_channel_t *ch, uint8_t drv, uint32_t lba) {
    uint8_t sel = (drv ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER) | ((lba >> 24) & 0x0F);
    outb(ch->io_base + ATA_REG_DRIVE_HEAD, sel);
    inb(ch->ctrl_base + ATA_REG_ALT_STATUS);
    inb(ch->ctrl_base + ATA_REG_ALT_STATUS);
    inb(ch->ctrl_base + ATA_REG_ALT_STATUS);
    inb(ch->ctrl_base + ATA_REG_ALT_STATUS);
    ch->selected_drive = drv;
}

int ide_wait(uint8_t mask, uint8_t value, int timeout) {
    uint16_t port = channels[0].io_base + ATA_REG_STATUS;
    while (timeout-- > 0) {
        uint8_t status = inb(port);
        if ((status & mask) == value) return 0;
    }
    return -1;
}

static int ide_poll(ide_channel_t *ch) {
    for (int i = 0; i < 500000; i++) {
        uint8_t s = inb(ch->io_base + ATA_REG_STATUS);
        if (!(s & ATA_STATUS_BSY)) {
            if (s & ATA_STATUS_ERR) return -1;
            if (s & ATA_STATUS_DF)  return -1;
            if (s & ATA_STATUS_DRQ) return 0;
        }
    }
    return -1;
}

static int ide_poll_bsy(ide_channel_t *ch) {
    for (int i = 0; i < 500000; i++) {
        uint8_t s = inb(ch->io_base + ATA_REG_STATUS);
        if (!(s & ATA_STATUS_BSY)) {
            if (s & ATA_STATUS_ERR) return -1;
            if (s & ATA_STATUS_DF)  return -1;
            return 0;
        }
    }
    return -1;
}

static int ide_pio_read_sector(ide_channel_t *ch, uint8_t drv, uint32_t lba, uint8_t *buf) {
    ide_select_drive(ch, drv, lba);

    outb(ch->io_base + ATA_REG_FEATURES, 0x00);
    outb(ch->io_base + ATA_REG_SECCOUNT, 1);
    outb(ch->io_base + ATA_REG_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(ch->io_base + ATA_REG_COMMAND,  ATA_CMD_READ_SECTORS);

    if (ide_poll(ch) != 0) return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t w = inw(ch->io_base + ATA_REG_DATA);
        buf[i * 2 + 0] = w & 0xFF;
        buf[i * 2 + 1] = (w >> 8) & 0xFF;
    }

    return 0;
}

static int ide_pio_write_sector(ide_channel_t *ch, uint8_t drv, uint32_t lba, const uint8_t *buf) {
    ide_select_drive(ch, drv, lba);

    outb(ch->io_base + ATA_REG_FEATURES, 0x00);
    outb(ch->io_base + ATA_REG_SECCOUNT, 1);
    outb(ch->io_base + ATA_REG_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(ch->io_base + ATA_REG_COMMAND,  ATA_CMD_WRITE_SECTORS);

    if (ide_poll(ch) != 0) return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t w = buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
        outw(ch->io_base + ATA_REG_DATA, w);
    }

    if (ide_poll_bsy(ch) != 0) return -1;

    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ide_poll_bsy(ch);

    return 0;
}

static int ide_dma_read_sector(ide_channel_t *ch, uint8_t ch_idx, uint8_t drv, uint32_t lba, uint8_t *buf) {
    uint16_t bmide = ch->bmide_base;

    // stop dma
    outb(bmide + BMIDE_REG_CMD, 0);
    outb(bmide + BMIDE_REG_STATUS, inb(bmide + BMIDE_REG_STATUS) | BMIDE_STATUS_ERR | BMIDE_STATUS_IRQ);

    // build prdt
    prdt[ch_idx][0].phys_addr  = dma_buf_phys[ch_idx];
    prdt[ch_idx][0].byte_count = 512;
    prdt[ch_idx][0].flags      = 0x8000;

    // load prdt
    outl(bmide + BMIDE_REG_PRDT, prdt_phys[ch_idx]);

    // dir: read
    outb(bmide + BMIDE_REG_CMD, BMIDE_CMD_READ);

    // sel drive and set up lba
    ide_select_drive(ch, drv, lba);
    outb(ch->io_base + ATA_REG_FEATURES, 0x00);
    outb(ch->io_base + ATA_REG_SECCOUNT, 1);
    outb(ch->io_base + ATA_REG_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

    // read dma command
    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_READ_DMA);

    // start bus master
    outb(bmide + BMIDE_REG_CMD, BMIDE_CMD_START | BMIDE_CMD_READ);

    // wait for completion
    for (int i = 0; i < 1000000; i++) {
        uint8_t bm_status = inb(bmide + BMIDE_REG_STATUS);
        if (bm_status & BMIDE_STATUS_ERR) {
            outb(bmide + BMIDE_REG_CMD, 0);
            outb(bmide + BMIDE_REG_STATUS, bm_status);
            return -1;
        }
        if (bm_status & BMIDE_STATUS_IRQ) {
            // dma complete
            outb(bmide + BMIDE_REG_CMD, 0);
            outb(bmide + BMIDE_REG_STATUS, bm_status);

            inb(ch->io_base + ATA_REG_STATUS);

            memcpy(buf, dma_buf[ch_idx], 512);
            return 0;
        }
    }

    // timeout
    outb(bmide + BMIDE_REG_CMD, 0);
    return -1;
}

static int ide_dma_write_sector(ide_channel_t *ch, uint8_t ch_idx, uint8_t drv, uint32_t lba, const uint8_t *buf) {
    uint16_t bmide = ch->bmide_base;

    // stop dma
    outb(bmide + BMIDE_REG_CMD, 0);
    outb(bmide + BMIDE_REG_STATUS, inb(bmide + BMIDE_REG_STATUS) | BMIDE_STATUS_ERR | BMIDE_STATUS_IRQ);

    memcpy(dma_buf[ch_idx], buf, 512);

    // build prdt
    prdt[ch_idx][0].phys_addr  = dma_buf_phys[ch_idx];
    prdt[ch_idx][0].byte_count = 512;
    prdt[ch_idx][0].flags      = 0x8000;

    // load prdt
    outl(bmide + BMIDE_REG_PRDT, prdt_phys[ch_idx]);

    // dir: write
    outb(bmide + BMIDE_REG_CMD, 0);

    // sel drive and set up lba
    ide_select_drive(ch, drv, lba);
    outb(ch->io_base + ATA_REG_FEATURES, 0x00);
    outb(ch->io_base + ATA_REG_SECCOUNT, 1);
    outb(ch->io_base + ATA_REG_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

    // write dma command
    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_DMA);

    // start bus master
    outb(bmide + BMIDE_REG_CMD, BMIDE_CMD_START);

    // wait for completion
    for (int i = 0; i < 1000000; i++) {
        uint8_t bm_status = inb(bmide + BMIDE_REG_STATUS);
        if (bm_status & BMIDE_STATUS_ERR) {
            outb(bmide + BMIDE_REG_CMD, 0);
            outb(bmide + BMIDE_REG_STATUS, bm_status);
            return -1;
        }
        if (bm_status & BMIDE_STATUS_IRQ) {
            // dma complete
            outb(bmide + BMIDE_REG_CMD, 0);
            outb(bmide + BMIDE_REG_STATUS, bm_status);
            inb(ch->io_base + ATA_REG_STATUS);

            // flush write cache
            outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
            ide_poll_bsy(ch);
            return 0;
        }
    }

    outb(bmide + BMIDE_REG_CMD, 0);
    return -1;
}

int ide_read_sector(uint8_t drive, uint32_t lba, uint8_t *buffer) {
    if (drive >= IDE_MAX_DRIVES || !drives[drive].present)
        return -1;
    ide_drive_t *drv = &drives[drive];
    ide_channel_t *ch = &channels[drv->channel];

    if (dma_capable[drv->channel])
        return ide_dma_read_sector(ch, drv->channel, drv->drive, lba, buffer);
    return ide_pio_read_sector(ch, drv->drive, lba, buffer);
}

int ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t *buffer) {
    for (uint8_t i = 0; i < count; i++) {
        int r = ide_read_sector(drive, lba + i, buffer + (i * 512));
        if (r != 0) return r;
    }
    return 0;
}

int ide_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buffer) {
    if (drive >= IDE_MAX_DRIVES || !drives[drive].present)
        return -1;
    ide_drive_t *drv = &drives[drive];
    ide_channel_t *ch = &channels[drv->channel];

    if (dma_capable[drv->channel])
        return ide_dma_write_sector(ch, drv->channel, drv->drive, lba, buffer);
    return ide_pio_write_sector(ch, drv->drive, lba, buffer);
}

int ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t *buffer) {
    for (uint8_t i = 0; i < count; i++) {
        int r = ide_write_sector(drive, lba + i, buffer + (i * 512));
        if (r != 0) return r;
    }
    return 0;
}

static void ide_identify_drive(uint8_t ch_idx, uint8_t drv_idx) {
    ide_channel_t *ch = &channels[ch_idx];
    uint8_t drive_idx = ch_idx * 2 + drv_idx;
    ide_drive_t *drv = &drives[drive_idx];
    drv->present = 0;
    drv->channel = ch_idx;
    drv->drive   = drv_idx;

    uint8_t sel = drv_idx ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER;
    outb(ch->io_base + ATA_REG_DRIVE_HEAD, sel);
    io_wait();

    outb(ch->io_base + ATA_REG_SECCOUNT, 0);
    outb(ch->io_base + ATA_REG_LBA_LO,   0);
    outb(ch->io_base + ATA_REG_LBA_MID,  0);
    outb(ch->io_base + ATA_REG_LBA_HI,   0);

    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    io_wait();

    uint8_t status = inb(ch->io_base + ATA_REG_STATUS);
    if (status == 0) return;

    for (int i = 0; i < 100000; i++) {
        status = inb(ch->io_base + ATA_REG_STATUS);
        if (!(status & ATA_STATUS_BSY)) break;
    }
    if (status & ATA_STATUS_BSY) return;

    uint8_t cl = inb(ch->io_base + ATA_REG_LBA_MID);
    uint8_t cm = inb(ch->io_base + ATA_REG_LBA_HI);
    if (cl == 0x14 && cm == 0xEB) {
        drv->type = 1;
        return;
    }

    for (int i = 0; i < 100000; i++) {
        status = inb(ch->io_base + ATA_REG_STATUS);
        if (status & ATA_STATUS_DRQ) break;
        if (status & ATA_STATUS_ERR) return;
    }
    if (!(status & ATA_STATUS_DRQ)) return;

    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inw(ch->io_base + ATA_REG_DATA);

    for (int i = 0; i < 20; i++) {
        drv->model[i * 2 + 0] = (id[27 + i] >> 8) & 0xFF;
        drv->model[i * 2 + 1] = id[27 + i] & 0xFF;
    }
    drv->model[40] = '\0';

    drv->size = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    drv->present = 1;
    drv->type    = 0;

    printfs(PRINT_STATUS_INFO, "ide: ch%u drv%u: %s (%u sectors)\n",
            ch_idx, drv_idx, drv->model, drv->size);
}

static void ide_setup_channels(pci_function_t *fn) {
    uint8_t prog_if = fn->prog_if;

    if ((prog_if & ATA_PROGIF_PRIMARY_NATIVE) && fn->bar[0]) {
        channels[0].io_base   = pci_bar_address(fn, 0) & 0xFFFC;
        channels[0].ctrl_base = (pci_bar_address(fn, 1) & 0xFFFC) + 2;
    } else {
        channels[0].io_base   = ATA_PRIMARY_IO;
        channels[0].ctrl_base = ATA_PRIMARY_CTRL;
    }
    channels[0].irq = 14;

    if ((prog_if & ATA_PROGIF_SECONDARY_NATIVE) && fn->bar[2]) {
        channels[1].io_base   = pci_bar_address(fn, 2) & 0xFFFC;
        channels[1].ctrl_base = (pci_bar_address(fn, 3) & 0xFFFC) + 2;
    } else {
        channels[1].io_base   = ATA_SECONDARY_IO;
        channels[1].ctrl_base = ATA_SECONDARY_CTRL;
    }
    channels[1].irq = 15;

    if (fn->bar[4]) {
        uint16_t bmide = pci_bar_address(fn, 4) & 0xFFFC;
        channels[0].bmide_base = bmide;
        channels[1].bmide_base = bmide + 8;
    }

    printfs(PRINT_STATUS_INFO,
            "ide: primary  io=0x%x ctrl=0x%x bmide=0x%x\n",
            channels[0].io_base, channels[0].ctrl_base, channels[0].bmide_base);
    printfs(PRINT_STATUS_INFO,
            "ide: secondary io=0x%x ctrl=0x%x bmide=0x%x\n",
            channels[1].io_base, channels[1].ctrl_base, channels[1].bmide_base);
}

static void ide_setup_legacy(void) {
    channels[0].io_base   = ATA_PRIMARY_IO;
    channels[0].ctrl_base = ATA_PRIMARY_CTRL;
    channels[0].irq       = 14;
    channels[1].io_base   = ATA_SECONDARY_IO;
    channels[1].ctrl_base = ATA_SECONDARY_CTRL;
    channels[1].irq       = 15;

    printfs(PRINT_STATUS_WARNING, "ide: no PCI controller found, using legacy ports\n");
}

static void ide_init_dma(void) {
    for (int i = 0; i < 2; i++) {
        dma_capable[i] = 0;
        if (!channels[i].bmide_base)
            continue;

        // alloc prdt
        prdt[i] = (prd_entry_t *)kernel_malloc_align(16, sizeof(prd_entry_t) * 8);
        if (!prdt[i]) continue;
        prdt_phys[i] = virt_to_phys(prdt[i]);

        // alloc dma bounce buf
        dma_buf[i] = (uint8_t *)kernel_malloc_align(PAGE_SIZE, PAGE_SIZE);
        if (!dma_buf[i]) continue;
        dma_buf_phys[i] = virt_to_phys(dma_buf[i]);

        // clear nIEN
        outb(channels[i].ctrl_base + ATA_REG_DEV_CTRL, 0x00);

        dma_capable[i] = 1;
        printfs(PRINT_STATUS_INFO,
                "ide: ch%d DMA enabled (prdt=0x%x buf=0x%x)\n",
                i, prdt_phys[i], dma_buf_phys[i]);
    }
}

void ide_init(void) {
    memset(drives, 0, sizeof(drives));
    memset(channels, 0, sizeof(channels));
    work_head = 0;
    work_tail = 0;

    pci_function_t *fn = pci_find_class(PCI_CLASS_STORAGE, PCI_SUBCLASS_IDE);

    if (fn) {
        printfs(PRINT_STATUS_INFO,
                "ide: PCI controller found [%x:%x] prog_if=0x%x\n",
                fn->vendor_id, fn->device_id, fn->prog_if);

        pci_func_enable_io_space(fn);
        pci_func_enable_bus_mastering(fn);

        ide_setup_channels(fn);
    } else {
        ide_setup_legacy();
    }

    // disable irqs on both channels
    outb(channels[0].ctrl_base + ATA_REG_DEV_CTRL, 0x02);
    outb(channels[1].ctrl_base + ATA_REG_DEV_CTRL, 0x02);

    for (uint8_t ch = 0; ch < 2; ch++)
        for (uint8_t drv = 0; drv < 2; drv++)
            ide_identify_drive(ch, drv);

    ide_init_dma();

    task_semaphore_init(&work_count, 0);
}

static void ide_process_work(disk_work_t *w) {
    file_handle_t *handle = (file_handle_t *)w->handle;
    vfs_node_t *node = handle->node;

    if (w->direction == IDE_WORK_READ) {
        char *kbuf = (char *)kernel_malloc(w->count);
        if (!kbuf) {
            ((processor_context_t *)w->task->processor_context)->eax = (uint32_t)-EIO;
            return;
        }

        int read_bytes = vfs_read(node, handle->offset, w->count, kbuf);
        if (read_bytes < 0) {
            ((processor_context_t *)w->task->processor_context)->eax =
                (uint32_t)((read_bytes == -1) ? -EIO : read_bytes);
            kernel_free(kbuf);
            return;
        }

        if (read_bytes > 0) {
            if (copy_to_user(w->addr_space, w->user_buf, kbuf, (size_t)read_bytes) != 0) {
                ((processor_context_t *)w->task->processor_context)->eax = (uint32_t)-EFAULT;
                kernel_free(kbuf);
                return;
            }
            handle->offset += read_bytes;
        }

        ((processor_context_t *)w->task->processor_context)->eax = (uint32_t)read_bytes;
        kernel_free(kbuf);

    } else {
        if (w->flags & O_APPEND)
            handle->offset = node->size;

        int written = vfs_write(node, handle->offset, w->count, w->kbuf);
        if (written < 0) {
            ((processor_context_t *)w->task->processor_context)->eax =
                (uint32_t)((written == -1) ? -EIO : written);
        } else {
            handle->offset += written;
            ((processor_context_t *)w->task->processor_context)->eax = (uint32_t)written;
        }

        kernel_free(w->kbuf);
    }
}

static void ide_worker_thread(void) {
    for (;;) {
        task_semaphore_acquire(&work_count);

        lock_scheduler();
        uint32_t idx = work_head;
        work_head = (work_head + 1) % IDE_WORK_QUEUE_SIZE;
        unlock_scheduler();

        disk_work_t w = work_queue[idx];

        ide_process_work(&w);

        task_unblock((process_control_block_t *)w.task);
    }
}

static int ide_enqueue_work(disk_work_t *w) {
    lock_scheduler();

    uint32_t next_tail = (work_tail + 1) % IDE_WORK_QUEUE_SIZE;
    if (next_tail == work_head) {
        unlock_scheduler();
        return -1;
    }

    work_queue[work_tail] = *w;
    work_tail = next_tail;

    unlock_scheduler();

    task_semaphore_release(&work_count);
    return 0;
}

void ide_submit_disk_read(process_control_block_t *task, file_handle_t *handle, address_space_t *as, uint32_t user_buf, uint32_t count, uint32_t flags) {
    disk_work_t w;
    w.task       = (struct process_control_block *)task;
    w.handle     = (struct file_handle *)handle;
    w.addr_space = (struct address_space *)as;
    w.user_buf   = user_buf;
    w.count      = count;
    w.direction  = IDE_WORK_READ;
    w.kbuf       = NULL;
    w.flags      = flags;

    if (ide_enqueue_work(&w) != 0) {
        task->processor_context->eax = (uint32_t)-EIO;
        return;
    }

    task_block();
    __builtin_unreachable();
}

void ide_submit_disk_write(process_control_block_t *task, file_handle_t *handle, address_space_t *as, uint32_t user_buf, uint32_t count, char *kbuf, uint32_t flags) {
    disk_work_t w;
    w.task       = (struct process_control_block *)task;
    w.handle     = (struct file_handle *)handle;
    w.addr_space = (struct address_space *)as;
    w.user_buf   = user_buf;
    w.count      = count;
    w.direction  = IDE_WORK_WRITE;
    w.kbuf       = kbuf;
    w.flags      = flags;

    if (ide_enqueue_work(&w) != 0) {
        kernel_free(kbuf);
        task->processor_context->eax = (uint32_t)-EIO;
        return;
    }

    task_block();
    __builtin_unreachable();
}

void ide_start_worker(void) {
    process_control_block_t *worker = task_create(ide_worker_thread, "kernel: ide worker", CPU_KERNEL_MODE, 254);
    enqueue(worker);
    printfs(PRINT_STATUS_INFO, "ide: worker thread started\n");
}
