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
    asm volatile ( "inb %w1, %b0"
                   : "=a"(ret)
                   : "Nd"(port)
                   : "memory");
    return ret;
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}


void irq_handler(int irq);
void pic_remap(int offset1, int offset2);
void handle_scancode(uint8_t scancode);

#define MILLISECONDS_TO_TICKS(ms) (ms)

#endif