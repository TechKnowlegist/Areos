// thread.c - switching the CPU between main and the speech thread.
//
// How a switch works: when an interrupt arrives, interrupts.S saves every
// register of whatever was running onto its stack (the "frame"). Normally
// the same frame is restored afterwards. To switch, we remember that frame
// and restore the *other* thread's saved frame instead, so the interrupt
// "returns" into the other thread. Floating point registers are saved and
// restored by hand, since only the speech thread uses them.

#include "thread.h"
#include "timer.h"
#include "heap.h"
#include "cpu.h"
#include "idt.h"

#define SPEECH_STACK_SIZE (512 * 1024)

// When both threads are busy, main gets 3 ms, then speech gets 2 ms.
#define MAIN_SLICE_MS   3
#define SPEECH_SLICE_MS 2

struct context {
    struct interrupt_frame *frame;                 // saved registers while not running
    uint8_t floating_point[512] __attribute__((aligned(16)));
};

enum { MAIN = 0, SPEECH = 1 };

static struct context contexts[2];
static int running = MAIN;
static bool speech_exists = false;
static bool (*speech_runnable)(void);
static uint64_t slice_started;

// Main's resting state, set by thread_idle.
static volatile bool main_idle = false;
static volatile uint64_t main_wake_at = 0;
static volatile bool main_woken = false;   // input arrived while main was resting

void thread_start_speech(void (*entry)(void), bool (*runnable)(void)) {
    uint8_t *stack = kmalloc(SPEECH_STACK_SIZE);
    if (stack == NULL) {
        return;
    }
    uint64_t top = ((uint64_t)stack + SPEECH_STACK_SIZE) & ~(uint64_t)15;

    // A frame that "returns" into entry() with interrupts on, as if the
    // speech thread had been interrupted just before its first instruction.
    struct interrupt_frame *frame = (struct interrupt_frame *)(top - 8 - sizeof(struct interrupt_frame));
    uint8_t *bytes = (uint8_t *)frame;
    for (size_t i = 0; i < sizeof(*frame); i++) bytes[i] = 0;
    uint16_t cs, ss;
    asm volatile ("mov %%cs, %0" : "=r"(cs));
    asm volatile ("mov %%ss, %0" : "=r"(ss));
    frame->rip = (uint64_t)entry;
    frame->cs = cs;
    frame->rflags = 0x202;          // interrupts on
    frame->rsp = top - 8;           // as if entry() had just been called
    frame->ss = ss;

    // Speech starts with clean floating point registers.
    uint64_t flags = cpu_interrupts_save();
    cpu_save_floating_point(contexts[MAIN].floating_point);
    uint32_t mxcsr = 0x1F80;
    asm volatile ("fninit; ldmxcsr %0" : : "m"(mxcsr));
    cpu_save_floating_point(contexts[SPEECH].floating_point);
    cpu_restore_floating_point(contexts[MAIN].floating_point);

    contexts[SPEECH].frame = frame;
    speech_runnable = runnable;
    speech_exists = true;
    cpu_interrupts_restore(flags);
}

static struct interrupt_frame *switch_to(int next, struct interrupt_frame *frame) {
    contexts[running].frame = frame;
    cpu_save_floating_point(contexts[running].floating_point);
    running = next;
    cpu_restore_floating_point(contexts[running].floating_point);
    slice_started = timer_ms();
    return contexts[running].frame;
}

struct interrupt_frame *thread_schedule(struct interrupt_frame *frame, int irq) {
    if (!speech_exists) {
        return frame;
    }
    if (irq != IRQ_TIMER && main_idle) {
        main_woken = true;   // a key, the mouse or the terminal: main may want to react
    }
    uint64_t now = timer_ms();
    uint64_t used = now - slice_started;
    bool main_resting = main_idle && !main_woken && now < main_wake_at;

    if (running == MAIN) {
        bool speech_wants = speech_runnable();
        if (speech_wants && (main_resting || used >= MAIN_SLICE_MS)) {
            return switch_to(SPEECH, frame);
        }
    } else {
        bool speech_wants = speech_runnable();
        if (!speech_wants || (!main_resting && used >= SPEECH_SLICE_MS)) {
            return switch_to(MAIN, frame);
        }
    }
    return frame;
}

struct interrupt_frame *thread_speech_crashed(void) {
    speech_exists = false;
    running = MAIN;
    cpu_restore_floating_point(contexts[MAIN].floating_point);
    return contexts[MAIN].frame;
}

bool thread_in_speech(void) {
    return speech_exists && running == SPEECH;
}

void thread_idle(uint64_t wake_at_ms) {
    if (running != MAIN) {
        asm volatile ("hlt");
        return;
    }
    asm volatile ("cli");
    main_wake_at = wake_at_ms;
    main_woken = false;
    main_idle = true;
    asm volatile ("sti; hlt");   // sti waits one instruction, so no tick slips in between
    main_idle = false;
}
