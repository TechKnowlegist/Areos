// pic.c - the PIC (Programmable Interrupt Controller).
//
// Out of the box, the PIC uses interrupt numbers 0-15, which crash into
// the CPU's own error numbers (0-31). So the first job is "remapping"
// it to 32-47. The setup sequence below is fixed by the chip's design.

#include <stdint.h>
#include "pic.h"
#include "idt.h"
#include "io.h"
#include "cpu.h"
#include "vmm.h"

#define PIC1_COMMAND  0x20   // main PIC (IRQs 0-7)
#define PIC1_DATA     0x21
#define PIC2_COMMAND  0xA0   // second PIC (IRQs 8-15), chained to main IRQ 2
#define PIC2_DATA     0xA1

#define PIC_INIT          0x11  // "start setup, 4 config bytes follow"
#define PIC_MODE_8086     0x01
#define PIC_END_OF_IRQ    0x20
#define PIC_READ_ISR      0x0B  // ask which IRQ is really being serviced

#define MSR_APIC_BASE        0x1B
#define APIC_BASE_ADDRESS    0xFFFFFFFFFF000ULL  // which bits of the MSR hold the address
#define APIC_LVT_LINT0       0x350               // the APIC's "input from the PIC" setting
#define APIC_DELIVER_EXTINT  (7 << 8)            // "pass the PIC's interrupts straight through", unmasked

// Give slow old hardware a moment between commands.
static void io_wait(void) {
    outb(0x80, 0);
}

void pic_init(void) {
    // Modern CPUs also have a newer interrupt controller, the "local APIC",
    // sitting between the PIC and the CPU. The bootloader leaves its input
    // from the PIC muted, so the timer and keyboard can't get through.
    // Un-mute it. (AREOS can use the APIC fully later, for multiple cores.)
    uint64_t apic_physical = cpu_read_msr(MSR_APIC_BASE) & APIC_BASE_ADDRESS;
    volatile uint8_t *apic = (volatile uint8_t *)vmm_map_hardware(apic_physical);
    *(volatile uint32_t *)(apic + APIC_LVT_LINT0) = APIC_DELIVER_EXTINT;

    outb(PIC1_COMMAND, PIC_INIT); io_wait();
    outb(PIC2_COMMAND, PIC_INIT); io_wait();
    outb(PIC1_DATA, IRQ_BASE);     io_wait();  // main PIC: IRQ 0-7  -> vectors 32-39
    outb(PIC2_DATA, IRQ_BASE + 8); io_wait();  // second:   IRQ 8-15 -> vectors 40-47
    outb(PIC1_DATA, 0x04);         io_wait();  // "second PIC is on your line 2"
    outb(PIC2_DATA, 0x02);         io_wait();  // "you are on line 2"
    outb(PIC1_DATA, PIC_MODE_8086); io_wait();
    outb(PIC2_DATA, PIC_MODE_8086); io_wait();

    // Mask (switch off) everything, except line 2 which links the two PICs.
    outb(PIC1_DATA, 0xFF & ~(1 << 2));
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(int irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    int line = irq % 8;
    outb(port, inb(port) & (uint8_t)~(1 << line));
}

void pic_send_end_of_interrupt(int irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_END_OF_IRQ);
    }
    outb(PIC1_COMMAND, PIC_END_OF_IRQ);
}

bool pic_is_spurious(int irq) {
    uint16_t command = (irq == 7) ? PIC1_COMMAND : PIC2_COMMAND;
    outb(command, PIC_READ_ISR);
    bool real = (inb(command) & (1 << 7)) != 0;
    if (!real && irq == 15) {
        // The main PIC did see line 2 fire, so it still needs its "done".
        outb(PIC1_COMMAND, PIC_END_OF_IRQ);
    }
    return !real;
}
