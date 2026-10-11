// stdio.c - text formatting for the speech engine.
//
// sprintf and friends work fully (Flite uses them to build feature names
// and numbers). printf/fprintf go to a debug hook. AREOS has no files yet,
// so opening one always fails, and Flite carries on without it.

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "libc.h"

struct speech_file { int unused; };
static struct speech_file standard_files[3];
FILE *stdin = &standard_files[0];
FILE *stdout = &standard_files[1];
FILE *stderr = &standard_files[2];

// ---- The formatter ----

struct out {
    char *to;
    size_t size;     // room in `to`, including the final '\0'
    size_t length;   // characters produced so far (may be more than fit)
};

static void put(struct out *o, char c) {
    if (o->length + 1 < o->size) o->to[o->length] = c;
    o->length++;
}

static void put_padded(struct out *o, const char *text, size_t n, int width, bool left, char pad) {
    int spaces = width > (int)n ? width - (int)n : 0;
    if (!left) {
        // Zero padding goes after a sign.
        if (pad == '0' && n > 0 && (text[0] == '-' || text[0] == '+')) {
            put(o, *text++);
            n--;
        }
        while (spaces-- > 0) put(o, pad);
    }
    for (size_t i = 0; i < n; i++) put(o, text[i]);
    if (left) while (spaces-- > 0) put(o, ' ');
}

