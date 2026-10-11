// panic.c - the AREOS crash screen.

#include "panic.h"
#include "console.h"
#include "cpu.h"
#include "lang_en.h"

void kernel_panic(const char *reason, const char *file, int line) {
    console_clear();

    // The friendly part.
    console_set_color(COLOR_GREEN);
    console_print(STR_PANIC_TITLE "\n");
    console_set_color(COLOR_BLUE);
    console_print(STR_PANIC_CALM "\n\n");

    // The technical part. Nothing here is sent anywhere - it stays on screen.
    console_set_color(COLOR_DIM);
    console_print(STR_PANIC_DETAILS "\n");
    console_set_color(COLOR_WARNING);
    console_print("  " STR_PANIC_ERROR);
    console_print(reason);
    console_print("\n  " STR_PANIC_WHERE);
    console_print(file);
    console_print(":");
    console_print_number((uint64_t)line);
    console_print("\n\n");

    console_set_color(COLOR_DIM);
    console_print(STR_PANIC_HINT "\n");
    console_hide_cursor();

    cpu_halt_forever();
}
