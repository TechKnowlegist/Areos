// areos.h - everything an AREOS program needs.
//
// Include this, write a main() that returns an int, and you have an
// AREOS program. See apps/hello.c for an example.
//
// Programs talk to AREOS through "system calls": put a request number in
// register rax, details in rdi and rsi, and run `int 0x80`. The functions
// below wrap that up so you never have to.
//
// The numbers here MUST match kernel/src/syscall.h.

#ifndef AREOS_H
#define AREOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AREOS_SYS_EXIT         0
#define AREOS_SYS_WRITE        1
#define AREOS_SYS_READ_KEY     2
#define AREOS_SYS_SLEEP_MS     3
#define AREOS_SYS_TIME_MS      4
#define AREOS_SYS_MORE_MEMORY  5
#define AREOS_SYS_SET_COLOR    6
#define AREOS_SYS_GRAPHICS_START 7
#define AREOS_SYS_GRAPHICS_DRAW  8
#define AREOS_SYS_GRAPHICS_STOP  9
#define AREOS_SYS_KEY_EVENT      10
#define AREOS_SYS_MOUSE_EVENT    11
#define AREOS_SYS_DATE_TIME      12
#define AREOS_SYS_SETTING_GET    13
#define AREOS_SYS_SETTING_SET    14
#define AREOS_SYS_APP_NAME       15
#define AREOS_SYS_LAUNCH         16
#define AREOS_SYS_POWER          17
#define AREOS_SYS_SPEECH         18

// Settings (they last until AREOS is turned off). MUST match kernel/src/settings.h.
#define AREOS_SETTING_SETUP_DONE  0   // 1 once setup has been finished
#define AREOS_SETTING_THEME       1   // 0 = dark, 1 = light
#define AREOS_SETTING_LANGUAGE    2   // 0 = English
#define AREOS_SETTING_VERBOSITY   3   // 0 = detailed announcements, 1 = brief
#define AREOS_SETTING_WALLPAPER   4   // 0 = nebula, 1 = red planet, 2 = plain
#define AREOS_SETTING_RETURN_TO   5   // desktop: which app it opened (+1), to come back to
#define AREOS_SETTING_SPEECH_OFF   6   // 0 = AREOS speaks (the default), 1 = quiet
#define AREOS_SETTING_SPEECH_SPEED 7   // 0 = normal, 1 = fast, 2 = very fast
#define AREOS_SETTING_NAME_FIRST  8   // 8-11: the user's name, 8 letters per setting

// The AREOS colors, so programs can match the system's look.
#define AREOS_GREEN    0x3DFF9A
#define AREOS_BLUE     0x4FC3F7
#define AREOS_DIM      0x7A8C9E
#define AREOS_WARNING  0xFFB347

// Keys. Letter keys are their lowercase letter ('a'), digits are '0'-'9'.
#define AREOS_KEY_ENTER      '\n'
#define AREOS_KEY_BACKSPACE  '\b'
#define AREOS_KEY_TAB        '\t'
#define AREOS_KEY_SPACE      ' '
#define AREOS_KEY_ESCAPE     27
#define AREOS_KEY_UP         0x80
#define AREOS_KEY_DOWN       0x81
#define AREOS_KEY_LEFT       0x82
#define AREOS_KEY_RIGHT      0x83
#define AREOS_KEY_CTRL       0x84
#define AREOS_KEY_SHIFT      0x85
#define AREOS_KEY_ALT        0x86
#define AREOS_KEY_SUPER      0x87   // the Windows key
#define AREOS_KEY_HOME       0x88
#define AREOS_KEY_END        0x89
#define AREOS_KEY_DELETE     0x8A
#define AREOS_KEY_PAGE_UP    0x8B
#define AREOS_KEY_PAGE_DOWN  0x8C
#define AREOS_KEY_F1         0x90   // F1 to F12 are 0x90 to 0x9B

