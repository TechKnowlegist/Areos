// memory.c - basic memory functions for AREOS programs.
//
// C compilers sometimes turn ordinary loops into calls to these four
// functions behind your back, so every program needs them.

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *to = destination;
    const uint8_t *from = source;
    for (size_t i = 0; i < count; i++) {
        to[i] = from[i];
    }
    return destination;
}

void *memset(void *destination, int value, size_t count) {
    uint8_t *to = destination;
    for (size_t i = 0; i < count; i++) {
        to[i] = (uint8_t)value;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    uint8_t *to = destination;
    const uint8_t *from = source;
    if (to < from) {
        for (size_t i = 0; i < count; i++) {
            to[i] = from[i];
        }
    } else {
        for (size_t i = count; i > 0; i--) {
            to[i - 1] = from[i - 1];
        }
    }
    return destination;
}

int memcmp(const void *a, const void *b, size_t count) {
    const uint8_t *x = a;
    const uint8_t *y = b;
    for (size_t i = 0; i < count; i++) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return 0;
}
