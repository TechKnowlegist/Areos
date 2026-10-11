// keyboard.c - AREOS keyboard driver (PS/2).
//
// When you press or release a key, the keyboard sends a number called a
// "scancode". It isn't a letter: it says WHICH physical key moved.
// This file turns those numbers into letters.

#include <stdbool.h>
#include <stdint.h>
#include "keyboard.h"
#include "io.h"

#define KEYBOARD_DATA_PORT    0x60  // where scancodes arrive
#define KEYBOARD_STATUS_PORT  0x64  // tells us if a scancode is waiting
#define STATUS_OUTPUT_FULL    0x01  // bit set = there's a byte to read

#define SCANCODE_RELEASED     0x80  // added to a scancode when a key is let go
#define SCANCODE_EXTENDED     0xE0  // prefix for extra keys (arrows etc.)
#define SCANCODE_LEFT_SHIFT   0x2A
#define SCANCODE_RIGHT_SHIFT  0x36
#define SCANCODE_CAPS_LOCK    0x3A

// Scancode -> character, for a US keyboard layout. 0 means "not a character".
static const char keymap_normal[128] = {
    0,    0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' ',
};

// The same keys while Shift is held.
static const char keymap_shift[128] = {
    0,    0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' ',
};

static bool shift_held = false;
static bool caps_lock_on = false;
static bool next_is_extended = false;

// Wait for the keyboard to send one scancode, then return it.
static uint8_t read_scancode(void) {
    while ((inb(KEYBOARD_STATUS_PORT) & STATUS_OUTPUT_FULL) == 0) {
        // nothing yet, keep waiting
    }
    return inb(KEYBOARD_DATA_PORT);
}

static bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char keyboard_get_char(void) {
    for (;;) {
        uint8_t scancode = read_scancode();

        if (scancode == SCANCODE_EXTENDED) {
            next_is_extended = true;  // arrows etc. - not handled yet
            continue;
        }
        if (next_is_extended) {
            next_is_extended = false;
            continue;
        }

        bool released = (scancode & SCANCODE_RELEASED) != 0;
        uint8_t key = scancode & ~SCANCODE_RELEASED;

        if (key == SCANCODE_LEFT_SHIFT || key == SCANCODE_RIGHT_SHIFT) {
            shift_held = !released;
            continue;
        }
        if (released) {
            continue;  // we only care about key presses, not releases
        }
        if (key == SCANCODE_CAPS_LOCK) {
            caps_lock_on = !caps_lock_on;
            continue;
        }

        char c = shift_held ? keymap_shift[key] : keymap_normal[key];
        if (c == 0) {
            continue;  // a key we don't understand yet
        }

        // Caps Lock flips letters only (not numbers or symbols).
        if (caps_lock_on && is_letter(c)) {
            c = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : (char)(c - 'A' + 'a');
        }
        return c;
    }
}
