// memory.c - basic memory functions for AREOS programs.
//
// C compilers sometimes turn ordinary loops into calls to these four
// functions behind your back, so every program needs them.

#include <stddef.h>
#include <stdint.h>

// Stop the compiler from turning these loops back into calls to
// themselves (which would loop forever).
#define NO_LOOP_TRICKS __attribute__((optimize("no-tree-loop-distribute-patterns")))

// Copies 8 bytes at a time when both sides line up, which makes big
// copies (like whole screen images) much faster.
NO_LOOP_TRICKS
void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *to = destination;
    const uint8_t *from = source;
    if ((((uintptr_t)to | (uintptr_t)from) & 7) == 0) {
        uint64_t *to64 = (uint64_t *)to;
        const uint64_t *from64 = (const uint64_t *)from;
        for (; count >= 8; count -= 8) {
            *to64++ = *from64++;
        }
        to = (uint8_t *)to64;
        from = (const uint8_t *)from64;
    }
    for (size_t i = 0; i < count; i++) {
        to[i] = from[i];
    }
    return destination;
}

NO_LOOP_TRICKS
void *memset(void *destination, int value, size_t count) {
    uint8_t *to = destination;
    if (((uintptr_t)to & 7) == 0) {
        uint64_t pattern = (uint8_t)value;
        pattern |= pattern << 8;
        pattern |= pattern << 16;
        pattern |= pattern << 32;
        uint64_t *to64 = (uint64_t *)to;
        for (; count >= 8; count -= 8) {
            *to64++ = pattern;
        }
        to = (uint8_t *)to64;
    }
    for (size_t i = 0; i < count; i++) {
        to[i] = (uint8_t)value;
    }
    return destination;
}

NO_LOOP_TRICKS
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
