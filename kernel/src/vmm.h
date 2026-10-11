// vmm.h - Virtual Memory Manager (the beginnings of one).
//
// Programs don't use physical memory addresses directly. The CPU
// translates every address through "page tables": a lookup book that
// says "virtual address V is really physical address P". The VMM edits
// that book.
//
// For now it does one job: make hardware registers that live at fixed
// physical addresses (like the APIC's) reachable by the kernel.

#ifndef AREOS_VMM_H
#define AREOS_VMM_H

#include <stdint.h>

void vmm_init(uint64_t hhdm_offset);

// Make one 4 KB page of hardware registers at physical address `physical`
// reachable, and return a pointer to it. Uses uncached memory, as
// hardware registers must not be cached.
volatile void *vmm_map_hardware(uint64_t physical);

#endif
