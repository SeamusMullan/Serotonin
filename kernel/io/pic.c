#include "io.h"

void pic_remap(int offset1, int offset2) {
    uint8_t a1 = inb(0x21);  // Save master mask
    uint8_t a2 = inb(0xA1);  // Save slave mask

    outb(0x20, 0x11);        // Start init
    io_wait();
    outb(0xA0, 0x11);
    io_wait();

    outb(0x21, offset1);     // Master PIC vector offset (0x20)
    io_wait();
    outb(0xA1, offset2);     // Slave PIC vector offset (0x28)
    io_wait();

    outb(0x21, 0x04);        // Tell master PIC slave is at IRQ2
    io_wait();
    outb(0xA1, 0x02);        // Tell slave its cascade ID
    io_wait();

    outb(0x21, 0x01);        // 8086 mode
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    outb(0x21, a1);          // Restore saved masks
    outb(0xA1, a2);
}