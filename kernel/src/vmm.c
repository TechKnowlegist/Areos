// vmm.c - Virtual Memory Manager (the beginnings of one).
//
// x86-64 page tables have 4 levels. A virtual address is split into
// four 9-bit numbers, one to pick an entry at each level, plus a 12-bit
// position inside the final 4 KB page:
//
//   level 4 (PML4) -> level 3 (PDPT) -> level 2 (PD) -> level 1 (PT) -> page
//
// To map an address we walk down the levels, creating any missing
// tables on the way, and fill in the final entry.

#include <stdbool.h>
#include <stddef.h>
#include "vmm.h"
#include "pmm.h"
#include "memory.h"
#include "panic.h"

#define ENTRY_PRESENT    (1ULL << 0)
#define ENTRY_WRITABLE   (1ULL << 1)
#define ENTRY_WRITE_THRU (1ULL << 3)
#define ENTRY_NO_CACHE   (1ULL << 4)
#define ENTRY_HUGE       (1ULL << 7)   // this entry is a big 2 MB / 1 GB page
#define ENTRY_ADDRESS    0x000FFFFFFFFFF000ULL

static uint64_t hhdm = 0;

void vmm_init(uint64_t hhdm_offset) {
    hhdm = hhdm_offset;
}

// The physical address of the top-level table lives in register CR3.
static uint64_t *top_level_table(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return (uint64_t *)((cr3 & ENTRY_ADDRESS) + hhdm);
}

// Find the table that `entry` points to, creating it if it doesn't exist.
// Returns NULL if `entry` is a huge page (already mapped, nothing to do).
static uint64_t *next_level(uint64_t *entry) {
    if (*entry & ENTRY_PRESENT) {
        if (*entry & ENTRY_HUGE) {
            return NULL;
        }
        return (uint64_t *)((*entry & ENTRY_ADDRESS) + hhdm);
    }
    uint64_t *table = pmm_alloc_pages(1);
    if (table == NULL) {
        PANIC("Out of memory while building page tables");
    }
    memset(table, 0, PAGE_SIZE);
    *entry = ((uint64_t)table - hhdm) | ENTRY_PRESENT | ENTRY_WRITABLE;
    return table;
}

volatile void *vmm_map_hardware(uint64_t physical) {
    physical &= ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t virtual_address = physical + hhdm;

    uint64_t *table = top_level_table();
    for (int level = 3; level >= 1; level--) {
        size_t index = (virtual_address >> (12 + 9 * level)) & 0x1FF;
        table = next_level(&table[index]);
        if (table == NULL) {
            return (volatile void *)virtual_address;  // already covered by a huge page
        }
    }
    size_t index = (virtual_address >> 12) & 0x1FF;
    table[index] = physical | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_WRITE_THRU | ENTRY_NO_CACHE;

    // Tell the CPU to forget any old translation it remembered for this address.
    asm volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return (volatile void *)virtual_address;
}
