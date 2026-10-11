// gdt.h - the GDT (Global Descriptor Table) and TSS (Task State Segment).
//
// The CPU has privilege "rings". The kernel runs in ring 0 (can do
// anything); programs run in ring 3 (can only touch their own memory).
// The GDT lists the "segments" for each ring. The TSS tells the CPU
// which stack to use when an interrupt pulls it from ring 3 into ring 0.

#ifndef AREOS_GDT_H
#define AREOS_GDT_H

#include <stdint.h>

// Selectors: how code refers to a GDT entry. The "| 3" marks ring 3.
#define SELECTOR_KERNEL_CODE  0x08
#define SELECTOR_KERNEL_DATA  0x10
#define SELECTOR_USER_DATA    (0x18 | 3)
#define SELECTOR_USER_CODE    (0x20 | 3)
#define SELECTOR_TSS          0x28

void gdt_init(void);

#endif
