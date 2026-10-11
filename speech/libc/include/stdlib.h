#ifndef SPEECH_STDLIB_H
#define SPEECH_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fffffff
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *p, size_t size);
void free(void *p);
int atoi(const char *s);
long atol(const char *s);
double atof(const char *s);
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int abs(int x);
long labs(long x);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
int rand(void);
void srand(unsigned seed);
char *getenv(const char *name);
_Noreturn void exit(int status);
_Noreturn void abort(void);
#endif
