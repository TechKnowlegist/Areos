// input.c - typing, from either the keyboard or the terminal.

#include <stdint.h>
#include "input.h"
#include "keyboard.h"
#include "serial.h"
#include "console.h"
#include "timer.h"

#define CURSOR_BLINK_MS 500

// Translate what terminals send into what AREOS expects.
static bool serial_try_get_key(char *out) {
    static int skip = 0;  // bytes left to ignore from an arrow-key code
    char c;
    while (serial_try_getc(&c)) {
        if (skip > 0) {
            skip--;
            continue;
        }
        if (c == 0x1B) {            // Escape: arrow keys send Esc [ A etc.
            skip = 2;
            continue;
        }
        if (c == '\r') {
            *out = KEY_ENTER;
            return true;
        }
        if (c == 0x7F || c == 0x08) {  // Backspace (terminals send either)
            *out = KEY_BACKSPACE;
            return true;
        }
        if (c >= 32 && c <= 126) {
            *out = c;
            return true;
        }
    }
    return false;
}

bool input_try_get_key(char *out) {
    return keyboard_try_get_char(out) || serial_try_get_key(out);
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
