// stdio.h - just enough for Flite. AREOS has no files yet, so the file
// functions all fail politely; text formatting (sprintf) works.
#ifndef SPEECH_STDIO_H
#define SPEECH_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct speech_file FILE;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
extern FILE *stdin, *stdout, *stderr;
int printf(const char *format, ...);
int fprintf(FILE *f, const char *format, ...);
int sprintf(char *to, const char *format, ...);
int snprintf(char *to, size_t size, const char *format, ...);
int vsprintf(char *to, const char *format, va_list args);
int vsnprintf(char *to, size_t size, const char *format, va_list args);
int vfprintf(FILE *f, const char *format, va_list args);
int sscanf(const char *from, const char *format, ...);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *to, size_t size, size_t count, FILE *f);
size_t fwrite(const void *from, size_t size, size_t count, FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
int fgetc(FILE *f);
int getc(FILE *f);
int ungetc(int c, FILE *f);
int fputc(int c, FILE *f);
int putc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int fflush(FILE *f);
int feof(FILE *f);
int putchar(int c);
int puts(const char *s);
#endif
