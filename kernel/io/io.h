#ifndef _KERNEL_IO
#define _KERNEL_IO

#include <stdint.h>

#define MILLISECONDS_TO_TICKS(ms) (ms)
#define STDIO_INPUT_BUFFER 1024
#define SCHEDULE_QUANTUM MILLISECONDS_TO_TICKS(100)
#define IRQ_PIT 0
#define IRQ_KEYBOARD  1
#define IRQ_RTC 8
#define CMOS_STATUS_REGISTER_A 0x70
#define CMOS_STATUS_REGISTER_B 0x71
#define CMOS_RTC_SECONDS 0x00
#define CMOS_RTC_MINUTES 0x02
#define CMOS_RTC_HOURS 0x04
#define CMOS_RTC_DAY 0x07
#define CMOS_RTC_MONTH 0x08
#define CMOS_RTC_YEAR 0x09
#define CMOS_RTC_STATUS_A 0x0A
#define CMOS_RTC_STATUS_B 0x0B
#define CMOS_RTC_STATUS_C 0x0C

typedef struct processor_context {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_at_pushal, ebx, edx, ecx, eax;
    uint32_t stub_eflags;
    uint32_t eip, cs, eflags, esp_at_trap, ss;
} processor_context_t;

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

extern volatile uint64_t timer_ticks;
extern volatile uint64_t last_quantum_tick;
extern volatile int multitasking_ready;
extern volatile int irq_disabled;
extern volatile rtc_time_t last_rtc_time;

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

static inline void clear_interrupts(void) {
    if (irq_disabled == 0) {
        irq_disabled++;
        asm volatile ("cli");
    }
}

static inline void enable_interrupts(void) {
    if (irq_disabled == 1) {
        irq_disabled--;
        asm volatile ("sti");
    }
}

static inline uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_STATUS_REGISTER_A, reg);
    return inb(CMOS_STATUS_REGISTER_B);
}

static inline uint8_t bcd_to_bin(uint8_t val) {
    return (val & 0x0F) + ((val >> 4) * 10);
}

void irq_handler(int irq, processor_context_t *ctx);
void pic_remap(int offset1, int offset2);
void handle_scancode(uint8_t scancode);
void rtc_init();

#endif