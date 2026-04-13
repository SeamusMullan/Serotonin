#include <kernel/io/io.h>

const int days_in_month[12] = {
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
};


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
    outb(CMOS_STATUS_REGISTER_B, prev | 0x40); // enable update-ended interrupts
}

int is_leap_year(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

int days_before_month(int month, int year) {
    int days = 0;
    for (int i = 0; i < month - 1; i++) {
        days += days_in_month[i];
        if (i == 1 && is_leap_year(year)) {
            days += 1; // february in leap year
        }
    }
    return days;
}

int rtc_to_unix_timestamp(rtc_time_t *t) {
    int year = t->century * 100 + t->year;

    // days since 1970-01-01
    int days = 0;

    // add days for each full year
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    // add days for months in the current year
    days += days_before_month(t->month, year);

    // add days in current month (subtract 1 because day starts at 1)
    days += t->day - 1;

    // convert days to seconds, and add time of day
    int timestamp = ((uint64_t)days * 86400) + ((uint64_t)t->hour * 3600) + ((uint64_t)t->minute * 60) + (uint64_t)t->second;

    return timestamp;
}