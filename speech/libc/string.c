// string.c - string and character functions for the speech engine.
//
// The speech engine (Flite) is ordinary C that expects a C library. AREOS
// doesn't have one, so this folder is a small one: just what Flite uses.

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

// memcpy, memset, memmove and memcmp come from the kernel
// (kernel/src/memory.c), which the speech engine is linked into. The host
// test build supplies its own.

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) return (void *)(p + i);
    }
    return NULL;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *to, const char *from) {
    char *d = to;
    while ((*d++ = *from++)) { }
    return to;
}

char *strncpy(char *to, const char *from, size_t n) {
    size_t i = 0;
    for (; i < n && from[i]; i++) to[i] = from[i];
    for (; i < n; i++) to[i] = '\0';
    return to;
}

char *strcat(char *to, const char *from) {
    strcpy(to + strlen(to), from);
    return to;
}

char *strncat(char *to, const char *from, size_t n) {
    char *d = to + strlen(to);
    while (n-- && *from) *d++ = *from++;
    *d = '\0';
    return to;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y || !x) return x - y;
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
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c) last = s;
        if (!*s) return (char *)last;
    }
}

char *strstr(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle && strncmp(haystack, needle, n) == 0) return (char *)haystack;
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (s[n] && strchr(accept, s[n])) n++;
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (s[n] && !strchr(reject, s[n])) n++;
    return n;
}

char *strpbrk(const char *s, const char *accept) {
    for (; *s; s++) {
        if (strchr(accept, *s)) return (char *)s;
    }
    return NULL;
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

char *strerror(int error) {
    (void)error;
    return "error";
}

#include <wchar.h>
size_t wcslen(const wchar_t *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
