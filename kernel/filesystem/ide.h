#ifndef _IDE_H
#define _IDE_H

#include <stdint.h>

void ide_init(void);

int ide_read_sector(uint8_t drive, uint32_t lba, uint8_t *buffer);
int ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t *buffer);

#endif