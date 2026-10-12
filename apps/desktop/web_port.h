// web_port.h - what the web engine needs from the machine it runs on.
//
// The web engine (web_*.c, img_*.c) is plain C that doesn't know about AREOS.
// It runs inside the AREOS browser, and also on an ordinary computer (that is
// how it is tested: a small program draws pages to a picture file). The only
// things it needs are memory, a few text functions, and an arena: a pile of
// memory that is handed out in pieces and thrown away all at once.

#ifndef WEB_PORT_H
#define WEB_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef WEB_HOST
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#else
void *malloc(size_t size);
void *realloc(void *p, size_t size);
void free(void *p);
void *calloc(size_t n, size_t size);
void *memcpy(void *to, const void *from, size_t n);
void *memset(void *to, int value, size_t n);
void *memmove(void *to, const void *from, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
int snprintf(char *to, size_t size, const char *format, ...);
int abs(int x);
int atoi(const char *s);
void qsort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *));
#endif

// ---- Small text helpers (ASCII) ----

static inline bool w_is_space(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f'; }
static inline bool w_is_digit(int c) { return c >= '0' && c <= '9'; }
static inline bool w_is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline bool w_is_alnum(int c) { return w_is_alpha(c) || w_is_digit(c); }
static inline int w_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static inline int w_hex(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Equal, ignoring upper and lower case. `b` is lower case.
static inline bool w_eq(const char *a, int alen, const char *b) {
    int i = 0;
    for (; i < alen; i++) {
        if (b[i] == '\0' || w_lower((unsigned char)a[i]) != (unsigned char)b[i]) return false;
    }
    return b[i] == '\0';
}
static inline bool w_eq0(const char *a, const char *b) {
    for (;; a++, b++) {
        if (w_lower((unsigned char)*a) != w_lower((unsigned char)*b)) return false;
        if (!*a) return true;
    }
}
static inline bool w_starts(const char *a, const char *prefix) {
    for (; *prefix; a++, prefix++) if (w_lower((unsigned char)*a) != w_lower((unsigned char)*prefix)) return false;
    return true;
}
static inline int w_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

// ---- Arena ----

struct w_arena {
    struct w_chunk *first;
    struct w_chunk *last;
    size_t total;
};

void *w_alloc(struct w_arena *a, size_t size);                 // zeroed, 8-byte aligned
char *w_strdup(struct w_arena *a, const char *s, int len);     // copy of `len` bytes + a zero
void w_arena_free(struct w_arena *a);                          // give everything back

// ---- Utf-8 ----

// Read one character from UTF-8 text; moves *p on. Bad bytes come out as U+FFFD.
uint32_t w_utf8_next(const char **p, const char *end);
// Write a character as UTF-8. Returns how many bytes (1-4).
int w_utf8_put(uint32_t cp, char *out);

#define W_MIN(a, b) ((a) < (b) ? (a) : (b))
#define W_MAX(a, b) ((a) > (b) ? (a) : (b))
static inline int w_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

#endif
