// hello.c - the first AREOS app.
//
// Built as ".arx with ELF inside". Says hello, asks your name, and
// shows off a few system calls.

#include "areos.h"

int main(void) {
    areos_set_color(AREOS_GREEN);
    areos_print("Hello from the first AREOS app!\n");
    areos_set_color(AREOS_BLUE);
    areos_print("I'm a separate program, running in my own memory.\n");
    areos_print("What's your name? ");

    char name[64];
    areos_read_line(name, sizeof(name));
    if (name[0] == '\0') {
        areos_print("Mysterious. I like it.\n");
    } else {
        areos_print("Nice to meet you, ");
        areos_print(name);
        areos_print("!\n");
    }

    areos_set_color(AREOS_DIM);
    areos_print("AREOS has been running for ");
    areos_print_number(areos_time_ms() / 1000);
    areos_print(" seconds. Bye!\n");
    return 0;
}
