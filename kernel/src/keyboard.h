// keyboard.h - AREOS keyboard driver (PS/2).
//
// For now AREOS "polls" the keyboard: it keeps asking "was a key
// pressed yet?" in a loop. Later, once interrupts work (roadmap step 5),
// the keyboard will tap AREOS on the shoulder instead.

#ifndef AREOS_KEYBOARD_H
#define AREOS_KEYBOARD_H

// Special keys that aren't normal characters.
#define KEY_ENTER      '\n'
#define KEY_BACKSPACE  '\b'

// Wait until a key is pressed and return its character.
// Keys AREOS doesn't understand yet are skipped.
char keyboard_get_char(void);

#endif
