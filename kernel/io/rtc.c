#include "io.h"

/**
 * @brief Initialize the RTC.
 *
 * This function initializes the real-time clock (RTC) by configuring the
 * necessary registers and enabling interrupts.
 */
void rtc_init() {
    clear_interrupts();
    
    outb(CMOS_STATUS_REGISTER_A, CMOS_RTC_STATUS_B);
    uint8_t prev = inb(CMOS_STATUS_REGISTER_B);
    outb(CMOS_STATUS_REGISTER_A, CMOS_RTC_STATUS_B);
    outb(CMOS_STATUS_REGISTER_B, prev | 0x10); // enable update-ended interrupts
}