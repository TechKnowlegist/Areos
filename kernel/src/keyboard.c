// keyboard.c - AREOS keyboard driver (PS/2).
//
// When you press or release a key, the keyboard sends a number called a
// "scancode". It isn't a letter: it says WHICH physical key moved.
// This file turns those numbers into key events, and key events into
// letters.

#include "keyboard.h"
#include "io.h"
#include "idt.h"

#define KEYBOARD_DATA_PORT    0x60  // where scancodes arrive
#define KEYBOARD_STATUS_PORT  0x64  // tells us if a scancode is waiting
#define STATUS_OUTPUT_FULL    0x01  // bit set = there's a byte to read

#define SCANCODE_RELEASED     0x80  // added to a scancode when a key is let go
#define SCANCODE_EXTENDED     0xE0  // prefix for extra keys (arrows etc.)

// A few scancodes we treat specially.
#define SC_ESCAPE      0x01
#define SC_CTRL        0x1D
#define SC_LEFT_SHIFT  0x2A
#define SC_RIGHT_SHIFT 0x36
#define SC_ALT         0x38
#define SC_CAPS_LOCK   0x3A
#define SC_F1          0x3B   // F1-F10 are 0x3B-0x44
#define SC_F10         0x44
#define SC_F11         0x57
#define SC_F12         0x58
// After the 0xE0 prefix:
#define SC_EXT_ENTER   0x1C   // keypad Enter
#define SC_EXT_UP      0x48
#define SC_EXT_LEFT    0x4B
#define SC_EXT_RIGHT   0x4D
#define SC_EXT_DOWN    0x50

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

// Scancodes waiting to be decoded. The keyboard interrupt adds to the
// end; the functions below take from the front. It's a "ring": when the
// end reaches the last slot it wraps back to the first.
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

// A decoded key, plus where it came from (needed to work out its character).
struct raw_event {
    struct key_event event;
    uint8_t scancode;
    bool extended;
};

// Which KEY_ code a scancode means.
static uint8_t key_for_scancode(uint8_t scancode, bool extended) {
    if (extended) {
        switch (scancode) {
        case SC_EXT_UP:    return KEY_UP;
        case SC_EXT_DOWN:  return KEY_DOWN;
        case SC_EXT_LEFT:  return KEY_LEFT;
        case SC_EXT_RIGHT: return KEY_RIGHT;
        case SC_EXT_ENTER: return KEY_ENTER;
        case SC_CTRL:      return KEY_CTRL;   // right Ctrl
        case SC_ALT:       return KEY_ALT;    // right Alt
        default:           return 0;          // a key we don't use yet
        }
    }
    switch (scancode) {
    case SC_ESCAPE:      return KEY_ESCAPE;
    case SC_CTRL:        return KEY_CTRL;
    case SC_LEFT_SHIFT:
    case SC_RIGHT_SHIFT: return KEY_SHIFT;
    case SC_ALT:         return KEY_ALT;
    case SC_F11:         return KEY_F1 + 10;
    case SC_F12:         return KEY_F1 + 11;
    default:
        if (scancode >= SC_F1 && scancode <= SC_F10) {
            return (uint8_t)(KEY_F1 + (scancode - SC_F1));
        }
        return (uint8_t)keymap_normal[scancode];  // 0 if it isn't a character key
    }
}

// Take scancodes off the buffer until one makes a complete key event.
static bool next_raw_event(struct raw_event *out) {
    while (buffer_head != buffer_tail) {
        uint8_t scancode = buffer[buffer_head];
        buffer_head = (buffer_head + 1) % BUFFER_SIZE;

        if (scancode == SCANCODE_EXTENDED) {
            next_is_extended = true;
            continue;
        }
        bool extended = next_is_extended;
        next_is_extended = false;

        bool pressed = (scancode & SCANCODE_RELEASED) == 0;
        uint8_t code = scancode & ~SCANCODE_RELEASED;

        if (!extended && (code == SC_LEFT_SHIFT || code == SC_RIGHT_SHIFT)) {
            shift_held = pressed;
        }
        if (!extended && code == SC_CAPS_LOCK) {
            if (pressed) {
                caps_lock_on = !caps_lock_on;
            }
            continue;
        }

        uint8_t key = key_for_scancode(code, extended);
        if (key == 0) {
            continue;
        }
        out->event.key = key;
        out->event.pressed = pressed;
        out->scancode = code;
        out->extended = extended;
        return true;
    }
    return false;
}

static bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// The character a key press types, taking Shift and Caps Lock into
// account. 0 if it doesn't type anything.
static char character_for(const struct raw_event *raw) {
    if (!raw->event.pressed) {
        return 0;
    }
    if (raw->extended) {
        return raw->event.key == KEY_ENTER ? KEY_ENTER : 0;
    }
    char c = shift_held ? keymap_shift[raw->scancode] : keymap_normal[raw->scancode];
    // Caps Lock flips letters only (not numbers or symbols).
    if (c != 0 && caps_lock_on && is_letter(c)) {
        c = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : (char)(c - 'A' + 'a');
    }
    return c;
}

bool keyboard_try_get_event(struct key_event *out) {
    struct raw_event raw;
    if (!next_raw_event(&raw)) {
        return false;
    }
    *out = raw.event;
    out->character = character_for(&raw);
    return true;
}

bool keyboard_try_get_char(char *out) {
    struct raw_event raw;
    while (next_raw_event(&raw)) {
        char c = character_for(&raw);
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
