// oops.c - a program that crashes on purpose.
//
// It tries to write into the kernel's memory, which programs aren't
// allowed to touch. The CPU stops it, AREOS reports what happened, and
// everything else keeps running. That's the point of the walls between
// programs and the kernel.

#include "areos.h"

int main(void) {
    areos_set_color(AREOS_WARNING);
    areos_print("I'm about to do something I'm not allowed to do...\n");
    areos_sleep_ms(500);

    volatile uint64_t *kernel_memory = (volatile uint64_t *)0xFFFFFFFF80000000ULL;
    *kernel_memory = 0xBAD;

    areos_print("If you can read this, the walls are broken!\n");
    return 1;
}
