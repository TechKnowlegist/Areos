// pmm.h - Physical Memory Manager.
//
// RAM is split into "pages" of 4 KB each. The PMM keeps a checklist
// (a "bitmap": one bit per page) of which pages are in use and which
// are free, and hands out free ones when asked.

#ifndef AREOS_PMM_H
#define AREOS_PMM_H

#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#define PAGE_SIZE 4096

// Learn the memory layout from the bootloader and build the checklist.
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

// Get `count` pages in a row. Returns a pointer to the first one,
// or NULL if there isn't that much free memory in one piece.
void *pmm_alloc_pages(size_t count);

// Give pages back so they can be used again.
void pmm_free_pages(void *pointer, size_t count);

// Totals, in bytes.
uint64_t pmm_total_bytes(void);
uint64_t pmm_free_bytes(void);

#endif
