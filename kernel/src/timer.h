// timer.h - AREOS's clock.
//
// The PIT (Programmable Interval Timer) is a chip that can "tick" at a
// steady rate. We set it to 1000 ticks a second, so each tick is one
// millisecond. Doom will use this to run at the right speed.

#ifndef AREOS_TIMER_H
#define AREOS_TIMER_H

#include <stdint.h>

#define TIMER_HZ 1000  // ticks per second

void timer_init(void);

// Milliseconds since AREOS started.
uint64_t timer_ms(void);

// Wait this many milliseconds. The CPU rests while waiting.
void timer_sleep_ms(uint64_t ms);

#endif
