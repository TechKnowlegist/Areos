#include <stdint.h>
#include <stddef.h>
#include <memory.h>

// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// They must be implemented as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!

// These stop the compiler from "helpfully" turning the loops below back
// into calls to memcpy/memset, which would call themselves forever.
#define NO_LOOP_TRICKS __attribute__((optimize("no-tree-loop-distribute-patterns")))

#ifdef memcpy
#  undef memcpy
#endif
// Copies 8 bytes at a time when it can (much faster for big things like
// screen images), then finishes any leftover bytes one by one.
NO_LOOP_TRICKS
void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    uint8_t *pdest = dest;
    const uint8_t *psrc = src;

    if ((((uintptr_t)pdest | (uintptr_t)psrc) & 7) == 0) {
        uint64_t *d64 = (uint64_t *)pdest;
        const uint64_t *s64 = (const uint64_t *)psrc;
        for (; n >= 8; n -= 8) {
            *d64++ = *s64++;
        }
        pdest = (uint8_t *)d64;
        psrc = (const uint8_t *)s64;
    }
    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

#ifdef memset
#  undef memset
#endif
NO_LOOP_TRICKS
void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;

    if (((uintptr_t)p & 7) == 0) {
        uint64_t pattern = (uint8_t)c;
        pattern |= pattern << 8;
        pattern |= pattern << 16;
        pattern |= pattern << 32;
        uint64_t *p64 = (uint64_t *)p;
        for (; n >= 8; n -= 8) {
            *p64++ = pattern;
        }
        p = (uint8_t *)p64;
    }
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

#ifdef memmove
#  undef memmove
#endif
void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = dest;
    const uint8_t *psrc = src;

    if ((uintptr_t)src > (uintptr_t)dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if ((uintptr_t)src < (uintptr_t)dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

#ifdef memcmp
#  undef memcmp
#endif
int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = s1;
    const uint8_t *p2 = s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}
