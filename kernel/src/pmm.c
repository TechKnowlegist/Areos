// pmm.c - Physical Memory Manager.
//
// How it works:
//  1. The bootloader gives us a "memory map": a list of regions of RAM
//     and what each one is (usable, reserved, broken, etc).
//  2. We make a bitmap with one bit per 4 KB page. 1 = in use, 0 = free.
//  3. Everything starts as "in use", then we mark the usable regions free.
//  4. The bitmap itself lives in RAM too, so we mark its own pages in use.
//
// Limine maps all of physical memory at a fixed offset ("HHDM"), so the
// physical address X can be reached at the pointer X + hhdm_offset.

#include <stdbool.h>
#include "pmm.h"
#include "panic.h"

static uint8_t *bitmap = NULL;
static uint64_t bitmap_pages = 0;   // how many pages the bitmap tracks
static uint64_t hhdm = 0;
static uint64_t total_bytes = 0;
static uint64_t free_pages = 0;
static uint64_t search_start = 0;   // where to start looking next time

static bool page_is_used(uint64_t page) {
    return (bitmap[page / 8] & (1 << (page % 8))) != 0;
}

static void mark_used(uint64_t page) {
    bitmap[page / 8] |= (uint8_t)(1 << (page % 8));
}

static void mark_free(uint64_t page) {
    bitmap[page / 8] &= (uint8_t)~(1 << (page % 8));
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    hhdm = hhdm_offset;

    // Step 1: find the end of the highest usable memory, and add up the total.
    uint64_t highest_address = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        total_bytes += entry->length;
        if (entry->base + entry->length > highest_address) {
            highest_address = entry->base + entry->length;
        }
    }
    bitmap_pages = highest_address / PAGE_SIZE;
    uint64_t bitmap_bytes = (bitmap_pages + 7) / 8;

    // Step 2: find a usable region big enough to hold the bitmap itself.
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_bytes) {
            bitmap = (uint8_t *)(entry->base + hhdm);
            break;
        }
    }
    if (bitmap == NULL) {
        PANIC("Not enough memory to track memory");
    }

    // Step 3: start with everything marked "in use"...
    for (uint64_t i = 0; i < bitmap_bytes; i++) {
        bitmap[i] = 0xFF;
    }

    // ...then free every page inside a usable region.
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        uint64_t first = (entry->base + PAGE_SIZE - 1) / PAGE_SIZE;  // round up
        uint64_t last = (entry->base + entry->length) / PAGE_SIZE;   // round down
        for (uint64_t page = first; page < last; page++) {
            mark_free(page);
            free_pages++;
        }
    }

    // Step 4: the bitmap's own pages are in use (by the bitmap!).
    uint64_t bitmap_first = ((uint64_t)bitmap - hhdm) / PAGE_SIZE;
    uint64_t bitmap_count = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t page = bitmap_first; page < bitmap_first + bitmap_count; page++) {
        if (!page_is_used(page)) {
            mark_used(page);
            free_pages--;
        }
    }

    // Never hand out page 0: a pointer to address 0 means "nothing" (NULL).
    if (bitmap_pages > 0 && !page_is_used(0)) {
        mark_used(0);
        free_pages--;
    }
}

void *pmm_alloc_pages(size_t count) {
    if (count == 0 || count > free_pages) {
        return NULL;
    }

    // Look for `count` free pages in a row. Start where we left off last
    // time (faster), and wrap around to the beginning once if needed.
    for (int pass = 0; pass < 2; pass++) {
        uint64_t start = (pass == 0) ? search_start : 0;
        uint64_t run_length = 0;
        for (uint64_t page = start; page < bitmap_pages; page++) {
            if (page_is_used(page)) {
                run_length = 0;
                continue;
            }
            run_length++;
            if (run_length == count) {
                uint64_t first = page - count + 1;
                for (uint64_t p = first; p <= page; p++) {
                    mark_used(p);
                }
                free_pages -= count;
                search_start = page + 1;
                return (void *)(first * PAGE_SIZE + hhdm);
            }
        }
    }
    return NULL;
}

void pmm_free_pages(void *pointer, size_t count) {
    uint64_t first = ((uint64_t)pointer - hhdm) / PAGE_SIZE;
    for (uint64_t page = first; page < first + count; page++) {
        ASSERT(page < bitmap_pages);
        ASSERT(page_is_used(page));  // freeing something twice is a bug
        mark_free(page);
        free_pages++;
    }
    if (first < search_start) {
        search_start = first;
    }
}

uint64_t pmm_total_bytes(void) {
    return total_bytes;
}

uint64_t pmm_free_bytes(void) {
    return free_pages * PAGE_SIZE;
}

uint64_t pmm_physical_address(const void *pointer) {
    return (uint64_t)pointer - hhdm;
}
