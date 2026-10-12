#ifndef AREOS_JS_TIME_H
#define AREOS_JS_TIME_H
#include <stdint.h>
#include <stddef.h>
typedef int64_t time_t;
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};
struct timespec { time_t tv_sec; long tv_nsec; };
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
int clock_gettime(int clock, struct timespec *ts);
struct tm *localtime_r(const time_t *t, struct tm *out);
#endif
