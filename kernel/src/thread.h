// thread.h - doing two things at once: the speech thread.
//
// Until now AREOS did one thing at a time. Speaking needs to happen *while*
// you use the desktop, so the kernel now has a second line of work (a
// "thread") just for the voice. The timer switches the CPU between them:
//
//   main    the command line, apps and the desktop (everything as before)
//   speech  turns text into sound
//
// Speech only gets the CPU when it has something to say, and gets all of
// it while main is resting (waiting for a key or sleeping).

#ifndef AREOS_THREAD_H
#define AREOS_THREAD_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "idt.h"

// Start the speech thread running `entry` on its own stack.
// `runnable` is asked on every tick: does speech have work right now?
void thread_start_speech(void (*entry)(void), bool (*runnable)(void));

// Called by interrupt_dispatch for each hardware interrupt: may switch
// threads. Returns the saved registers to resume.
struct interrupt_frame *thread_schedule(struct interrupt_frame *frame, int irq);

// The speech thread hit a CPU error: stop it for good, go back to main.
struct interrupt_frame *thread_speech_crashed(void);

// Is the speech thread the one running?
bool thread_in_speech(void);

// Main has nothing to do until `wake_at_ms` (or any key/mouse/serial
// input): let speech use the CPU meanwhile. Rests until the next interrupt.
void thread_idle(uint64_t wake_at_ms);

#endif