static int format_unsigned(char *buf, uint64_t value, int base, bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int n = 0;
    do {
        tmp[n++] = digits[value % base];
        value /= base;
    } while (value);
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

// Fixed-point: "-12.345" with `precision` digits after the point.
static int format_fixed(char *buf, double value, int precision) {
    int n = 0;
    if (value < 0) {
        buf[n++] = '-';
        value = -value;
    }
    if (precision > 17) precision = 17;
    double scale = 1;
    for (int i = 0; i < precision; i++) scale *= 10;
    value = floor(value * scale + 0.5);
    if (value > 1.8e19) {   // too big to print exactly; good enough
        buf[n++] = 'b'; buf[n++] = 'i'; buf[n++] = 'g';
        return n;
    }
    uint64_t whole = (uint64_t)(value / scale);
    uint64_t fraction = (uint64_t)(value - (double)whole * scale);
    n += format_unsigned(buf + n, whole, 10, false);
    if (precision > 0) {
        buf[n++] = '.';
        char digits[24];
        int d = format_unsigned(digits, fraction, 10, false);
        for (int i = d; i < precision; i++) buf[n++] = '0';
        for (int i = 0; i < d; i++) buf[n++] = digits[i];
    }
    return n;
}

// Exponent form: "1.234e+05".
static int format_exponent(char *buf, double value, int precision) {
    int n = 0;
    if (value < 0) {
        buf[n++] = '-';
        value = -value;
    }
    int exponent = 0;
    if (value != 0) {
        while (value >= 10) { value /= 10; exponent++; }
        while (value < 1) { value *= 10; exponent--; }
    }
    n += format_fixed(buf + n, value, precision);
    buf[n++] = 'e';
    buf[n++] = exponent < 0 ? '-' : '+';
    if (exponent < 0) exponent = -exponent;
    if (exponent < 10) buf[n++] = '0';
    n += format_unsigned(buf + n, (uint64_t)exponent, 10, false);
    return n;
}

// %g: the shorter of fixed and exponent, without trailing zeros.
static int format_general(char *buf, double value, int precision) {
    if (precision == 0) precision = 1;
    double magnitude = value < 0 ? -value : value;
    int exponent = 0;
    if (magnitude != 0) {
        double m = magnitude;
        while (m >= 10) { m /= 10; exponent++; }
        while (m < 1) { m *= 10; exponent--; }
    }
    int n;
    if (exponent < -4 || exponent >= precision) {
        n = format_exponent(buf, value, precision - 1);
        // Trim zeros before the 'e'.
        int e = 0;
        while (buf[e] != 'e') e++;
        int end = e;
        bool has_point = false;
        for (int i = 0; i < e; i++) if (buf[i] == '.') has_point = true;
        if (has_point) {
            while (end > 0 && buf[end - 1] == '0') end--;
            if (end > 0 && buf[end - 1] == '.') end--;
        }
        int tail = n - e;
        memmove(buf + end, buf + e, (size_t)tail);
        n = end + tail;
    } else {
        int decimals = precision - 1 - exponent;
        if (decimals < 0) decimals = 0;
        n = format_fixed(buf, value, decimals);
        bool has_point = false;
        for (int i = 0; i < n; i++) if (buf[i] == '.') has_point = true;
        if (has_point) {
            while (n > 0 && buf[n - 1] == '0') n--;
            if (n > 0 && buf[n - 1] == '.') n--;
        }
    }
    return n;
}

int vsnprintf(char *to, size_t size, const char *format, va_list args) {
    struct out o = { to, size, 0 };
    for (const char *f = format; *f; f++) {
        if (*f != '%') {
            put(&o, *f);
            continue;
        }
        f++;
        bool left = false, plus = false, space = false;
        char pad = ' ';
        for (;; f++) {
            if (*f == '-') left = true;
            else if (*f == '0') pad = '0';
            else if (*f == '+') plus = true;
            else if (*f == ' ') space = true;
            else if (*f == '#') { }
            else break;
        }
        int width = 0;
        if (*f == '*') {
            width = va_arg(args, int);
            if (width < 0) { left = true; width = -width; }
            f++;
        } else {
            while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        }
        int precision = -1;
        if (*f == '.') {
            f++;
            precision = 0;
            if (*f == '*') {
                precision = va_arg(args, int);
                f++;
            } else {
                while (*f >= '0' && *f <= '9') precision = precision * 10 + (*f++ - '0');
            }
        }
        int longs = 0;
        while (*f == 'l' || *f == 'h' || *f == 'z' || *f == 'L') {
            if (*f == 'l' || *f == 'z') longs++;
            f++;
        }
        if (left) pad = ' ';

        char buf[400];
        int n = 0;
        switch (*f) {
        case 'd': case 'i': {
            int64_t v = longs ? va_arg(args, int64_t) : va_arg(args, int);
            if (v < 0) buf[n++] = '-';
            else if (plus) buf[n++] = '+';
            else if (space) buf[n++] = ' ';
            n += format_unsigned(buf + n, v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v, 10, false);
            put_padded(&o, buf, (size_t)n, width, left, pad);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            uint64_t v = longs ? va_arg(args, uint64_t) : va_arg(args, unsigned);
            int base = *f == 'u' ? 10 : (*f == 'o' ? 8 : 16);
            n = format_unsigned(buf, v, base, *f == 'X');
            put_padded(&o, buf, (size_t)n, width, left, pad);
            break;
        }
        case 'p': {
            buf[0] = '0'; buf[1] = 'x';
            n = 2 + format_unsigned(buf + 2, (uint64_t)(uintptr_t)va_arg(args, void *), 16, false);
            put_padded(&o, buf, (size_t)n, width, left, ' ');
            break;
        }
        case 'c':
            buf[0] = (char)va_arg(args, int);
            put_padded(&o, buf, 1, width, left, ' ');
            break;
        case 's': {
            const char *s = va_arg(args, const char *);
            if (s == NULL) s = "(null)";
            size_t len = strlen(s);
            if (precision >= 0 && (size_t)precision < len) len = (size_t)precision;
            put_padded(&o, s, len, width, left, ' ');
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = va_arg(args, double);
            if (precision < 0) precision = 6;
            if (v != v) { memcpy(buf, "nan", 3); n = 3; }
            else if (v > 1.7e308) { memcpy(buf, "inf", 3); n = 3; }
            else if (v < -1.7e308) { memcpy(buf, "-inf", 4); n = 4; }
            else {
                int start = 0;
                if (v >= 0 && plus) buf[start++] = '+';
                else if (v >= 0 && space) buf[start++] = ' ';
                if (*f == 'f' || *f == 'F') n = start + format_fixed(buf + start, v, precision);
                else if (*f == 'e' || *f == 'E') n = start + format_exponent(buf + start, v, precision);
                else n = start + format_general(buf + start, v, precision);
            }
            put_padded(&o, buf, (size_t)n, width, left, pad);
            break;
        }
        case '%':
            put(&o, '%');
            break;
        case '\0':
            f--;
            break;
        default:
            put(&o, '%');
            put(&o, *f);
            break;
        }
    }
    if (o.size > 0) o.to[o.length < o.size ? o.length : o.size - 1] = '\0';
    return (int)o.length;
}

int vsprintf(char *to, const char *format, va_list args) {
    return vsnprintf(to, (size_t)1 << 30, format, args);
}

int snprintf(char *to, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(to, size, format, args);
    va_end(args);
    return n;
}

int sprintf(char *to, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsprintf(to, format, args);
    va_end(args);
    return n;
}

int vfprintf(FILE *f, const char *format, va_list args) {
    (void)f;
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), format, args);
    speech_libc_debug_write(buf, strlen(buf));
    return n;
}

