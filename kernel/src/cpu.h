// cpu.h - things that control the processor itself.

#ifndef AREOS_CPU_H
#define AREOS_CPU_H

#include <stdint.h>

// MSRs ("model-specific registers") are special CPU settings.
static inline uint64_t cpu_read_msr(uint32_t msr) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void cpu_write_msr(uint32_t msr, uint64_t value) {
    asm volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

// Stop the processor for good. Nothing runs after this.
__attribute__((noreturn))
static inline void cpu_halt_forever(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

#endif
