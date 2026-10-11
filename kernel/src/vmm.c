// vmm.c - Virtual Memory Manager.
//
// x86-64 page tables have 4 levels. A virtual address is split into
// four 9-bit numbers, one to pick an entry at each level, plus a 12-bit
// position inside the final 4 KB page:
//
//   level 4 (PML4) -> level 3 (PDPT) -> level 2 (PD) -> level 1 (PT) -> page
//
// To map an address we walk down the levels, creating any missing
// tables on the way, and fill in the final entry.
//
// An address space's "handle" is the physical address of its level-4
// table: exactly the value the CPU's CR3 register wants.

#include "vmm.h"
#include "pmm.h"
#include "memory.h"
#include "panic.h"

#define ENTRY_PRESENT    (1ULL << 0)
#define ENTRY_WRITABLE   (1ULL << 1)
#define ENTRY_USER       (1ULL << 2)   // ring 3 (programs) may use it
#define ENTRY_WRITE_THRU (1ULL << 3)
#define ENTRY_NO_CACHE   (1ULL << 4)
#define ENTRY_HUGE       (1ULL << 7)   // this entry is a big 2 MB / 1 GB page
#define ENTRY_ADDRESS    0x000FFFFFFFFFF000ULL

#define ENTRIES_PER_TABLE  512
#define KERNEL_HALF_START  256   // level-4 entries 256-511 are the kernel

static uint64_t hhdm = 0;
static uint64_t kernel_space = 0;

static uint64_t *physical_to_pointer(uint64_t physical) {
    return (uint64_t *)(physical + hhdm);
}

static uint64_t read_cr3(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ENTRY_ADDRESS;
}

void vmm_init(uint64_t hhdm_offset) {
    hhdm = hhdm_offset;
    kernel_space = read_cr3();  // the bootloader's tables become the kernel's
}

static size_t index_at(uint64_t virtual_address, int level) {
    return (virtual_address >> (12 + 9 * level)) & 0x1FF;
}

// Find the table that `entry` points to. If missing and `create` is set,
// make a new empty one. Returns NULL if missing (and not created), or if
// `entry` is a huge page.
static uint64_t *next_level(uint64_t *entry, bool create, uint64_t extra_flags) {
    if (*entry & ENTRY_PRESENT) {
        if (*entry & ENTRY_HUGE) {
            return NULL;
        }
        *entry |= extra_flags;
        return physical_to_pointer(*entry & ENTRY_ADDRESS);
    }
    if (!create) {
        return NULL;
    }
    uint64_t *table = pmm_alloc_pages(1);
    if (table == NULL) {
        return NULL;
    }
    memset(table, 0, PAGE_SIZE);
    *entry = ((uint64_t)table - hhdm) | ENTRY_PRESENT | ENTRY_WRITABLE | extra_flags;
    return table;
}

// Walk to the level-1 entry for an address. Returns NULL if impossible.
static uint64_t *find_entry(uint64_t space, uint64_t virtual_address, bool create, uint64_t flags) {
    uint64_t *table = physical_to_pointer(space);
    for (int level = 3; level >= 1; level--) {
        table = next_level(&table[index_at(virtual_address, level)], create, flags);
        if (table == NULL) {
            return NULL;
        }
    }
    return &table[index_at(virtual_address, 0)];
}

volatile void *vmm_map_hardware(uint64_t physical) {
    physical &= ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t virtual_address = physical + hhdm;

    // If the bootloader already covered this with a huge page, we're done.
    uint64_t *table = physical_to_pointer(kernel_space);
    for (int level = 3; level >= 1; level--) {
        uint64_t *entry = &table[index_at(virtual_address, level)];
        if ((*entry & ENTRY_PRESENT) && (*entry & ENTRY_HUGE)) {
            return (volatile void *)virtual_address;
        }
        table = next_level(entry, true, 0);
        if (table == NULL) {
            PANIC("Out of memory while building page tables");
        }
    }
    table[index_at(virtual_address, 0)] =
        physical | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_WRITE_THRU | ENTRY_NO_CACHE;

    // Tell the CPU to forget any old translation it remembered for this address.
    asm volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return (volatile void *)virtual_address;
}

