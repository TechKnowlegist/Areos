// clib.c - the few C library functions the JavaScript engine (QuickJS) needs:
// text and number helpers, printf, the clock, and what happens on a crash.
// Memory is in malloc.c and maths in libm.c.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "areos.h"
#include "jshost_hooks.h"

int errno;

// ---- Strings ----

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return NULL;
}
char *strcpy(char *to, const char *from) {
    char *r = to;
    while ((*to++ = *from++)) { }
    return r;
}
char *strncpy(char *to, const char *from, size_t n) {
    size_t i = 0;
    for (; i < n && from[i]; i++) to[i] = from[i];
    for (; i < n; i++) to[i] = 0;
    return to;
}
char *strcat(char *to, const char *from) {
    strcpy(to + strlen(to), from);
    return to;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}
char *strrchr(const char *s, int c) {
    const char *found = NULL;
    for (;; s++) {
        if (*s == (char)c) found = s;
        if (!*s) return (char *)found;
    }
}
char *strstr(const char *h, const char *n) {
    size_t len = strlen(n);
    if (!len) return (char *)h;
    for (; *h; h++) if (!strncmp(h, n, len)) return (char *)h;
    return NULL;
}
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

// ---- Numbers ----

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

unsigned long strtoul(const char *s, char **end, int base) {
    while (*s == ' ' || *s == '\t') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0) base = (s[0] == '0') ? 8 : 10;
    unsigned long v = 0;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
    }
    if (end) *end = (char *)s;
    return v;
}
long strtol(const char *s, char **end, int base) {
    while (*s == ' ' || *s == '\t') s++;
    bool neg = false;
    if (*s == '-') { neg = true; s++; } else if (*s == '+') s++;
    long v = (long)strtoul(s, end, base);
    return neg ? -v : v;
}

// A simple in-place sort (heap sort: no recursion, no extra memory).
static void swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
    while (n--) { unsigned char t = *a; *a++ = *b; *b++ = t; }
}
void qsort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *)) {
    unsigned char *a = base;
    if (count < 2) return;
    for (size_t start = count / 2; start-- > 0;) {
        size_t root = start;
        for (;;) {
            size_t child = root * 2 + 1;
            if (child >= count) break;
            if (child + 1 < count && cmp(a + child * size, a + (child + 1) * size) < 0) child++;
            if (cmp(a + root * size, a + child * size) >= 0) break;
            swap_bytes(a + root * size, a + child * size, size);
            root = child;
        }
    }
    for (size_t end = count - 1; end > 0; end--) {
        swap_bytes(a, a + end * size, size);
        size_t root = 0;
        for (;;) {
            size_t child = root * 2 + 1;
            if (child >= end) break;
            if (child + 1 < end && cmp(a + child * size, a + (child + 1) * size) < 0) child++;
            if (cmp(a + root * size, a + child * size) >= 0) break;
            swap_bytes(a + root * size, a + child * size, size);
            root = child;
        }
    }
}

// ---- Crashing ----

_Noreturn void abort(void) { js_host_fatal("The JavaScript engine hit an internal error."); }
_Noreturn void exit(int status) { (void)status; js_host_fatal("The JavaScript engine tried to exit."); }
_Noreturn void __assert_fail(const char *a, const char *f, unsigned l, const char *fn) {
    (void)a; (void)f; (void)l; (void)fn;
    js_host_fatal("The JavaScript engine failed an internal check.");
}

// ---- printf ----

struct file_stub { int fd; };
static struct file_stub in_stub = { 0 }, out_stub = { 1 }, err_stub = { 2 };
FILE *stdin = (FILE *)&in_stub, *stdout = (FILE *)&out_stub, *stderr = (FILE *)&err_stub;

struct out { char *to; size_t size, count; };
static void put(struct out *o, char c) {
    if (o->count + 1 < o->size) o->to[o->count] = c;
    o->count++;
}

