// input.h - typing, from either the keyboard or the terminal.
//
// Keys can arrive two ways: the PS/2 keyboard (typing in the QEMU
// window) or the serial port (typing in the terminal, which is how a
// screen reader user drives AREOS). This file merges both.

#ifndef AREOS_INPUT_H
#define AREOS_INPUT_H

#include <stdbool.h>
#include "keyboard.h"

// If a typed character is waiting from either source, put it in *out
// and return true.
bool input_try_get_key(char *out);

// Wait for a typed character. If `blink_cursor` is set, blink the
// cursor while waiting.
char input_wait_key(bool blink_cursor);

// If a key went down or up, describe it in *out and return true. Keys
// typed in the terminal arrive as a "down" followed right away by an "up",
// because terminals only say which key was typed, not how long it was held.
bool input_try_get_event(struct key_event *out);

#endif
