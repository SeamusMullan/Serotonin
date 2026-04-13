/**
 * @file date.c
 * @brief Print current date and time for Serotonin OS
 *
 * Calls gettimeofday() and converts the epoch timestamp to
 * a human-readable UTC date string.
 */

#include <stdio.h>
#include <sys/time.h>

static void epoch_to_date(long epoch, int *year, int *month, int *day,
                          int *hour, int *min, int *sec) {
    long s = epoch;
    *sec  = (int)(s % 60); s /= 60;
    *min  = (int)(s % 60); s /= 60;
    *hour = (int)(s % 24); s /= 24;

    /* Days since 1970-01-01 */
    long days = s;
    int y = 1970;

    for (;;) {
        int yd = 365;
        if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) yd = 366;
        if (days < yd) break;
        days -= yd;
        y++;
    }
    *year = y;

    int leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0;
    int mdays[] = {31, 28 + leap, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int m = 0;
    while (m < 11 && days >= mdays[m]) {
        days -= mdays[m];
        m++;
    }
    *month = m + 1;
    *day = (int)days + 1;
}

int main(void) {
    struct timeval tv;

    if (gettimeofday(&tv, NULL) < 0) {
        printf("date: cannot get time of day\n");
        return 1;
    }

    int year, month, day, hour, min, sec;
    epoch_to_date((long)tv.tv_sec, &year, &month, &day, &hour, &min, &sec);

    printf("%04d-%02d-%02d %02d:%02d:%02d UTC\n",
           year, month, day, hour, min, sec);

    return 0;
}
