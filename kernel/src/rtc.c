// rtc.c - the real-time clock.
//
// The clock chip is read one number at a time through two ports: write
// which number you want to port 0x70, read it from port 0x71. The chip
// may be mid-update when we read, so we read everything twice and only
// trust it when both readings match.

#include <stdbool.h>
#include "rtc.h"
#include "io.h"

#define CMOS_SELECT  0x70
#define CMOS_DATA    0x71

#define REG_SECOND   0x00
#define REG_MINUTE   0x02
#define REG_HOUR     0x04
#define REG_DAY      0x07
#define REG_MONTH    0x08
#define REG_YEAR     0x09
#define REG_STATUS_A 0x0A
#define REG_STATUS_B 0x0B

#define UPDATING        0x80  // status A: the chip is changing the time right now
#define BINARY_MODE     0x04  // status B: numbers are plain (not "BCD")
#define TWENTY_FOUR_HR  0x02  // status B: 24-hour clock
#define PM_BIT          0x80  // in 12-hour mode, the hour's top bit means PM

static uint8_t read_register(uint8_t reg) {
    outb(CMOS_SELECT, reg);
    return inb(CMOS_DATA);
}

static struct date_time read_raw(void) {
    while (read_register(REG_STATUS_A) & UPDATING) { }
    struct date_time t = {
        .second = read_register(REG_SECOND),
        .minute = read_register(REG_MINUTE),
        .hour   = read_register(REG_HOUR),
        .day    = read_register(REG_DAY),
        .month  = read_register(REG_MONTH),
        .year   = read_register(REG_YEAR),
    };
    return t;
}

static bool same(struct date_time a, struct date_time b) {
    return a.second == b.second && a.minute == b.minute && a.hour == b.hour
        && a.day == b.day && a.month == b.month && a.year == b.year;
}

// "BCD" stores each decimal digit in 4 bits: 0x59 means 59.
static uint8_t from_bcd(uint8_t value) {
    return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

struct date_time rtc_now(void) {
    struct date_time t = read_raw();
    struct date_time again = read_raw();
    while (!same(t, again)) {
        t = again;
        again = read_raw();
    }

    uint8_t status_b = read_register(REG_STATUS_B);
    bool pm = (t.hour & PM_BIT) != 0;
    t.hour &= (uint8_t)~PM_BIT;

    if (!(status_b & BINARY_MODE)) {
        t.second = from_bcd(t.second);
        t.minute = from_bcd(t.minute);
        t.hour   = from_bcd(t.hour);
        t.day    = from_bcd(t.day);
        t.month  = from_bcd(t.month);
        t.year   = from_bcd((uint8_t)t.year);
    }
    if (!(status_b & TWENTY_FOUR_HR)) {
        t.hour = (uint8_t)(t.hour % 12 + (pm ? 12 : 0));
    }
    t.year += 2000;  // the chip only stores the last two digits
    return t;
}