static inline uint64_t areos_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t result;
    asm volatile ("int $0x80"
                  : "=a"(result)
                  : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
                  : "memory");
    return result;
}

static inline uint64_t areos_syscall(uint64_t number, uint64_t arg1, uint64_t arg2) {
    return areos_syscall3(number, arg1, arg2, 0);
}

// End the program. 0 means "everything went fine".
__attribute__((noreturn)) static inline void areos_exit(int code) {
    areos_syscall(AREOS_SYS_EXIT, (uint64_t)code, 0);
    for (;;) { }
}

// Print `length` characters of `text`.
static inline void areos_write(const char *text, size_t length) {
    areos_syscall(AREOS_SYS_WRITE, (uint64_t)text, length);
}

// Print a string.
static inline void areos_print(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    areos_write(text, length);
}

// Print a whole number.
static inline void areos_print_number(uint64_t number) {
    char digits[21];
    int count = 0;
    do {
        digits[20 - count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number > 0);
    areos_write(&digits[21 - count], (size_t)count);
}

// Wait for a key and return it (letters, AREOS_KEY_ENTER, AREOS_KEY_BACKSPACE).
static inline char areos_read_key(void) {
    return (char)areos_syscall(AREOS_SYS_READ_KEY, 0, 0);
}

// Pause for some milliseconds (1000 = one second).
static inline void areos_sleep_ms(uint64_t ms) {
    areos_syscall(AREOS_SYS_SLEEP_MS, ms, 0);
}

// Milliseconds since AREOS started.
static inline uint64_t areos_time_ms(void) {
    return areos_syscall(AREOS_SYS_TIME_MS, 0, 0);
}

// Get `bytes` of fresh, zeroed memory. Returns NULL if there isn't enough.
static inline void *areos_more_memory(size_t bytes) {
    return (void *)areos_syscall(AREOS_SYS_MORE_MEMORY, bytes, 0);
}

// Change the color of text printed after this (0xRRGGBB).
static inline void areos_set_color(uint32_t color) {
    areos_syscall(AREOS_SYS_SET_COLOR, color, 0);
}

// ---- Graphics ----
// Draw into your own grid of pixels (each one 0x00RRGGBB, row by row),
// then hand it to areos_graphics_draw. AREOS scales it up to fill the
// screen. Any text you print still reaches the terminal.

// Switch to graphics mode. Tells you the real screen size. False if it failed.
static inline bool areos_graphics_start(uint32_t *screen_width, uint32_t *screen_height) {
    uint64_t size = areos_syscall(AREOS_SYS_GRAPHICS_START, 0, 0);
    if (size == (uint64_t)-1) {
        return false;
    }
    if (screen_width != NULL) *screen_width = (uint32_t)(size >> 32);
    if (screen_height != NULL) *screen_height = (uint32_t)size;
    return true;
}

// Show a picture `width` x `height` pixels big.
static inline void areos_graphics_draw(const uint32_t *pixels, uint32_t width, uint32_t height) {
    areos_syscall3(AREOS_SYS_GRAPHICS_DRAW, (uint64_t)pixels, width, height);
}

// Back to text. (Happens by itself when the program ends.)
static inline void areos_graphics_stop(void) {
    areos_syscall(AREOS_SYS_GRAPHICS_STOP, 0, 0);
}

// ---- Key events (for games) ----

struct areos_key_event {
    uint8_t key;       // an AREOS_KEY_ code, or a lowercase letter/digit
    bool pressed;      // true = went down, false = came up
    char character;    // what the key types ('G', '!', ...), or 0
};

// If a key went down or up since last time, fill in *event and return
// true. Never waits. Call it in a loop until it returns false.
static inline bool areos_key_event(struct areos_key_event *event) {
    uint64_t result = areos_syscall(AREOS_SYS_KEY_EVENT, 0, 0);
    if (result == 0) {
        return false;
    }
    event->key = (uint8_t)(result & 0xFF);
    event->pressed = (result & 0x100) != 0;
    event->character = (char)((result >> 16) & 0xFF);
    return true;
}

// ---- Mouse ----

#define AREOS_MOUSE_LEFT    0x1
#define AREOS_MOUSE_RIGHT   0x2
#define AREOS_MOUSE_MIDDLE  0x4

struct areos_mouse_event {
    int dx, dy;        // how far it moved (right and DOWN are positive)
    uint8_t buttons;   // AREOS_MOUSE_ buttons held right now
};

// If the mouse moved or a button changed, fill in *event and return true.
static inline bool areos_mouse_event(struct areos_mouse_event *event) {
    uint64_t result = areos_syscall(AREOS_SYS_MOUSE_EVENT, 0, 0);
    if (result == 0) {
        return false;
    }
    event->dx = (int16_t)(result & 0xFFFF);
    event->dy = (int16_t)((result >> 16) & 0xFFFF);
    event->buttons = (uint8_t)((result >> 32) & 0x7);
    return true;
}

// ---- Date and time ----

struct areos_date_time {
    int year, month, day;        // month 1-12
    int hour, minute, second;    // hour 0-23
};

static inline struct areos_date_time areos_date_time(void) {
    uint64_t t = areos_syscall(AREOS_SYS_DATE_TIME, 0, 0);
    struct areos_date_time d = {
        .year = (int)((t >> 40) & 0xFFFF), .month = (int)((t >> 32) & 0xFF),
        .day = (int)((t >> 24) & 0xFF), .hour = (int)((t >> 16) & 0xFF),
        .minute = (int)((t >> 8) & 0xFF), .second = (int)(t & 0xFF),
    };
    return d;
}

// ---- Settings ----

static inline uint64_t areos_setting_get(int id) {
    return areos_syscall(AREOS_SYS_SETTING_GET, (uint64_t)id, 0);
}

static inline void areos_setting_set(int id, uint64_t value) {
    areos_syscall(AREOS_SYS_SETTING_SET, (uint64_t)id, value);
}

// ---- Apps ----

// Name of app number `index` (like "hello.arx"). Returns false past the last one.
static inline bool areos_app_name(int index, char *buffer, size_t size) {
    return areos_syscall3(AREOS_SYS_APP_NAME, (uint64_t)index, (uint64_t)buffer, size) != (uint64_t)-1;
}

// Ask for app `name` to run as soon as this program exits. If `come_back`
// is true, this program starts again afterwards. Returns false if there's
// no such app. (You still need to exit for it to happen.)
static inline bool areos_launch(const char *name, bool come_back) {
    return areos_syscall(AREOS_SYS_LAUNCH, (uint64_t)name, come_back ? 1 : 0) == 0;
}

// ---- Power ----

static inline void areos_power_off(void)  { areos_syscall(AREOS_SYS_POWER, 0, 0); }
static inline void areos_restart(void)    { areos_syscall(AREOS_SYS_POWER, 1, 0); }

// The built-in screen reader. Everything printed is read out anyway; these
// are for interrupting it, or saying something without printing it.
static inline void areos_speech_stop(void) { areos_syscall(AREOS_SYS_SPEECH, 0, 0); }
static inline void areos_speech_say(const char *text) {
    areos_syscall(AREOS_SYS_SPEECH, 1, (uint64_t)text);
}

// Read a line of typing into `buffer` (at most `size - 1` characters).
// Shows what's typed and handles Backspace. Returns the length.
static inline size_t areos_read_line(char *buffer, size_t size) {
    size_t length = 0;
    for (;;) {
        char c = areos_read_key();
        if (c == AREOS_KEY_ENTER) {
            areos_print("\n");
            buffer[length] = '\0';
            return length;
        }
        if (c == AREOS_KEY_BACKSPACE) {
            if (length > 0) {
                length--;
                areos_write("\b", 1);
            }
            continue;
        }
        if (length + 1 < size) {
            buffer[length++] = c;
            areos_write(&c, 1);
        }
    }
}

#endif
