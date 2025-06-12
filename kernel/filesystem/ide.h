#ifndef _IDE_H
#define _IDE_H

#include <stdint.h>

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

#define ATA_MASTER 0xE0
#define ATA_SLAVE  0xF0

#define ATA_CMD_IDENTIFY       0xEC
#define ATA_CMD_READ_SECTORS   0x20
#define ATA_CMD_WRITE_SECTORS  0x30
#define ATA_CMD_CACHE_FLUSH   0xE7

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

void ide_init(void);

int ide_read_sector(uint8_t drive, uint32_t lba, uint8_t *buffer);
int ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t *buffer);
int ide_wait(uint8_t mask, uint8_t value, int timeout);
int ide_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buffer);
int ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t *buffer);

#endif