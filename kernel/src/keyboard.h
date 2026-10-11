// keyboard.h - AREOS keyboard driver (PS/2).
//
// Each key press triggers an interrupt: the keyboard taps AREOS on the
// shoulder, AREOS stores the key in a buffer, and goes back to resting.

#ifndef AREOS_KEYBOARD_H
#define AREOS_KEYBOARD_H

// Special keys that aren't normal characters.
#define KEY_ENTER      '\n'
#define KEY_BACKSPACE  '\b'

#include <stdbool.h>

// Start listening for key presses.
void keyboard_init(void);

// If a key was typed, put its character in *out and return true.
// Otherwise return false right away (doesn't wait).
bool keyboard_try_get_char(char *out);

// Wait until a key is typed and return its character.
// Keys AREOS doesn't understand yet are skipped.
char keyboard_get_char(void);

#endif