int fprintf(FILE *f, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfprintf(f, format, args);
    va_end(args);
    return n;
}

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfprintf(stdout, format, args);
    va_end(args);
    return n;
}

int putchar(int c) {
    char ch = (char)c;
    speech_libc_debug_write(&ch, 1);
    return c;
}
int puts(const char *s) {
    speech_libc_debug_write(s, strlen(s));
    speech_libc_debug_write("\n", 1);
    return 0;
}
int fputc(int c, FILE *f) { (void)f; return putchar(c); }
int putc(int c, FILE *f) { return fputc(c, f); }
int fputs(const char *s, FILE *f) { (void)f; speech_libc_debug_write(s, strlen(s)); return 0; }
int fflush(FILE *f) { (void)f; return 0; }

// Flite's sscanf use is only for reading numbers back; handle %d and %f.
int sscanf(const char *from, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int matched = 0;
    extern double strtod(const char *, char **);
    extern long strtol(const char *, char **, int);
    for (const char *f = format; *f; f++) {
        if (*f != '%') continue;
        f++;
        bool is_long = false;
        while (*f == 'l') { is_long = true; f++; }
        char *end;
        if (*f == 'd') {
            long v = strtol(from, &end, 10);
            if (end == from) break;
            *va_arg(args, int *) = (int)v;
        } else if (*f == 'f' || *f == 'g' || *f == 'e') {
            double v = strtod(from, &end);
            if (end == from) break;
            if (is_long) *va_arg(args, double *) = v;
            else *va_arg(args, float *) = (float)v;
        } else {
            break;
        }
        from = end;
        matched++;
    }
    va_end(args);
    return matched;
}

// ---- No files yet ----

FILE *fopen(const char *path, const char *mode) { (void)path; (void)mode; return NULL; }
int fclose(FILE *f) { (void)f; return 0; }
size_t fread(void *to, size_t size, size_t count, FILE *f) { (void)to; (void)size; (void)count; (void)f; return 0; }
size_t fwrite(const void *from, size_t size, size_t count, FILE *f) {
    (void)f;
    speech_libc_debug_write(from, size * count);
    return count;
}
int fseek(FILE *f, long offset, int whence) { (void)f; (void)offset; (void)whence; return -1; }
long ftell(FILE *f) { (void)f; return -1; }
int fgetc(FILE *f) { (void)f; return EOF; }
int getc(FILE *f) { (void)f; return EOF; }
int ungetc(int c, FILE *f) { (void)c; (void)f; return EOF; }
int feof(FILE *f) { (void)f; return 1; }
