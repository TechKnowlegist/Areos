// Just enough for QuickJS: printing goes to the host's log hook.
#ifndef AREOS_JS_STDIO_H
#define AREOS_JS_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct areos_js_file FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
int printf(const char *format, ...);
int fprintf(FILE *f, const char *format, ...);
int snprintf(char *to, size_t size, const char *format, ...);
int sprintf(char *to, const char *format, ...);
int vsnprintf(char *to, size_t size, const char *format, va_list args);
int fputs(const char *s, FILE *f);
int fputc(int c, FILE *f);
int putc(int c, FILE *f);
int putchar(int c);
size_t fwrite(const void *from, size_t size, size_t count, FILE *f);
int fflush(FILE *f);
#endif
