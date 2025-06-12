#include "ide.h"
#include "../io/io.h"
#include "../stdio/stdio.h"

// Wait until (status & mask) == value, or timeout
int ide_wait(uint8_t mask, uint8_t value, int timeout) {
    while (timeout-- > 0) {
        uint8_t status = inb(ATA_PRIMARY_IO + 7);
        if ((status & mask) == value) return 0;
    }
    return -1; // timeout
}

void ide_init(void) {
    for (uint8_t drive = 0; drive <= 1; drive++) {
        // Select drive
        uint8_t drive_select = (drive == 0) ? ATA_MASTER : ATA_SLAVE;
        outb(ATA_PRIMARY_IO + 6, drive_select);
        io_wait();

        // Clear registers
        outb(ATA_PRIMARY_IO + 1, 0);
        outb(ATA_PRIMARY_IO + 2, 0);
        outb(ATA_PRIMARY_IO + 3, 0);
        outb(ATA_PRIMARY_IO + 4, 0);
        outb(ATA_PRIMARY_IO + 5, 0);

        // Send IDENTIFY
        outb(ATA_PRIMARY_IO + 7, ATA_CMD_IDENTIFY);
        io_wait();

        uint8_t status = inb(ATA_PRIMARY_IO + 7);
        if (status == 0) {
            printfs(PRINT_STATUS_WARNING,"IDE drive %u: No device.\n", drive);
            continue;
        }

        // Wait for BSY = 0
        if (ide_wait(ATA_STATUS_BSY, 0, 100000) != 0) {
            printfs(PRINT_STATUS_WARNING,"IDE drive %u: BSY timeout.\n", drive);
            continue;
        }

        // Check for ATAPI device
        uint8_t cl = inb(ATA_PRIMARY_IO + 4);
        uint8_t ch = inb(ATA_PRIMARY_IO + 5);

        if (cl == 0x14 && ch == 0xEB) {
            printfs(PRINT_STATUS_DEBUG,"IDE drive %u: ATAPI device detected (CDROM).\n", drive);
            continue;
        }

        // Wait for DRQ = 1
        if (ide_wait(ATA_STATUS_DRQ, ATA_STATUS_DRQ, 100000) != 0) {
            printfs(PRINT_STATUS_WARNING,"IDE drive %u: DRQ timeout.\n", drive);
            continue;
        }

        // Read IDENTIFY data
        uint16_t identify_data[256];
        for (int i = 0; i < 256; i++) {
            identify_data[i] = inw(ATA_PRIMARY_IO);
        }

        // Print model string
        char model[41];
        for (int i = 0; i < 20; i++) {
            model[i * 2 + 0] = (identify_data[27 + i] >> 8) & 0xFF;
            model[i * 2 + 1] = identify_data[27 + i] & 0xFF;
        }
        model[40] = '\0';

        printfs(PRINT_STATUS_DEBUG,"IDE drive %u detected. Model: %s\n", drive, model);
    }
}

int ide_read_sector(uint8_t drive, uint32_t lba, uint8_t *buffer) {
    if (drive > 1) return -1;

    uint8_t drive_select = (drive == 0) ? ATA_MASTER : ATA_SLAVE;

    // Select drive
    outb(ATA_PRIMARY_IO + 6, drive_select | ((lba >> 24) & 0x0F));
    io_wait();

    // Setup sector count + LBA
    outb(ATA_PRIMARY_IO + 1, 0x00);
    outb(ATA_PRIMARY_IO + 2, 1); // 1 sector

    outb(ATA_PRIMARY_IO + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + 5, (uint8_t)((lba >> 16) & 0xFF));

    // Send READ command
    outb(ATA_PRIMARY_IO + 7, ATA_CMD_READ_SECTORS);
    io_wait();

    // Wait for BSY = 0
    if (ide_wait(ATA_STATUS_BSY, 0, 100000) != 0) {
        printfs(PRINT_STATUS_WARNING,"IDE: BSY timeout!\n");
        return -1;
    }

    // Wait for DRQ = 1
    if (ide_wait(ATA_STATUS_DRQ, ATA_STATUS_DRQ, 100000) != 0) {
        printfs(PRINT_STATUS_WARNING,"IDE: DRQ timeout!\n");
        return -1;
    }

    // Read 512 bytes (256 words)
    for (int i = 0; i < 256; i++) {
        uint16_t data = inw(ATA_PRIMARY_IO);
        buffer[i * 2 + 0] = data & 0xFF;
        buffer[i * 2 + 1] = (data >> 8) & 0xFF;
    }

    return 0; // success
}

int ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t *buffer) {
    for (uint8_t i = 0; i < count; i++) {
        int res = ide_read_sector(drive, lba + i, buffer + (i * 512));
        if (res != 0) return res;
    }
    return 0;
}

int ide_write_sector(uint8_t drive, uint32_t lba, const uint8_t *buffer) {
    if (drive > 1) return -1;

    uint8_t drive_sel = (drive == 0 ? ATA_MASTER : ATA_SLAVE) | ((lba >> 24) & 0x0F);

    // 1: select drive
    outb(ATA_PRIMARY_IO + 6, drive_sel);
    io_wait();

    // 2: setup count + LBA registers
    outb(ATA_PRIMARY_IO + 1, 0x00);      // features
    outb(ATA_PRIMARY_IO + 2, 1);         // sector count = 1
    outb(ATA_PRIMARY_IO + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + 4, (uint8_t)((lba>>8) & 0xFF));
    outb(ATA_PRIMARY_IO + 5, (uint8_t)((lba>>16)&0xFF));

    // 3: send WRITE SECTORS
    outb(ATA_PRIMARY_IO + 7, ATA_CMD_WRITE_SECTORS);
    io_wait();

    // 4: wait BSY=0
    if (ide_wait(0x80, 0, 100000) != 0) {
        printfs(PRINT_STATUS_WARNING,"IDE write: BSY timeout\n");
        return -1;
    }

    // 5: wait DRQ=1
    if (ide_wait(0x08, 0x08, 100000) != 0) {
        printfs(PRINT_STATUS_WARNING,"IDE write: DRQ timeout\n");
        return -1;
    }

    // 6: write 256 words (512 bytes)
    for (int i = 0; i < 256; i++) {
        uint16_t word = (uint16_t)buffer[i*2] 
                      | ((uint16_t)buffer[i*2+1] << 8);
        outw(ATA_PRIMARY_IO, word);
    }

    // 7: flush cache? some drives need CACHE FLUSH command (0xE7)
    //    but for now we’ll assume the BIOS flushes on close.

    return 0;
}

int ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t *buffer) {
    for (uint8_t i = 0; i < count; i++) {
        if (ide_write_sector(drive, lba + i, buffer + (i * 512)) != 0)
            return -1;
    }
    return 0;
}

