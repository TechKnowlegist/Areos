// io.h - talking to hardware through I/O ports.
//
// On x86 PCs, many devices (like the keyboard) are reached through
// numbered "ports". inb reads one byte from a port; outb writes one.

#ifndef AREOS_IO_H
#define AREOS_IO_H

#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

#endif
