// idt.c - interrupts.

#include <stddef.h>
#include "idt.h"
#include "pic.h"
#include "panic.h"

// One entry in the IDT. The CPU dictates this exact layout, which is why
// the handler's address is chopped into three pieces.
struct idt_gate {
    uint16_t address_low;
    uint16_t code_segment;
    uint8_t  ist;              // which emergency stack to use (0 = current)
    uint8_t  flags;            // "present, kernel only, interrupt gate"
    uint16_t address_middle;
    uint32_t address_high;
    uint32_t reserved;
} __attribute__((packed));

// What we hand to the `lidt` instruction: where the table is and its size.
struct idt_pointer {
    uint16_t size;
    uint64_t address;
} __attribute__((packed));

#define GATE_INTERRUPT_KERNEL 0x8E  // present | ring 0 | 64-bit interrupt gate

static struct idt_gate idt[256];
static irq_handler irq_handlers[16];

// The 256 stub addresses, from interrupts.S.
extern uint64_t interrupt_stub_table[256];

static uint16_t current_code_segment(void) {
    uint16_t cs;
    asm volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}

void idt_init(void) {
    uint16_t cs = current_code_segment();
    for (int vector = 0; vector < 256; vector++) {
        uint64_t address = interrupt_stub_table[vector];
        idt[vector].address_low    = (uint16_t)(address & 0xFFFF);
        idt[vector].code_segment   = cs;
        idt[vector].ist            = 0;
        idt[vector].flags          = GATE_INTERRUPT_KERNEL;
        idt[vector].address_middle = (uint16_t)((address >> 16) & 0xFFFF);
        idt[vector].address_high   = (uint32_t)(address >> 32);
        idt[vector].reserved       = 0;
    }

    struct idt_pointer pointer = {
        .size = sizeof(idt) - 1,
        .address = (uint64_t)idt,
    };
    asm volatile ("lidt %0" : : "m"(pointer));
}

void irq_set_handler(int irq, irq_handler handler) {
    irq_handlers[irq] = handler;
    pic_unmask(irq);
}

// Called by interrupts.S for every interrupt.
void interrupt_dispatch(struct interrupt_frame *frame) {
    if (frame->vector < IRQ_BASE) {
        // A CPU error. Nothing sensible to do but show the crash screen.
        kernel_panic_cpu(frame);
    }

    if (frame->vector < IRQ_BASE + 16) {
        int irq = (int)(frame->vector - IRQ_BASE);
        if (irq == 7 || irq == 15) {
            // Possibly a "spurious" interrupt: a false alarm from the PIC.
            if (pic_is_spurious(irq)) {
                return;
            }
        }
        if (irq_handlers[irq] != NULL) {
            irq_handlers[irq](frame);
        }
        pic_send_end_of_interrupt(irq);
    }
}
