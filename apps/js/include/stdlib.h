#ifndef AREOS_JS_STDLIB_H
#define AREOS_JS_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define alloca(n) __builtin_alloca(n)
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *p, size_t size);
void free(void *p);
int abs(int x);
long labs(long x);
long long llabs(long long x);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
_Noreturn void abort(void);
_Noreturn void exit(int status);
#endif
