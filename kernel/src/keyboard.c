// keyboard.c - AREOS keyboard driver (PS/2).
//
// When you press or release a key, the keyboard sends a number called a
// "scancode". It isn't a letter: it says WHICH physical key moved.
// This file turns those numbers into letters.

#include <stdbool.h>
#include <stdint.h>
#include "keyboard.h"
#include "io.h"
#include "idt.h"

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

// Scancodes waiting to be turned into characters. The keyboard interrupt
// adds to the end; keyboard_try_get_char takes from the front. It's a
// "ring": when the end reaches the last slot it wraps back to the first.
#define BUFFER_SIZE 128
static volatile uint8_t buffer[BUFFER_SIZE];
static volatile uint32_t buffer_head = 0;  // next slot to read
static volatile uint32_t buffer_tail = 0;  // next slot to write

// Runs every time a key goes down or up (hardware interrupt 1).
static void on_keyboard_interrupt(struct interrupt_frame *frame) {
    (void)frame;
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    uint32_t next = (buffer_tail + 1) % BUFFER_SIZE;
    if (next != buffer_head) {  // if the buffer is full, drop the key
        buffer[buffer_tail] = scancode;
        buffer_tail = next;
    }
}

void keyboard_init(void) {
    // Throw away anything left over from before AREOS started.
    while (inb(KEYBOARD_STATUS_PORT) & STATUS_OUTPUT_FULL) {
        inb(KEYBOARD_DATA_PORT);
    }
    irq_set_handler(IRQ_KEYBOARD, on_keyboard_interrupt);
}

static bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Turn one scancode into a character, or 0 if it doesn't make one
// (like releasing a key, or pressing Shift on its own).
static char translate(uint8_t scancode) {
    if (scancode == SCANCODE_EXTENDED) {
        next_is_extended = true;  // arrows etc. - not handled yet
        return 0;
    }
    if (next_is_extended) {
        next_is_extended = false;
        return 0;
    }

    bool released = (scancode & SCANCODE_RELEASED) != 0;
    uint8_t key = scancode & ~SCANCODE_RELEASED;

    if (key == SCANCODE_LEFT_SHIFT || key == SCANCODE_RIGHT_SHIFT) {
        shift_held = !released;
        return 0;
    }
    if (released) {
        return 0;  // we only care about key presses, not releases
    }
    if (key == SCANCODE_CAPS_LOCK) {
        caps_lock_on = !caps_lock_on;
        return 0;
    }

    char c = shift_held ? keymap_shift[key] : keymap_normal[key];

    // Caps Lock flips letters only (not numbers or symbols).
    if (c != 0 && caps_lock_on && is_letter(c)) {
        c = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : (char)(c - 'A' + 'a');
    }
    return c;
}

bool keyboard_try_get_char(char *out) {
    while (buffer_head != buffer_tail) {
        uint8_t scancode = buffer[buffer_head];
        buffer_head = (buffer_head + 1) % BUFFER_SIZE;
        char c = translate(scancode);
        if (c != 0) {
            *out = c;
            return true;
        }
    }
    return false;
}

char keyboard_get_char(void) {
    char c;
    while (!keyboard_try_get_char(&c)) {
        asm volatile ("hlt");  // rest until the next interrupt
    }
    return c;
}
