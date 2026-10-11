// malloc.c - the speech engine's own memory allocator.
//
// Flite makes thousands of small allocations for every sentence and frees
// them all afterwards, so this is built for that: every block is rounded
// up to a power of two, and freed blocks go on a list for their size, ready
// to be handed out again straight away. New memory comes off the end of one
// big region the kernel gives us at start-up.
//
// It's only used by the speech engine, which runs on its own, so it needs
// no locking.

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "libc.h"

#define MIN_SHIFT 4      // smallest block: 16 bytes
#define MAX_SHIFT 30     // biggest block: 1 GB (far more than we have)
#define HEADER 16        // keeps every block 16-byte aligned

struct block_header {
    uint32_t size_class;     // block holds 1 << size_class bytes, header included
    uint32_t magic;
    uint64_t unused;
};

#define MAGIC_USED 0x5EEC4ED1u
#define MAGIC_FREE 0xF4EEB10Cu

struct free_block {
    struct block_header header;
    struct free_block *next;
};

static unsigned char *region_next, *region_end, *region_start;
static struct free_block *free_lists[MAX_SHIFT + 1];

void speech_libc_set_memory(void *start, size_t size) {
    uintptr_t aligned = ((uintptr_t)start + 15) & ~(uintptr_t)15;
    region_start = region_next = (unsigned char *)aligned;
    region_end = (unsigned char *)start + size;
    for (int i = 0; i <= MAX_SHIFT; i++) free_lists[i] = NULL;
}

size_t speech_libc_memory_used(void) {
    return (size_t)(region_next - region_start);
}

static int class_for(size_t size) {
    size_t total = size + HEADER;
    int shift = MIN_SHIFT;
    while (shift <= MAX_SHIFT && ((size_t)1 << shift) < total) shift++;
    return shift;
}

void *malloc(size_t size) {
    int c = class_for(size);
    if (c > MAX_SHIFT) return NULL;
    struct free_block *block = free_lists[c];
    if (block != NULL) {
        free_lists[c] = block->next;
    } else {
        size_t bytes = (size_t)1 << c;
        if ((size_t)(region_end - region_next) < bytes) return NULL;
        block = (struct free_block *)region_next;
        region_next += bytes;
    }
    block->header.size_class = (uint32_t)c;
    block->header.magic = MAGIC_USED;
    return (unsigned char *)block + HEADER;
}

void free(void *p) {
    if (p == NULL) return;
    struct free_block *block = (struct free_block *)((unsigned char *)p - HEADER);
    if (block->header.magic != MAGIC_USED) return;   // not ours, or freed twice: ignore
    block->header.magic = MAGIC_FREE;
    int c = (int)block->header.size_class;
    block->next = free_lists[c];
    free_lists[c] = block;
}

void *calloc(size_t count, size_t size) {
    if (size != 0 && count > (size_t)-1 / size) return NULL;
    void *p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

void *realloc(void *p, size_t size) {
    if (p == NULL) return malloc(size);
    if (size == 0) {
        free(p);
        return NULL;
    }
    struct block_header *header = (struct block_header *)((unsigned char *)p - HEADER);
    size_t room = ((size_t)1 << header->size_class) - HEADER;
    if (size <= room) return p;
    void *bigger = malloc(size);
    if (bigger == NULL) return NULL;
    memcpy(bigger, p, room);
    free(p);
    return bigger;
}