// ---- Address spaces for programs ----

uint64_t vmm_create_address_space(void) {
    uint64_t *top = pmm_alloc_pages(1);
    if (top == NULL) {
        return 0;
    }
    memset(top, 0, PAGE_SIZE);
    // Share the kernel half: same tables, so kernel changes show up everywhere.
    uint64_t *kernel_top = physical_to_pointer(kernel_space);
    for (int i = KERNEL_HALF_START; i < ENTRIES_PER_TABLE; i++) {
        top[i] = kernel_top[i];
    }
    return (uint64_t)top - hhdm;
}

bool vmm_map_user_pages(uint64_t space, uint64_t virtual_address, size_t pages) {
    virtual_address &= ~(uint64_t)(PAGE_SIZE - 1);
    if (virtual_address == 0 || virtual_address + pages * PAGE_SIZE > USER_SPACE_END) {
        return false;
    }
    for (size_t i = 0; i < pages; i++) {
        uint64_t address = virtual_address + i * PAGE_SIZE;
        uint64_t *entry = find_entry(space, address, true, ENTRY_USER);
        if (entry == NULL) {
            return false;
        }
        if (*entry & ENTRY_PRESENT) {
            continue;  // already mapped (two pieces of a program share a page)
        }
        void *page = pmm_alloc_pages(1);
        if (page == NULL) {
            return false;
        }
        memset(page, 0, PAGE_SIZE);
        *entry = ((uint64_t)page - hhdm) | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_USER;
    }
    return true;
}

bool vmm_copy_to_user(uint64_t space, uint64_t virtual_address, const void *data, size_t length) {
    const uint8_t *source = data;
    while (length > 0) {
        uint64_t *entry = find_entry(space, virtual_address, false, 0);
        if (entry == NULL || !(*entry & ENTRY_PRESENT)) {
            return false;
        }
        // Write through the kernel's view of that physical page.
        size_t offset = virtual_address % PAGE_SIZE;
        size_t chunk = PAGE_SIZE - offset;
        if (chunk > length) {
            chunk = length;
        }
        uint8_t *destination = (uint8_t *)physical_to_pointer(*entry & ENTRY_ADDRESS) + offset;
        memcpy(destination, source, chunk);
        source += chunk;
        virtual_address += chunk;
        length -= chunk;
    }
    return true;
}

void vmm_switch_to(uint64_t space) {
    asm volatile ("mov %0, %%cr3" : : "r"(space) : "memory");
}

void vmm_switch_to_kernel(void) {
    vmm_switch_to(kernel_space);
}

// Free a table and everything below it. `level` 1 tables point at pages.
static void free_table(uint64_t *table, int level) {
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (!(table[i] & ENTRY_PRESENT)) {
            continue;
        }
        uint64_t *below = physical_to_pointer(table[i] & ENTRY_ADDRESS);
        if (level > 1) {
            free_table(below, level - 1);
        } else {
            pmm_free_pages(below, 1);
        }
    }
    pmm_free_pages(table, 1);
}

void vmm_destroy_address_space(uint64_t space) {
    uint64_t *top = physical_to_pointer(space);
    // Only the program half. The kernel half is shared and must stay.
    for (int i = 0; i < KERNEL_HALF_START; i++) {
        if (top[i] & ENTRY_PRESENT) {
            free_table(physical_to_pointer(top[i] & ENTRY_ADDRESS), 3);
        }
    }
    pmm_free_pages(top, 1);
}

bool vmm_user_range_ok(uint64_t address, size_t length) {
    if (address + length < address || address + length > USER_SPACE_END) {
        return false;  // wraps around, or reaches into the kernel half
    }
    uint64_t space = read_cr3();
    uint64_t end = address + length;
    for (uint64_t page = address & ~(uint64_t)(PAGE_SIZE - 1); page < end; page += PAGE_SIZE) {
        uint64_t *entry = find_entry(space, page, false, 0);
        if (entry == NULL || (*entry & (ENTRY_PRESENT | ENTRY_USER)) != (ENTRY_PRESENT | ENTRY_USER)) {
            return false;
        }
    }
    return true;
}
