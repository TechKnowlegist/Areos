// cpu.h - things that control the processor itself.

#ifndef AREOS_CPU_H
#define AREOS_CPU_H

// Stop the processor for good. Nothing runs after this.
static inline void cpu_halt_forever(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

#endif
