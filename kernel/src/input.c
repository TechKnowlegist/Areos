// input.c - typing, from either the keyboard or the terminal.

#include <stdint.h>
#include "input.h"
#include "serial.h"
#include "console.h"
#include "timer.h"

#define CURSOR_BLINK_MS 500
#define ESCAPE_CODE_WAIT 2000  // how many times to check for the rest of an arrow code

// Read the next byte from the terminal, giving it a moment to arrive.
// Used for the 2nd and 3rd bytes of arrow-key codes, which come together.
static bool serial_next_byte(char *out) {
    for (int tries = 0; tries < ESCAPE_CODE_WAIT; tries++) {
        if (serial_try_getc(out)) {
            return true;
        }
    }
    return false;
}

// Read one key from the terminal and translate it into an AREOS key code.
// Terminals send Enter as '\r', Backspace as 127 (or 8), and arrow keys
// as three bytes: Escape, '[', then A/B/C/D.
static bool serial_try_get_code(uint8_t *out) {
    char c;
    while (serial_try_getc(&c)) {
        if (c == 0x1B) {
            char second, third;
            if (!serial_next_byte(&second)) {
                *out = KEY_ESCAPE;  // Escape on its own
                return true;
            }
            if ((second == '[' || second == 'O') && serial_next_byte(&third)) {
                switch (third) {
                case 'A': *out = KEY_UP;    return true;
                case 'B': *out = KEY_DOWN;  return true;
                case 'C': *out = KEY_RIGHT; return true;
                case 'D': *out = KEY_LEFT;  return true;
                case 'P': *out = KEY_F1;     return true;  // F1-F4 send Esc O P/Q/R/S
                case 'Q': *out = KEY_F1 + 1; return true;
                case 'R': *out = KEY_F1 + 2; return true;
                case 'S': *out = KEY_F1 + 3; return true;
                case 'Z': *out = KEY_BACK_TAB; return true;  // Shift+Tab
                default:  continue;  // some other special key: ignore
                }
            }
            continue;
        }
        if (c == '\r') {
            *out = KEY_ENTER;
            return true;
        }
        if (c == 0x7F || c == 0x08) {
            *out = KEY_BACKSPACE;
            return true;
        }
        if (c == '\t' || (c >= 32 && c <= 126)) {
            *out = (uint8_t)c;
            return true;
        }
    }
    return false;
}

bool input_try_get_key(char *out) {
    if (keyboard_try_get_char(out)) {
        return true;
    }
    uint8_t code;
    while (serial_try_get_code(&code)) {
        if (code < 0x80 && code != KEY_ESCAPE) {  // only things you can type
            *out = (char)code;
            return true;
        }
    }
    return false;
}

char input_wait_key(bool blink_cursor) {
    bool cursor_on = true;
    uint64_t next_blink = timer_ms() + CURSOR_BLINK_MS;
    char c;
    while (!input_try_get_key(&c)) {
        if (blink_cursor && timer_ms() >= next_blink) {
            cursor_on = !cursor_on;
            console_blink_cursor(cursor_on);
            next_blink = timer_ms() + CURSOR_BLINK_MS;
        }
        asm volatile ("hlt");  // rest until the next interrupt (key or tick)
    }
    if (blink_cursor) {
        console_blink_cursor(true);  // always show it right after a key press
    }
    return c;
}

static uint8_t to_lower(uint8_t key) {
    return (key >= 'A' && key <= 'Z') ? (uint8_t)(key - 'A' + 'a') : key;
}

// Key events made up from terminal typing, waiting to be handed out.
#define SERIAL_EVENTS_MAX 8
static struct key_event serial_events[SERIAL_EVENTS_MAX];
static int serial_events_count = 0;
static int serial_events_next = 0;

static void queue_serial_event(uint8_t key, bool pressed, char character) {
    if (serial_events_count < SERIAL_EVENTS_MAX) {
        serial_events[serial_events_count].key = key;
        serial_events[serial_events_count].pressed = pressed;
        serial_events[serial_events_count].character = character;
        serial_events_count++;
    }
}

bool input_try_get_event(struct key_event *out) {
    // Hand out any made-up events left over from last time first.
    if (serial_events_next < serial_events_count) {
        *out = serial_events[serial_events_next++];
        return true;
    }
    serial_events_count = 0;
    serial_events_next = 0;

    if (keyboard_try_get_event(out)) {
        return true;
    }
    uint8_t code;
    if (!serial_try_get_code(&code)) {
        return false;
    }
    if (code == KEY_BACK_TAB) {
        // Shift+Tab: Shift down, Tab down, Tab up, Shift up.
        queue_serial_event(KEY_SHIFT, true, 0);
        queue_serial_event(KEY_TAB, true, 0);
        queue_serial_event(KEY_TAB, false, 0);
        queue_serial_event(KEY_SHIFT, false, 0);
    } else {
        char character = (code < 0x80 && code != KEY_ESCAPE) ? (char)code : 0;
        queue_serial_event(to_lower(code), true, character);
        queue_serial_event(to_lower(code), false, 0);
    }
    *out = serial_events[serial_events_next++];
    return true;
}
