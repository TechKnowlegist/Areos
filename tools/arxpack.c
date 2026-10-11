// arxpack.c - turn a normal (ELF) program into a native AREOS .arx file.
//
// Usage:  arxpack input.elf output.arx
//
// It reads the ELF's "load" segments, lays them out in one flat block
// (filling any gaps with zeros), and writes the native AREOS header
// followed by that block. The format is described in docs/arx-format.md.
//
// This runs on your PC (Linux/WSL), not inside AREOS.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct elf_header {
    uint8_t  magic[4];
    uint8_t  class_;
    uint8_t  endian;
    uint8_t  version;
    uint8_t  os_abi;
    uint8_t  padding[8];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint64_t entry;
    uint64_t program_headers;
    uint64_t section_headers;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_names_index;
} __attribute__((packed));

struct elf_segment {
    uint32_t type;
    uint32_t flags;
    uint64_t file_offset;
    uint64_t address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

// Must match struct arx_header in kernel/src/process.c.
struct arx_header {
    char     magic[8];
    uint32_t version;
    uint32_t header_size;
    uint64_t load_address;
    uint64_t entry;
    uint64_t image_size;
    uint64_t memory_size;
    uint8_t  reserved[16];
} __attribute__((packed));

#define ELF_SEGMENT_LOAD 1

static void fail(const char *message) {
    fprintf(stderr, "arxpack: %s\n", message);
    exit(1);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: arxpack input.elf output.arx\n");
        return 1;
    }

    // Read the whole input file.
    FILE *in = fopen(argv[1], "rb");
    if (in == NULL) {
        fail("can't open the input file");
    }
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    uint8_t *file = malloc((size_t)size);
    if (file == NULL || fread(file, 1, (size_t)size, in) != (size_t)size) {
        fail("can't read the input file");
    }
    fclose(in);

    const struct elf_header *elf = (const struct elf_header *)file;
    if (size < (long)sizeof(*elf) || memcmp(elf->magic, "\x7F" "ELF", 4) != 0
        || elf->class_ != 2 || elf->machine != 62) {
        fail("input isn't a 64-bit x86 ELF program");
    }
    const struct elf_segment *segments = (const struct elf_segment *)(file + elf->program_headers);

    // Find the lowest and highest addresses the program uses.
    uint64_t low = UINT64_MAX, file_high = 0, memory_high = 0;
    for (int i = 0; i < elf->program_header_count; i++) {
        const struct elf_segment *s = &segments[i];
        if (s->type != ELF_SEGMENT_LOAD || s->memory_size == 0) {
            continue;
        }
        if (s->address < low) low = s->address;
        if (s->address + s->file_size > file_high) file_high = s->address + s->file_size;
        if (s->address + s->memory_size > memory_high) memory_high = s->address + s->memory_size;
    }
    if (low == UINT64_MAX) {
        fail("the program has nothing to load");
    }

    // Lay the segments out in one flat block.
    uint64_t image_size = file_high - low;
    uint8_t *image = calloc(1, image_size ? image_size : 1);
    for (int i = 0; i < elf->program_header_count; i++) {
        const struct elf_segment *s = &segments[i];
        if (s->type != ELF_SEGMENT_LOAD || s->file_size == 0) {
            continue;
        }
        memcpy(image + (s->address - low), file + s->file_offset, s->file_size);
    }

    struct arx_header header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "AREOSARX", 8);
    header.version = 1;
    header.header_size = sizeof(header);
    header.load_address = low;
    header.entry = elf->entry;
    header.image_size = image_size;
    header.memory_size = memory_high - low;

    FILE *out = fopen(argv[2], "wb");
    if (out == NULL) {
        fail("can't create the output file");
    }
    fwrite(&header, sizeof(header), 1, out);
    fwrite(image, 1, image_size, out);
    fclose(out);

    printf("arxpack: %s -> %s (native AREOS, %llu bytes)\n", argv[1], argv[2],
           (unsigned long long)(sizeof(header) + image_size));
    return 0;
}
