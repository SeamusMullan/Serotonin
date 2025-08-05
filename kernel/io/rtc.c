#include "io.h"

void rtc_init() {
    clear_interrupts();
    
    outb(CMOS_STATUS_REGISTER_A, CMOS_RTC_STATUS_B);
    uint8_t prev = inb(CMOS_STATUS_REGISTER_B);
    outb(CMOS_STATUS_REGISTER_A, CMOS_RTC_STATUS_B);
    outb(CMOS_STATUS_REGISTER_B, prev | 0x10); // enable update-ended interrupts
}