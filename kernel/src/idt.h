// idt.h - interrupts: how the CPU tells AREOS that something happened.
//
// The IDT (Interrupt Descriptor Table) is a list the CPU reads to find
// out which code to run for each of the 256 interrupt numbers.
//   0-31   : CPU errors ("exceptions"), like dividing by zero or a bad address
//   32-47  : hardware, like the timer (32) and the keyboard (33)
//   48-255 : unused for now

#ifndef AREOS_IDT_H
#define AREOS_IDT_H

#include <stdint.h>

#define IRQ_BASE      32            // hardware interrupts start at vector 32
#define IRQ_TIMER     0             // hardware interrupt numbers ("IRQs")
#define IRQ_KEYBOARD  1

// Everything the CPU and our stub saved when the interrupt happened.
// The order here must match interrupts.S exactly.
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;      // which interrupt this is
    uint64_t error_code;  // extra info for some CPU errors (0 otherwise)
    uint64_t rip;         // the instruction that was running
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;         // the stack at that moment
    uint64_t ss;
};

typedef void (*irq_handler)(struct interrupt_frame *frame);

// Build the IDT and tell the CPU about it.
void idt_init(void);

// Run `handler` whenever hardware interrupt `irq` fires.
void irq_set_handler(int irq, irq_handler handler);

// Turn interrupts on/off for the whole CPU.
static inline void interrupts_enable(void)  { asm volatile ("sti"); }
static inline void interrupts_disable(void) { asm volatile ("cli"); }

#endif
