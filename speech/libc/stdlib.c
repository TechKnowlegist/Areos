// stdlib.c - number parsing, sorting and friends for the speech engine.

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "libc.h"

int errno;

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

long strtol(const char *s, char **end, int base) {
    while (isspace((unsigned char)*s)) s++;
    bool negative = false;
    if (*s == '-' || *s == '+') negative = *s++ == '-';
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = s[0] == '0' ? 8 : 10;
    }
    long value = 0;
    for (;; s++) {
        int digit;
        if (isdigit((unsigned char)*s)) digit = *s - '0';
        else if (isalpha((unsigned char)*s)) digit = tolower((unsigned char)*s) - 'a' + 10;
        else break;
        if (digit >= base) break;
        value = value * base + digit;
    }
    if (end) *end = (char *)s;
    return negative ? -value : value;
}

unsigned long strtoul(const char *s, char **end, int base) {
    return (unsigned long)strtol(s, end, base);
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

double strtod(const char *s, char **end) {
    const char *start = s;
    while (isspace((unsigned char)*s)) s++;
    bool negative = false;
    if (*s == '-' || *s == '+') negative = *s++ == '-';
    double value = 0;
    bool any = false;
    while (isdigit((unsigned char)*s)) {
        value = value * 10 + (*s++ - '0');
        any = true;
    }
    if (*s == '.') {
        s++;
        double scale = 0.1;
        while (isdigit((unsigned char)*s)) {
            value += (*s++ - '0') * scale;
            scale *= 0.1;
            any = true;
        }
    }
    if (any && (*s == 'e' || *s == 'E')) {
        const char *e = s + 1;
        bool exp_negative = false;
        if (*e == '-' || *e == '+') exp_negative = *e++ == '-';
        if (isdigit((unsigned char)*e)) {
            int exponent = 0;
            while (isdigit((unsigned char)*e)) exponent = exponent * 10 + (*e++ - '0');
            value *= pow(10.0, exp_negative ? -exponent : exponent);
            s = e;
        }
    }
    if (end) *end = (char *)(any ? s : start);
    return negative ? -value : value;
}

float strtof(const char *s, char **end) { return (float)strtod(s, end); }
double atof(const char *s) { return strtod(s, NULL); }

// Shell sort: small, no recursion, and fast enough for what Flite sorts.
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *)) {
    char *b = base;
    char tmp[256];
    if (size > sizeof(tmp)) return;   // Flite never sorts anything that big
    for (size_t gap = count / 2; gap > 0; gap /= 2) {
        for (size_t i = gap; i < count; i++) {
            memcpy(tmp, b + i * size, size);
            size_t j = i;
            while (j >= gap && compare(b + (j - gap) * size, tmp) > 0) {
                memcpy(b + j * size, b + (j - gap) * size, size);
                j -= gap;
            }
            memcpy(b + j * size, tmp, size);
        }
    }
}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
    const char *b = base;
    size_t low = 0, high = count;
    while (low < high) {
        size_t mid = (low + high) / 2;
        int c = compare(key, b + mid * size);
        if (c == 0) return (void *)(b + mid * size);
        if (c < 0) high = mid;
        else low = mid + 1;
    }
    return NULL;
}

static unsigned long random_state = 1;
int rand(void) {
    random_state = random_state * 1103515245 + 12345;
    return (int)((random_state >> 1) & RAND_MAX);
}
void srand(unsigned seed) { random_state = seed; }

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

_Noreturn void exit(int status) {
    (void)status;
    speech_libc_fatal("exit");
}

_Noreturn void abort(void) {
    speech_libc_fatal("abort");
}
