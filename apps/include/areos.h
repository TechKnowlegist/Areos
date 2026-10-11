// areos.h - everything an AREOS program needs.
//
// Include this, write a main() that returns an int, and you have an
// AREOS program. See apps/hello.c for an example.
//
// Programs talk to AREOS through "system calls": put a request number in
// register rax, details in rdi and rsi, and run `int 0x80`. The functions
// below wrap that up so you never have to.
//
// The numbers here MUST match kernel/src/syscall.h.

#ifndef AREOS_H
#define AREOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AREOS_SYS_EXIT         0
#define AREOS_SYS_WRITE        1
#define AREOS_SYS_READ_KEY     2
#define AREOS_SYS_SLEEP_MS     3
#define AREOS_SYS_TIME_MS      4
#define AREOS_SYS_MORE_MEMORY  5
#define AREOS_SYS_SET_COLOR    6

// The AREOS colors, so programs can match the system's look.
#define AREOS_GREEN    0x3DFF9A
#define AREOS_BLUE     0x4FC3F7
#define AREOS_DIM      0x7A8C9E
#define AREOS_WARNING  0xFFB347

#define AREOS_KEY_ENTER      '\n'
#define AREOS_KEY_BACKSPACE  '\b'

static inline uint64_t areos_syscall(uint64_t number, uint64_t arg1, uint64_t arg2) {
    uint64_t result;
    asm volatile ("int $0x80"
                  : "=a"(result)
                  : "a"(number), "D"(arg1), "S"(arg2)
                  : "memory");
    return result;
}

// End the program. 0 means "everything went fine".
__attribute__((noreturn)) static inline void areos_exit(int code) {
    areos_syscall(AREOS_SYS_EXIT, (uint64_t)code, 0);
    for (;;) { }
}

// Print `length` characters of `text`.
static inline void areos_write(const char *text, size_t length) {
    areos_syscall(AREOS_SYS_WRITE, (uint64_t)text, length);
}

// Print a string.
static inline void areos_print(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    areos_write(text, length);
}

// Print a whole number.
static inline void areos_print_number(uint64_t number) {
    char digits[21];
    int count = 0;
    do {
        digits[20 - count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number > 0);
    areos_write(&digits[21 - count], (size_t)count);
}

// Wait for a key and return it (letters, AREOS_KEY_ENTER, AREOS_KEY_BACKSPACE).
static inline char areos_read_key(void) {
    return (char)areos_syscall(AREOS_SYS_READ_KEY, 0, 0);
}

// Pause for some milliseconds (1000 = one second).
static inline void areos_sleep_ms(uint64_t ms) {
    areos_syscall(AREOS_SYS_SLEEP_MS, ms, 0);
}

// Milliseconds since AREOS started.
static inline uint64_t areos_time_ms(void) {
    return areos_syscall(AREOS_SYS_TIME_MS, 0, 0);
}

// Get `bytes` of fresh, zeroed memory. Returns NULL if there isn't enough.
static inline void *areos_more_memory(size_t bytes) {
    return (void *)areos_syscall(AREOS_SYS_MORE_MEMORY, bytes, 0);
}

// Change the color of text printed after this (0xRRGGBB).
static inline void areos_set_color(uint32_t color) {
    areos_syscall(AREOS_SYS_SET_COLOR, color, 0);
}

// Read a line of typing into `buffer` (at most `size - 1` characters).
// Shows what's typed and handles Backspace. Returns the length.
static inline size_t areos_read_line(char *buffer, size_t size) {
    size_t length = 0;
    for (;;) {
        char c = areos_read_key();
        if (c == AREOS_KEY_ENTER) {
            areos_print("\n");
            buffer[length] = '\0';
            return length;
        }
        if (c == AREOS_KEY_BACKSPACE) {
            if (length > 0) {
                length--;
                areos_write("\b", 1);
            }
            continue;
        }
        if (length + 1 < size) {
            buffer[length++] = c;
            areos_write(&c, 1);
        }
    }
}

#endif
