// web_util.c - the arena, and UTF-8 text. See web_port.h.

#include "web_port.h"

struct w_chunk {
    struct w_chunk *next;
    size_t size, used;
    // the memory follows
};

#define CHUNK_SIZE (256 * 1024)

void *w_alloc(struct w_arena *a, size_t size) {
    size = (size + 7) & ~(size_t)7;
    if (size == 0) size = 8;
    struct w_chunk *c = a->last;
    if (!c || c->used + size > c->size) {
        size_t want = size + sizeof(struct w_chunk) > CHUNK_SIZE ? size + sizeof(struct w_chunk) : CHUNK_SIZE;
        c = malloc(want);
        if (!c) return NULL;
        c->next = NULL;
        c->size = want - sizeof(struct w_chunk);
        c->used = 0;
        if (a->last) a->last->next = c; else a->first = c;
        a->last = c;
    }
    void *p = (char *)(c + 1) + c->used;
    c->used += size;
    a->total += size;
    memset(p, 0, size);
    return p;
}

char *w_strdup(struct w_arena *a, const char *s, int len) {
    char *p = w_alloc(a, (size_t)len + 1);
    if (!p) return NULL;
    memcpy(p, s, (size_t)len);
    p[len] = '\0';
    return p;
}

void w_arena_free(struct w_arena *a) {
    struct w_chunk *c = a->first;
    while (c) {
        struct w_chunk *next = c->next;
        free(c);
        c = next;
    }
    a->first = a->last = NULL;
    a->total = 0;
}

uint32_t w_utf8_next(const char **p, const char *end) {
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = s[0];
    if (c < 0x80) { *p += 1; return c; }
    int extra = (c >= 0xF0 && c < 0xF8) ? 3 : (c >= 0xE0 && c < 0xF0) ? 2 : (c >= 0xC2 && c < 0xE0) ? 1 : -1;
    if (extra < 0 || (const char *)(s + extra) >= end) { *p += 1; return 0xFFFD; }
    uint32_t cp = c & (0x3F >> extra);
    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p += 1; return 0xFFFD; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p += extra + 1;
    return cp;
}

int w_utf8_put(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

#ifndef WEB_HOST
// The little C library of the JavaScript engine has no atoi.
int atoi(const char *s) {
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v * sign;
}
#endif
