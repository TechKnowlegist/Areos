// timer.c - AREOS's clock.

#include "speech.h"
#include "thread.h"
#include "timer.h"
#include "idt.h"
#include "io.h"

#define PIT_CHANNEL0     0x40
#define PIT_COMMAND      0x43
#define PIT_BASE_HZ      1193182  // the PIT's built-in crystal speed
#define PIT_MODE_SQUARE  0x36     // channel 0, low+high byte, square wave

// "volatile" because the timer interrupt changes it behind the code's back.
static volatile uint64_t ticks = 0;

static void on_tick(struct interrupt_frame *frame) {
    (void)frame;
    ticks++;
    speech_tick();   // keep the sound card fed, speak paused lines
}

void timer_init(void) {
    uint16_t divisor = PIT_BASE_HZ / TIMER_HZ;
    outb(PIT_COMMAND, PIT_MODE_SQUARE);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
    irq_set_handler(IRQ_TIMER, on_tick);
}

uint64_t timer_ms(void) {
    return ticks * 1000 / TIMER_HZ;
}

void timer_sleep_ms(uint64_t ms) {
    uint64_t wake_at = timer_ms() + ms;
    while (timer_ms() < wake_at) {
        thread_idle(wake_at);  // rest (and let speech work) until the next interrupt
    }
}
