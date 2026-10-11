// input.h - typing, from either the keyboard or the terminal.
//
// Keys can arrive two ways: the PS/2 keyboard (typing in the QEMU
// window) or the serial port (typing in the terminal, which is how a
// screen reader user drives AREOS). This file merges both.

#ifndef AREOS_INPUT_H
#define AREOS_INPUT_H

#include <stdbool.h>

// If a key is waiting from either source, put it in *out and return true.
bool input_try_get_key(char *out);

// Wait for a key. If `blink_cursor` is set, blink the cursor while waiting.
char input_wait_key(bool blink_cursor);

#endif