static int digits_of(char *buf, unsigned long long v, int base, bool upper) {
    char tmp[24];
    int n = 0;
    do {
        int d = (int)(v % (unsigned)base);
        tmp[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
        v /= (unsigned)base;
    } while (v);
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

// Fixed-point text for %f / %g-ish output (debug use only; not exact for huge values).
static int fixed_text(char *buf, double v, int precision) {
    int n = 0;
    if (v != v) { memcpy(buf, "nan", 3); return 3; }
    if (v < 0) { buf[n++] = '-'; v = -v; }
    if (v > 1e18) { memcpy(buf + n, "big", 3); return n + 3; }
    double scale = 1;
    for (int i = 0; i < precision; i++) scale *= 10;
    double scaled = floor(v * scale + 0.5);
    unsigned long long whole = (unsigned long long)(scaled / scale);
    unsigned long long frac = (unsigned long long)(scaled - (double)whole * scale);
    n += digits_of(buf + n, whole, 10, false);
    if (precision > 0) {
        buf[n++] = '.';
        char d[24];
        int dn = digits_of(d, frac, 10, false);
        for (int i = dn; i < precision; i++) buf[n++] = '0';
        memcpy(buf + n, d, (size_t)dn);
        n += dn;
    }
    return n;
}

int vsnprintf(char *to, size_t size, const char *f, va_list ap) {
    struct out o = { to, size, 0 };
    for (; *f; f++) {
        if (*f != '%') { put(&o, *f); continue; }
        f++;
        bool left = false, zero = false, plus = false, space = false, alt = false;
        for (;; f++) {
            if (*f == '-') left = true;
            else if (*f == '0') zero = true;
            else if (*f == '+') plus = true;
            else if (*f == ' ') space = true;
            else if (*f == '#') alt = true;
            else break;
        }
        int width = 0, precision = -1;
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = true; width = -width; } f++; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            f++;
            precision = 0;
            if (*f == '*') { precision = va_arg(ap, int); f++; }
            else while (*f >= '0' && *f <= '9') precision = precision * 10 + (*f++ - '0');
        }
        int longness = 0;   // 0 int, 1 long, 2 long long
        for (;; f++) {
            if (*f == 'l') longness++;
            else if (*f == 'z' || *f == 'j' || *f == 't') longness = 2;
            else if (*f == 'h') { }
            else break;
        }
        char tmp[80];
        const char *text = tmp;
        int len = 0;
        char sign = 0;
        const char *prefix = "";
        char conv = *f;
        if (conv == '%') { put(&o, '%'); continue; }
        if (conv == 'c') { tmp[0] = (char)va_arg(ap, int); len = 1; zero = false; }
        else if (conv == 's') {
            text = va_arg(ap, const char *);
            if (!text) text = "(null)";
            len = 0;
            while (text[len] && (precision < 0 || len < precision)) len++;
            zero = false;
        } else if (conv == 'd' || conv == 'i') {
            long long v = longness == 0 ? (long long)va_arg(ap, int) : longness == 1 ? (long long)va_arg(ap, long) : va_arg(ap, long long);
            unsigned long long u = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
            sign = v < 0 ? '-' : plus ? '+' : space ? ' ' : 0;
            len = (precision == 0 && u == 0) ? 0 : digits_of(tmp, u, 10, false);
        } else if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o' || conv == 'p') {
            unsigned long long u;
            if (conv == 'p') { u = (unsigned long long)(uintptr_t)va_arg(ap, void *); prefix = "0x"; }
            else u = longness == 0 ? (unsigned long long)va_arg(ap, unsigned) : longness == 1 ? (unsigned long long)va_arg(ap, unsigned long) : va_arg(ap, unsigned long long);
            int base = conv == 'u' ? 10 : conv == 'o' ? 8 : 16;
            if (alt && (conv == 'x' || conv == 'X') && u) prefix = conv == 'x' ? "0x" : "0X";
            len = (precision == 0 && u == 0) ? 0 : digits_of(tmp, u, base, conv == 'X');
        } else if (conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G' || conv == 'e' || conv == 'E') {
            double v = va_arg(ap, double);
            len = fixed_text(tmp, v, precision < 0 ? 6 : precision);
            if (tmp[0] == '-') { sign = '-'; memmove(tmp, tmp + 1, (size_t)--len); }
            else sign = plus ? '+' : space ? ' ' : 0;
        } else {
            put(&o, '%'); put(&o, conv); if (!conv) break; continue;
        }
        int zeros = 0;
        if (conv != 'c' && conv != 's' && precision > len && !(conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G' || conv == 'e' || conv == 'E')) zeros = precision - len;
        int total = len + zeros + (sign ? 1 : 0) + (int)strlen(prefix);
        int pad = width > total ? width - total : 0;
        if (!left && !zero) while (pad-- > 0) put(&o, ' ');
        if (sign) put(&o, sign);
        for (const char *p = prefix; *p; p++) put(&o, *p);
        if (!left && zero && precision < 0) while (pad-- > 0) put(&o, '0');
        else if (!left && zero) while (pad-- > 0) put(&o, ' ');
        while (zeros-- > 0) put(&o, '0');
        for (int i = 0; i < len; i++) put(&o, text[i]);
        if (left) while (pad-- > 0) put(&o, ' ');
    }
    if (size > 0) to[o.count < size ? o.count : size - 1] = '\0';
    return (int)o.count;
}
int snprintf(char *to, size_t size, const char *f, ...) {
    va_list ap; va_start(ap, f);
    int n = vsnprintf(to, size, f, ap);
    va_end(ap);
    return n;
}
int sprintf(char *to, const char *f, ...) {
    va_list ap; va_start(ap, f);
    int n = vsnprintf(to, 1u << 30, f, ap);
    va_end(ap);
    return n;
}
static void emit(const char *text, size_t n) { js_host_log(text, n); }
int fprintf(FILE *file, const char *f, ...) {
    (void)file;
    char buf[512];
    va_list ap; va_start(ap, f);
    int n = vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    emit(buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
    return n;
}
int printf(const char *f, ...) {
    char buf[512];
    va_list ap; va_start(ap, f);
    int n = vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    emit(buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
    return n;
}
int fputs(const char *s, FILE *f) { (void)f; emit(s, strlen(s)); return 0; }
int fputc(int c, FILE *f) { (void)f; char ch = (char)c; emit(&ch, 1); return c; }
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
size_t fwrite(const void *p, size_t size, size_t count, FILE *f) { (void)f; emit(p, size * count); return count; }
int fflush(FILE *f) { (void)f; return 0; }

// ---- The clock ----
//
// AREOS knows the calendar date and time (in local time; it doesn't know the
// time zone yet, so JavaScript sees local time as if it were UTC) and the
// milliseconds since boot. Together they give a steady millisecond clock.

static int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int64_t base_epoch_ms = -1;
static uint64_t base_tick_ms;
int64_t js_host_epoch_ms(void) {
    if (base_epoch_ms < 0) {
        struct areos_date_time n = areos_date_time();
        int64_t days = days_from_civil(n.year, n.month, n.day);
        base_epoch_ms = (days * 86400 + n.hour * 3600 + n.minute * 60 + n.second) * 1000;
        base_tick_ms = areos_time_ms();
    }
    return base_epoch_ms + (int64_t)(areos_time_ms() - base_tick_ms);
}
int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    int64_t ms = js_host_epoch_ms();
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (long)(ms % 1000) * 1000;
    return 0;
}
int clock_gettime(int clock, struct timespec *ts) {
    int64_t ms = clock == CLOCK_MONOTONIC ? (int64_t)areos_time_ms() : js_host_epoch_ms();
    ts->tv_sec = ms / 1000;
    ts->tv_nsec = (long)(ms % 1000) * 1000000;
    return 0;
}
struct tm *localtime_r(const time_t *t, struct tm *out) {
    int64_t secs = *t;
    int64_t days = secs / 86400, rem = secs % 86400;
    if (rem < 0) { rem += 86400; days--; }
    out->tm_hour = (int)(rem / 3600);
    out->tm_min = (int)(rem % 3600 / 60);
    out->tm_sec = (int)(rem % 60);
    out->tm_wday = (int)(((days % 7) + 11) % 7);
    out->tm_gmtoff = 0;
    out->tm_isdst = 0;
    out->tm_zone = "UTC";
    out->tm_mday = out->tm_mon = out->tm_year = out->tm_yday = 0;   // (QuickJS only uses tm_gmtoff)
    return out;
}
