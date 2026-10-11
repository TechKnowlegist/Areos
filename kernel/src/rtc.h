// rtc.h - the real-time clock: today's date and time.
//
// PCs have a little battery-powered clock chip (the "CMOS RTC") that
// keeps time even when the computer is off. QEMU fills it with your
// PC's local time.

#ifndef AREOS_RTC_H
#define AREOS_RTC_H

#include <stdint.h>

struct date_time {
    uint16_t year;
    uint8_t month;    // 1-12
    uint8_t day;      // 1-31
    uint8_t hour;     // 0-23
    uint8_t minute;
    uint8_t second;
};

struct date_time rtc_now(void);

#endif
