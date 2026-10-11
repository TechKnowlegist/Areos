// keyboard.h - AREOS keyboard driver (PS/2).
//
// Each key press triggers an interrupt: the keyboard taps AREOS on the
// shoulder, AREOS stores the key in a buffer, and goes back to resting.
//
// There are two ways to read the keyboard:
//   - characters: what you'd type into a command line ('a', 'A', '!')
//   - key events: "this key went down" / "this key came up", for games,
//     including keys that don't type anything (arrows, Ctrl, Escape)

#ifndef AREOS_KEYBOARD_H
#define AREOS_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

// Special keys that are also characters.
#define KEY_ENTER      '\n'
#define KEY_BACKSPACE  '\b'
#define KEY_TAB        '\t'
#define KEY_ESCAPE     27

// Keys that don't type anything. (Letter keys use their lowercase letter.)
#define KEY_UP         0x80
#define KEY_DOWN       0x81
#define KEY_LEFT       0x82
#define KEY_RIGHT      0x83
#define KEY_CTRL       0x84
#define KEY_SHIFT      0x85
#define KEY_ALT        0x86
#define KEY_SUPER      0x87   // the Windows key
#define KEY_HOME       0x88
#define KEY_END        0x89
#define KEY_DELETE     0x8A
#define KEY_PAGE_UP    0x8B
#define KEY_PAGE_DOWN  0x8C
#define KEY_F1         0x90   // F1 to F12 are 0x90 to 0x9B
#define KEY_BACK_TAB   0xA0   // only used inside input.c (terminal Shift+Tab)
#define KEY_ALT_CHAR   0xA1   // only used inside input.c (terminal Alt+letter)

struct key_event {
    uint8_t key;       // a KEY_ code, or a lowercase letter/digit/symbol
    bool pressed;      // true = went down, false = came up
    char character;    // what it types ('G', '!', ...) or 0 if nothing / key up
};

// Start listening for key presses.
void keyboard_init(void);

// If a key went down or up, describe it in *out and return true.
bool keyboard_try_get_event(struct key_event *out);

// If a key was typed, put its character in *out and return true.
// Otherwise return false right away (doesn't wait).
bool keyboard_try_get_char(char *out);

// Wait until a key is typed and return its character.
char keyboard_get_char(void);

#endif
