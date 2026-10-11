// countdown.c - counts down to liftoff.
//
// Built as ".arx in the native AREOS format" (packed by tools/arxpack),
// to prove AREOS runs both kinds.

#include "areos.h"

int main(void) {
    areos_set_color(AREOS_BLUE);
    areos_print("Native AREOS app reporting in. Countdown to liftoff:\n");

    for (int i = 5; i >= 1; i--) {
        areos_set_color(i <= 2 ? AREOS_WARNING : AREOS_GREEN);
        areos_print_number((uint64_t)i);
        areos_print("...\n");
        areos_sleep_ms(1000);
    }

    // Ask AREOS for some memory and use it, to show that works too.
    char *message = areos_more_memory(4096);
    if (message == NULL) {
        areos_print("No memory for the big finish.\n");
        return 1;
    }
    const char *text = "Liftoff! Next stop: Mars.\n";
    for (int i = 0; text[i] != '\0'; i++) {
        message[i] = text[i];
    }
    areos_set_color(AREOS_GREEN);
    areos_print(message);
    return 0;
}
