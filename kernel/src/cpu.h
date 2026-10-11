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

// Turn on SSE, the CPU's floating point unit for decimals. The kernel
// itself never uses it, but the speech engine (and some apps) need it.
static inline void cpu_enable_floating_point(void) {
    uint64_t cr0, cr4;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ull << 2);   // EM off: no x87 emulation
    cr0 |= (1ull << 1);    // MP on
    asm volatile ("mov %0, %%cr0" : : "r"(cr0));
    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 9) | (1ull << 10);   // OSFXSR + OSXMMEXCPT: SSE allowed
    asm volatile ("mov %0, %%cr4" : : "r"(cr4));
    uint32_t mxcsr = 0x1F80;             // all floating point errors quietly ignored
    asm volatile ("fninit; ldmxcsr %0" : : "m"(mxcsr));
}

// Save / restore every floating point register (512 bytes, 16-byte aligned).
static inline void cpu_save_floating_point(void *area) {
    asm volatile ("fxsave64 (%0)" : : "r"(area) : "memory");
}
static inline void cpu_restore_floating_point(const void *area) {
    asm volatile ("fxrstor64 (%0)" : : "r"(area) : "memory");
}

// Turn interrupts off for a moment, remembering whether they were on.
static inline uint64_t cpu_interrupts_save(void) {
    uint64_t flags;
    asm volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static inline void cpu_interrupts_restore(uint64_t flags) {
    if (flags & (1 << 9)) {
        asm volatile ("sti" : : : "memory");
    }
}

// Stop the processor for good. Nothing runs after this.
__attribute__((noreturn))
static inline void cpu_halt_forever(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

#endif
