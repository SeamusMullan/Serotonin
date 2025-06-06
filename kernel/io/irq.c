#include "io.h"
#include "../stdio/stdio.h"

volatile uint64_t timer_ticks = 0;

void irq_handler(int irq) {
    printfs(PRINT_STATUS_DEBUG,"IRQ %d received\n", irq);

    if (irq == 0) {
        timer_ticks++;
    } else if (irq == 1) {
        printf("[KEYBOARD]\n");
    }

    if (irq >= 8)
        outb(0xA0, 0x20);  // EOI to slave PIC
    outb(0x20, 0x20);      // EOI to master PIC
}
