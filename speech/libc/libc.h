// libc.h - the hooks between the speech engine's small C library and
// whatever it runs inside (the AREOS kernel, or the test program).

#ifndef SPEECH_LIBC_H
#define SPEECH_LIBC_H

#include <stddef.h>
#include <stdbool.h>

// Give the memory allocator a block of memory to hand out.
void speech_libc_set_memory(void *start, size_t size);
// How much of it is in use (for testing).
size_t speech_libc_memory_used(void);

// Something went badly wrong (Flite called exit or abort). Provided by the
// engine: it jumps back out of the synthesis that was running.
_Noreturn void speech_libc_fatal(const char *why);

// Where printf/fprintf text goes. Provided by whoever links the engine
// (the kernel throws it away; the test program prints it).
void speech_libc_debug_write(const char *text, size_t length);

#endif
