#ifndef _KERNEL_IO
#define _KERNEL_IO

#include <stdint.h>

#define MILLISECONDS_TO_TICKS(ms) (ms)
#define STDIO_INPUT_BUFFER 1024
#define SCHEDULE_QUANTUM MILLISECONDS_TO_TICKS(100)
#define IRQ_PIT 0
#define IRQ_KEYBOARD  1
#define IRQ_RTC 8
#define IRQ_MOUSE 12
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
#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_GET_COMPAQ_STATUS 0x20
#define PS2_SET_COMPAQ_STATUS 0x60
#define PS2_ENABLE_AUX_DEVICE 0xA8
#define PS2_MOUSE_BYTE 0xD4
#define PS2_MOUSE_RESET 0xFF
#define PS2_MOUSE_RESEND 0xFE
#define PS2_MOUSE_SET_DEFAULTS 0xF6
#define PS2_MOUSE_DISABLE_PACKET_STREAMING 0xF5
#define PS2_MOUSE_ENABLE_PACKET_STREAMING 0xF4
#define PS2_MOUSE_SET_SAMPLE_RATE 0xF3
#define PS2_MOUSE_GET_MOUSEID 0xF2
#define PS2_MOUSE_RQ_SINGLE_PACKET 0xEB
#define PS2_MOUSE_STATUS_RQ 0xE9
#define PS2_MOUSE_SET_RESOLUTION 0xE8
#define PS2_MOUSE_ACK 0xFA
#define PS2_MOUSE_SELFTEST_GOOD 0xAA
#define PS2_SEND_BYTE 0xD4

/**
 * @brief Processor context structure.
 *
 * This structure holds the context of a processor, including all general-purpose registers
 * and segment registers.
 */
typedef struct processor_context {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_at_pushal, ebx, edx, ecx, eax;
    uint32_t stub_eflags;
    uint32_t eip, cs, eflags, esp_at_trap, ss;
} processor_context_t;

/**
 * @brief Real-time clock (RTC) time structure.
 *
 * This structure holds the time information from the RTC.
 */
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint16_t century;
} rtc_time_t;

extern volatile uint64_t timer_ticks;
extern volatile uint64_t last_quantum_tick;
extern volatile int multitasking_ready;
extern volatile int irq_disabled;
extern volatile rtc_time_t last_rtc_time;

/**
 * @brief Output a byte to a port.
 *
 * @param port The port number.
 * @param val The value to output.
 */
static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * @brief Input a byte from a port.
 *
 * @param port The port number.
 * @return uint8_t The value read from the port.
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile ( "inb %w1, %b0"
                   : "=a"(ret)
                   : "Nd"(port)
                   : "memory");
    return ret;
}

/**
 * @brief Wait for I/O operations to complete.
 *
 * This function waits for I/O operations to complete by reading from the
 * specified port.
 */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

/**
 * @brief Input a word from a port.
 *
 * @param port The port number.
 * @return uint16_t The value read from the port.
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * @brief Output a word to a port.
 *
 * @param port The port number.
 * @param val The value to output.
 */
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * @brief Clear interrupts.
 *
 * This function disables interrupts by incrementing the IRQ disabled counter.
 */
static inline void clear_interrupts(void) {
    if (irq_disabled == 0) {
        irq_disabled++;
        asm volatile ("cli");
    }
}

/**
 * @brief Enable interrupts.
 *
 * This function enables interrupts by decrementing the IRQ disabled counter.
 */
static inline void enable_interrupts(void) {
    if (irq_disabled == 1) {
        irq_disabled--;
        asm volatile ("sti");
    }
}

/**
 * @brief Read a byte from the CMOS.
 *
 * @param reg The CMOS register to read from.
 * @return uint8_t The value read from the CMOS register.
 */
static inline uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_STATUS_REGISTER_A, reg);
    return inb(CMOS_STATUS_REGISTER_B);
}

/**
 * @brief Convert BCD to binary.
 *
 * @param val The BCD value to convert.
 * @return uint8_t The converted binary value.
 */
static inline uint8_t bcd_to_bin(uint8_t val) {
    return (val & 0x0F) + ((val >> 4) * 10);
}

static inline void io_wait_input_clear() {
    while (inb(PS2_STATUS_PORT) & 0x02);
}

static inline void io_wait_output_full() {
    while (!(inb(PS2_STATUS_PORT) & 0x01));
}

/**
 * @brief Handle an IRQ.
 *
 * @param irq The IRQ number.
 * @param ctx The processor context.
 */
void irq_handler(int irq, processor_context_t *ctx);
void pic_remap(int offset1, int offset2);
void handle_scancode(uint8_t scancode);
void rtc_init(void);
void ps2_mouse_init(void);

#endif