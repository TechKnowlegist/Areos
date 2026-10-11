// heap.h - AREOS heap: memory in any size you want.
//
// The PMM only hands out whole 4 KB pages. Most things need less (a
// line of text) or a weirdly-sized chunk (a Doom level). The heap takes
// big pieces from the PMM and slices them up. These work like C's
// malloc/free, with a "k" for "kernel".

#ifndef AREOS_HEAP_H
#define AREOS_HEAP_H

#include <stddef.h>
#include <stdint.h>

void heap_init(void);

// Get `size` bytes. Returns NULL if out of memory.
void *kmalloc(size_t size);

// Same as kmalloc, but the memory starts out all zeros.
void *kcalloc(size_t count, size_t size);

// Make an earlier allocation bigger or smaller, keeping its contents.
void *krealloc(void *pointer, size_t new_size);

// Give memory back. kfree(NULL) is allowed and does nothing.
void kfree(void *pointer);

// Totals, in bytes.
uint64_t heap_total_bytes(void);
uint64_t heap_used_bytes(void);

#endif
