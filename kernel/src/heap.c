// heap.c - AREOS heap.
//
// How it works:
// The heap is a chain of "blocks". Each block starts with a small label
// (the header) saying how big it is and whether it's free. kmalloc walks
// the chain looking for a free block that's big enough ("first fit"),
// and splits it if it's much bigger than needed. kfree marks a block
// free again and glues it to free neighbours so memory doesn't end up
// in tiny useless crumbs. If nothing fits, the heap grabs more pages.

#include <stdbool.h>
#include "heap.h"
#include "pmm.h"
#include "memory.h"
#include "panic.h"

#define ALIGNMENT        16                 // every allocation starts on a 16-byte boundary
#define HEAP_START_SIZE  (32 * 1024 * 1024) // try to start with 32 MB
#define HEAP_MIN_GROW    (4 * 1024 * 1024)  // grow by at least 4 MB at a time
#define BLOCK_MAGIC      0xA2E05A2E05ULL    // "AREOS AREOS"-ish: spots corrupted headers

struct block {
    uint64_t magic;       // always BLOCK_MAGIC; if not, something scribbled on memory
    size_t size;          // bytes available after the header
    bool free;
    struct block *next;   // next block, in address order
};

// Header size rounded up so the memory after it stays aligned.
#define HEADER_SIZE ((sizeof(struct block) + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1))

static struct block *first_block = NULL;
static uint64_t total_bytes = 0;
static uint64_t used_bytes = 0;

static size_t round_up(size_t value, size_t multiple) {
    return (value + multiple - 1) / multiple * multiple;
}

static void *block_data(struct block *b) {
    return (uint8_t *)b + HEADER_SIZE;
}

static struct block *block_from_data(void *pointer) {
    struct block *b = (struct block *)((uint8_t *)pointer - HEADER_SIZE);
    if (b->magic != BLOCK_MAGIC) {
        PANIC("Heap: freed or resized memory that wasn't from kmalloc");
    }
    return b;
}

// True if block `b` ends exactly where `next` begins.
static bool touching(struct block *b, struct block *next) {
    return (uint8_t *)block_data(b) + b->size == (uint8_t *)next;
}

// Glue `b` together with the free blocks right after it.
static void merge_with_next(struct block *b) {
    if (!b->free) {
        return;  // never swallow free memory into a block that's in use
    }
    while (b->next != NULL && b->next->free && touching(b, b->next)) {
        b->size += HEADER_SIZE + b->next->size;
        b->next = b->next->next;
    }
}

// Ask the PMM for more memory and add it to the chain as one free block.
static bool grow(size_t bytes) {
    size_t pages = round_up(bytes, PAGE_SIZE) / PAGE_SIZE;
    struct block *b = pmm_alloc_pages(pages);
    if (b == NULL) {
        return false;
    }
    b->magic = BLOCK_MAGIC;
    b->size = pages * PAGE_SIZE - HEADER_SIZE;
    b->free = true;
    total_bytes += pages * PAGE_SIZE;

    // Insert in address order so neighbours can be merged.
    if (first_block == NULL || b < first_block) {
        b->next = first_block;
        first_block = b;
    } else {
        struct block *before = first_block;
        while (before->next != NULL && before->next < b) {
            before = before->next;
        }
        b->next = before->next;
        before->next = b;
        merge_with_next(before);
    }
    merge_with_next(b);
    return true;
}

void heap_init(void) {
    // Start big, but settle for less on a machine with little memory.
    for (size_t size = HEAP_START_SIZE; size >= PAGE_SIZE * 4; size /= 2) {
        if (grow(size)) {
            return;
        }
    }
    PANIC("Not enough memory to start the heap");
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size = round_up(size, ALIGNMENT);

    for (int attempt = 0; attempt < 2; attempt++) {
        for (struct block *b = first_block; b != NULL; b = b->next) {
            if (!b->free || b->size < size) {
                continue;
            }
            // Big enough. If there's plenty left over, split it off as a new free block.
            if (b->size >= size + HEADER_SIZE + ALIGNMENT) {
                struct block *rest = (struct block *)((uint8_t *)block_data(b) + size);
                rest->magic = BLOCK_MAGIC;
                rest->size = b->size - size - HEADER_SIZE;
                rest->free = true;
                rest->next = b->next;
                b->next = rest;
                b->size = size;
            }
            b->free = false;
            used_bytes += b->size;
            return block_data(b);
        }
        // Nothing fit: grab more memory and try once more.
        size_t want = size + HEADER_SIZE;
        if (!grow(want > HEAP_MIN_GROW ? want : HEAP_MIN_GROW)) {
            return NULL;
        }
    }
    return NULL;
}

void *kcalloc(size_t count, size_t size) {
    if (size != 0 && count > (size_t)-1 / size) {
        return NULL;  // count * size would overflow
    }
    void *pointer = kmalloc(count * size);
    if (pointer != NULL) {
        memset(pointer, 0, count * size);
    }
    return pointer;
}

void kfree(void *pointer) {
    if (pointer == NULL) {
        return;
    }
    struct block *b = block_from_data(pointer);
    if (b->free) {
        PANIC("Heap: the same memory was freed twice");
    }
    b->free = true;
    used_bytes -= b->size;

    merge_with_next(b);
    // Also merge into the block before it, if that one is free and touching.
    for (struct block *before = first_block; before != NULL; before = before->next) {
        if (before->next == b) {
            if (before->free && touching(before, b)) {
                merge_with_next(before);
            }
            break;
        }
    }
}

void *krealloc(void *pointer, size_t new_size) {
    if (pointer == NULL) {
        return kmalloc(new_size);
    }
    if (new_size == 0) {
        kfree(pointer);
        return NULL;
    }
    struct block *b = block_from_data(pointer);
    if (b->size >= new_size) {
        return pointer;  // already big enough
    }
    void *bigger = kmalloc(new_size);
    if (bigger == NULL) {
        return NULL;
    }
    memcpy(bigger, pointer, b->size);
    kfree(pointer);
    return bigger;
}

uint64_t heap_total_bytes(void) {
    return total_bytes;
}

uint64_t heap_used_bytes(void) {
    return used_bytes;
}
