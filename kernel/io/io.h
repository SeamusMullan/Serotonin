#ifndef _KERNEL_IO
#define _KERNEL_IO

#include <stdint.h>

extern volatile uint64_t timer_ticks;

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ( "inb %w1, %b0"
                   : "=a"(ret)
                   : "Nd"(port)
                   : "memory");
    return ret;
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

void irq_handler(int irq);
void pic_remap(int offset1, int offset2);
void handle_scancode(uint8_t scancode);

#endif