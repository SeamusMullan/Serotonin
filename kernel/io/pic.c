#include <kernel/io/io.h>

#define PIT_FREQ 1000
#define PIT_DIVISOR (1193182 / PIT_FREQ)

/**
 * @brief Remap the PIC (Programmable Interrupt Controller) to new vector offsets.
 * 
 * @param offset1 The new offset for the master PIC.
 * @param offset2 The new offset for the slave PIC.
 */
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

    outb(0x43, 0x36);               // Command port
    outb(0x40,PIT_DIVISOR & 0xFF); // Channel 0 data port (low byte)
    outb(0x40,PIT_DIVISOR >> 8);   // Channel 0 data port (high byte)

    outb(0x21, inb(0x21) & ~(1 << 0)); 
    outb(0x21, inb(0x21) & ~(1 << 8)); 
    io_wait();
    //asm volatile ("sti");
}