#ifndef _DEVICE_IDE_PCI_H
#define _DEVICE_IDE_PCI_H

#include <stdint.h>
#include <kernel/schedule/schedule.h>
#include <kernel/vmm/vmm.h>

#define ATA_CMD_IDENTIFY       0xEC
#define ATA_CMD_READ_SECTORS   0x20
#define ATA_CMD_WRITE_SECTORS  0x30
#define ATA_CMD_CACHE_FLUSH    0xE7
#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_SRV 0x10
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_RDY 0x40
#define ATA_STATUS_BSY 0x80
#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_FEATURES 1
#define ATA_REG_SECCOUNT 2
#define ATA_REG_LBA_LO 3
#define ATA_REG_LBA_MID 4
#define ATA_REG_LBA_HI 5
#define ATA_REG_DRIVE_HEAD 6
#define ATA_REG_STATUS 7
#define ATA_REG_COMMAND 7
#define ATA_REG_ALT_STATUS 0
#define ATA_REG_DEV_CTRL 0
#define ATA_DRIVE_MASTER 0xE0
#define ATA_DRIVE_SLAVE 0xF0
#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_SECONDARY_IO 0x170
#define ATA_SECONDARY_CTRL 0x376
#define ATA_PROGIF_PRIMARY_NATIVE 0x01
#define ATA_PROGIF_SECONDARY_NATIVE 0x04
#define IDE_MAX_DRIVES 4
#define IDE_WORK_QUEUE_SIZE 64
#define IDE_WORK_READ  0
#define IDE_WORK_WRITE 1
#define BMIDE_REG_CMD 0
#define BMIDE_REG_STATUS 2
#define BMIDE_REG_PRDT 4
#define BMIDE_CMD_START 0x01
#define BMIDE_CMD_READ 0x08
#define BMIDE_STATUS_ACTIVE 0x01
#define BMIDE_STATUS_ERR 0x02
#define BMIDE_STATUS_IRQ 0x04
#define ATA_CMD_READ_DMA 0xC8
#define ATA_CMD_WRITE_DMA 0xCA

typedef struct ide_channel {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint16_t bmide_base;
    uint8_t  irq;
    uint8_t  selected_drive;
} ide_channel_t;

typedef struct ide_drive {
    uint8_t  present;
    uint8_t  channel;
    uint8_t  drive;
    uint8_t  type;
    uint32_t size;
    char     model[41];
} ide_drive_t;

typedef struct disk_work {
    struct process_control_block *task;
    struct file_handle           *handle;
    struct address_space         *addr_space;
    uint32_t user_buf;
    uint32_t count;
    uint8_t  direction;
    char    *kbuf;
    uint32_t flags;
} disk_work_t;

typedef struct {
    uint32_t phys_addr;
    uint16_t byte_count;
    uint16_t flags;
} __attribute__((packed)) prd_entry_t;

void ide_init(void);
int ide_read_sector(uint8_t drive, uint32_t lba, uint8_t *buffer);
int ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t *buffer);
int ide_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buffer);
int ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t *buffer);
int ide_wait(uint8_t mask, uint8_t value, int timeout);
void ide_start_worker(void);
void ide_submit_disk_read(process_control_block_t *task, file_handle_t *handle, address_space_t *as, uint32_t user_buf, uint32_t count, uint32_t flags);
void ide_submit_disk_write(process_control_block_t *task, file_handle_t *handle, address_space_t *as, uint32_t user_buf, uint32_t count, char *kbuf, uint32_t flags);
int ide_cache_flush(uint8_t drive);

#endif
