// power.c - turning the computer off, or restarting it.

#include "power.h"
#include "console.h"
#include "graphics.h"
#include "timer.h"
#include "idt.h"
#include "io.h"
#include "cpu.h"
#include "lang_en.h"

void power_off(void) {
    graphics_stop();  // so any message below can be seen
    console_print(STR_TURNING_OFF "\n");
    // QEMU's virtual power button. Real hardware needs ACPI (much later).
    outw(0x604, 0x2000);
    // Powering off takes a moment. If we're still here after a second,
    // the machine didn't turn off by itself.
    interrupts_enable();
    timer_sleep_ms(1000);
    console_set_color(COLOR_WARNING);
    console_print(STR_OFF_FAILED "\n");
    cpu_halt_forever();
}

void power_restart(void) {
    graphics_stop();
    console_print(STR_RESTARTING "\n");
    // Ask the keyboard controller to pulse the CPU's reset line.
    // (Odd but true: this is the classic way PCs reboot.)
    outb(0x64, 0xFE);
    cpu_halt_forever();
}
