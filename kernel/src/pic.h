// pic.h - the PIC (Programmable Interrupt Controller).
//
// The PIC is the chip that collects interrupts from hardware (timer,
// keyboard, ...) and passes them to the CPU one at a time. A PC has two
// of them chained together, giving 16 hardware interrupt lines (IRQs).

#ifndef AREOS_PIC_H
#define AREOS_PIC_H

#include <stdbool.h>

// Move the PIC's interrupts to vectors 32-47 and switch them all off.
void pic_init(void);

// Allow one IRQ through.
void pic_unmask(int irq);

// Tell the PIC we've finished handling an IRQ, so it can send the next one.
void pic_send_end_of_interrupt(int irq);

// True if IRQ 7 or 15 was a false alarm (and should be ignored).
bool pic_is_spurious(int irq);

#endif
