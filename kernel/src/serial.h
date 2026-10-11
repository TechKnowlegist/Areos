// serial.h - the serial port (COM1).
//
// The serial port sends text one character at a time down a "wire".
// QEMU connects that wire to the terminal you started it from, so
// everything AREOS prints also appears there as plain text, where a
// screen reader like NVDA can read it. Typing in that terminal works too.

#ifndef AREOS_SERIAL_H
#define AREOS_SERIAL_H

#include <stdbool.h>

void serial_init(void);

// Send one character.
void serial_putc(char c);

// If a character has arrived, put it in *out and return true.
bool serial_try_getc(char *out);

#endif
