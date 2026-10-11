// vmm.h - Virtual Memory Manager.
//
// Programs don't use physical memory addresses directly. The CPU
// translates every address through "page tables": a lookup book that
// says "virtual address V is really physical address P". The VMM edits
// that book.
//
// Each program gets its own book (an "address space"). The top half of
// every book is the same: the kernel, which programs can't touch. The
// bottom half belongs to the program alone, so programs can't see or
// break each other's memory, or the kernel's.

#ifndef AREOS_VMM_H
#define AREOS_VMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Program memory lives below this address; the kernel lives above it.
#define USER_SPACE_END 0x0000800000000000ULL

void vmm_init(uint64_t hhdm_offset);

// Make one 4 KB page of hardware registers at physical address `physical`
// reachable, and return a pointer to it. Uses uncached memory, as
// hardware registers must not be cached.
volatile void *vmm_map_hardware(uint64_t physical);

// ---- Address spaces for programs ----

// Make a new, empty address space (with the kernel half filled in).
// Returns its handle, or 0 if out of memory.
uint64_t vmm_create_address_space(void);

// Give the program `pages` fresh, zeroed pages starting at `virtual_address`.
// Returns false if out of memory or the address isn't in program space.
bool vmm_map_user_pages(uint64_t space, uint64_t virtual_address, size_t pages);

// Copy bytes into a program's memory (which must already be mapped).
bool vmm_copy_to_user(uint64_t space, uint64_t virtual_address, const void *data, size_t length);

// Switch the CPU to a program's address space, or back to the kernel's.
void vmm_switch_to(uint64_t space);
void vmm_switch_to_kernel(void);

// Free a program's address space and every page in its half.
void vmm_destroy_address_space(uint64_t space);

// True if [address, address + length) is all valid program memory in
// the current address space. Used to check pointers programs hand us.
bool vmm_user_range_ok(uint64_t address, size_t length);

#endif
