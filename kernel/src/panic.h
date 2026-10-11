// panic.h - the AREOS crash screen.
//
// When something goes so wrong that AREOS can't safely keep going, it
// "panics": stops everything and shows what happened. Calm message on
// top, real technical details underneath (handy for pasting to Claude).

#ifndef AREOS_PANIC_H
#define AREOS_PANIC_H

// Use PANIC("what went wrong"). It automatically adds the file and line.
#define PANIC(reason) kernel_panic((reason), __FILE__, __LINE__)

// Check something that must be true. If it isn't, panic and say what failed.
#define ASSERT(condition) \
    do { if (!(condition)) { PANIC("Check failed: " #condition); } } while (0)

__attribute__((noreturn))
void kernel_panic(const char *reason, const char *file, int line);

// The crash screen for CPU errors (like a bad memory address), with the
// CPU's registers. Called by the interrupt code, not directly.
struct interrupt_frame;
__attribute__((noreturn))
void kernel_panic_cpu(struct interrupt_frame *frame);

#endif
