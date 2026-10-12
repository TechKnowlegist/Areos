// malloc.c - the memory allocator for the JavaScript engine.
//
// Small blocks come in steps of 16 bytes; bigger ones in four steps per
// doubling (so at most a quarter is wasted). Freed blocks wait on a list for
// their size and are handed out again first. When there is no room, a big
// chunk (at least 1 MB) is requested from AREOS, and blocks are cut off its
// end. Memory is never given back to AREOS, but the lists keep it from
// growing when a page's JavaScript runs, is thrown away, and another runs.

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>
#include "areos.h"

#define HEADER 16
#define LISTS 160
#define CHUNK (1024u * 1024u)

struct header {
    uint32_t list;
    uint32_t magic;
    uint64_t block_size;     // header included
};
struct free_block { struct header h; struct free_block *next; };

#define USED 0x7A5ED00Du
#define FREE 0x7A5EF4EEu

static struct free_block *lists[LISTS];
static unsigned char *chunk_next, *chunk_end;
static size_t total_obtained;

// Work out which list a request belongs to, and how big its block is.
static int classify(size_t total, size_t *block) {
    if (total < 32) total = 32;
    if (total <= 256) {
        *block = (total + 15) & ~(size_t)15;
        return (int)(*block / 16);                 // 2 .. 16
    }
    int e = 8;
    while (((size_t)1 << (e + 1)) < total) e++;    // 2^e < total <= 2^(e+1)
    size_t step = (size_t)1 << (e - 2);
    size_t steps = (total + step - 1) / step;      // 5 .. 8
    *block = steps * step;
    int idx = 32 + (e - 8) * 4 + (int)(steps - 5);
    return idx < LISTS ? idx : -1;
}

size_t js_memory_obtained(void) { return total_obtained; }

void *malloc(size_t size) {
    if (size > ((size_t)1 << 38)) return NULL;
    size_t block;
    int list = classify(size + HEADER, &block);
    if (list < 0) return NULL;
    struct free_block *b = lists[list];
    if (b) {
        lists[list] = b->next;
    } else {
        if ((size_t)(chunk_end - chunk_next) < block) {
            size_t want = block > CHUNK ? block : CHUNK;
            want = (want + 4095) & ~(size_t)4095;
            unsigned char *fresh = areos_more_memory(want);
            if (!fresh) return NULL;
            chunk_next = fresh;
            chunk_end = fresh + want;
            total_obtained += want;
        }
        b = (struct free_block *)chunk_next;
        chunk_next += block;
    }
    b->h.list = (uint32_t)list;
    b->h.magic = USED;
    b->h.block_size = block;
    return (unsigned char *)b + HEADER;
}

void free(void *p) {
    if (!p) return;
    struct free_block *b = (struct free_block *)((unsigned char *)p - HEADER);
    if (b->h.magic != USED) return;
    b->h.magic = FREE;
    b->next = lists[b->h.list];
    lists[b->h.list] = b;
}

size_t malloc_usable_size(void *p) {
    if (!p) return 0;
    struct header *h = (struct header *)((unsigned char *)p - HEADER);
    return h->block_size - HEADER;
}

void *calloc(size_t count, size_t size) {
    if (size && count > ((size_t)1 << 38) / size) return NULL;
    void *p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

void *realloc(void *p, size_t size) {
    if (!p) return malloc(size);
    if (size == 0) { free(p); return NULL; }
    size_t have = malloc_usable_size(p);
    if (size <= have && size * 2 >= have) return p;
    void *q = malloc(size);
    if (!q) return NULL;
    memcpy(q, p, have < size ? have : size);
    free(p);
    return q;
}
