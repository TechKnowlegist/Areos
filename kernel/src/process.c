// process.c - loading and running .arx programs.

#include <stdbool.h>
#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "idt.h"
#include "memory.h"

// Where things go in a program's memory (its own private half).
#define USER_STACK_TOP     0x0000700000000000ULL
#define USER_STACK_PAGES   64                     // 256 KB of stack
#define USER_HEAP_START    0x0000600000000000ULL  // where "more memory" begins
#define USER_HEAP_MAX      (1024ULL * 1024 * 1024) // 1 GB cap per program
#define PROGRAM_MAX_SIZE   (512ULL * 1024 * 1024)  // biggest program image

// Assembly helpers in usermode.S.
uint64_t usermode_enter(uint64_t entry, uint64_t user_stack, uint64_t *saved_rsp);
__attribute__((noreturn)) void usermode_return(uint64_t saved_rsp, uint64_t exit_code);

// The program that's running right now (AREOS runs one at a time, for now).
static struct {
    bool running;
    uint64_t space;
    uint64_t kernel_rsp;     // where usermode_enter left the kernel's stack
    uint64_t heap_end;       // next address "more memory" will hand out
    struct run_result result;
} current;

// ---- Format 1: ELF inside ----

#define ELF_CLASS_64      2
#define ELF_LITTLE_ENDIAN 1
#define ELF_TYPE_EXEC     2
#define ELF_MACHINE_X64   62
#define ELF_SEGMENT_LOAD  1

struct elf_header {
    uint8_t  magic[4];        // 0x7F 'E' 'L' 'F'
    uint8_t  class_;          // 2 = 64-bit
    uint8_t  endian;          // 1 = little-endian
    uint8_t  version;
    uint8_t  os_abi;
    uint8_t  padding[8];
    uint16_t type;            // 2 = executable
    uint16_t machine;         // 62 = x86-64
    uint32_t version2;
    uint64_t entry;           // where the program starts
    uint64_t program_headers; // offset of the segment list
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
    uint32_t type;            // 1 = "load this into memory"
    uint32_t flags;
    uint64_t file_offset;     // where its bytes are in the file
    uint64_t address;         // where they go in memory
    uint64_t physical_address;
    uint64_t file_size;       // bytes to copy from the file
    uint64_t memory_size;     // bytes in memory (extra is zeroed)
    uint64_t alignment;
} __attribute__((packed));

static bool is_elf(const uint8_t *file, size_t size) {
    return size >= sizeof(struct elf_header)
        && file[0] == 0x7F && file[1] == 'E' && file[2] == 'L' && file[3] == 'F';
}

static bool range_is_user(uint64_t address, uint64_t length) {
    return address != 0 && address + length >= address && address + length <= USER_HEAP_START;
}

static size_t pages_spanning(uint64_t address, uint64_t length) {
    uint64_t first = address / PAGE_SIZE;
    uint64_t last = (address + length + PAGE_SIZE - 1) / PAGE_SIZE;
    return (size_t)(last - first);
}

// Returns the entry point, or 0 on failure.
static uint64_t load_elf(uint64_t space, const uint8_t *file, size_t size, enum run_status *error) {
    const struct elf_header *header = (const struct elf_header *)file;
    *error = RUN_BAD_FORMAT;
    if (header->class_ != ELF_CLASS_64 || header->endian != ELF_LITTLE_ENDIAN
        || header->type != ELF_TYPE_EXEC || header->machine != ELF_MACHINE_X64
        || header->program_header_size != sizeof(struct elf_segment)) {
        return 0;
    }
    uint64_t table_end = header->program_headers
                       + (uint64_t)header->program_header_count * sizeof(struct elf_segment);
    if (table_end > size) {
        return 0;
    }

    const struct elf_segment *segments = (const struct elf_segment *)(file + header->program_headers);
    for (uint16_t i = 0; i < header->program_header_count; i++) {
        const struct elf_segment *segment = &segments[i];
        if (segment->type != ELF_SEGMENT_LOAD || segment->memory_size == 0) {
            continue;
        }
        if (segment->file_size > segment->memory_size
            || segment->file_offset + segment->file_size > size
            || segment->memory_size > PROGRAM_MAX_SIZE
            || !range_is_user(segment->address, segment->memory_size)) {
            return 0;
        }
        if (!vmm_map_user_pages(space, segment->address,
                                pages_spanning(segment->address, segment->memory_size))) {
            *error = RUN_NO_MEMORY;
            return 0;
        }
        if (!vmm_copy_to_user(space, segment->address, file + segment->file_offset, segment->file_size)) {
            return 0;
        }
    }
    return header->entry;
}

// ---- Format 2: native AREOS ----
// The layout is documented in docs/arx-format.md.

#define ARX_VERSION 1

struct arx_header {
    char     magic[8];        // "AREOSARX"
    uint32_t version;         // 1
    uint32_t header_size;     // 64: the program image starts right after
    uint64_t load_address;    // where the image goes in memory
    uint64_t entry;           // where the program starts
    uint64_t image_size;      // bytes of program image in the file
    uint64_t memory_size;     // bytes in memory (extra is zeroed)
    uint8_t  reserved[16];
} __attribute__((packed));

static bool is_native(const uint8_t *file, size_t size) {
    const char *magic = "AREOSARX";
    if (size < sizeof(struct arx_header)) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        if (file[i] != (uint8_t)magic[i]) {
            return false;
        }
    }
    return true;
}

static uint64_t load_native(uint64_t space, const uint8_t *file, size_t size, enum run_status *error) {
    const struct arx_header *header = (const struct arx_header *)file;
    *error = RUN_BAD_FORMAT;
    if (header->version != ARX_VERSION || header->header_size < sizeof(struct arx_header)
        || header->header_size + header->image_size > size
        || header->image_size > header->memory_size
        || header->memory_size > PROGRAM_MAX_SIZE
        || !range_is_user(header->load_address, header->memory_size)
        || header->entry < header->load_address
        || header->entry >= header->load_address + header->memory_size) {
        return 0;
    }
    if (!vmm_map_user_pages(space, header->load_address,
                            pages_spanning(header->load_address, header->memory_size))) {
        *error = RUN_NO_MEMORY;
        return 0;
    }
    if (!vmm_copy_to_user(space, header->load_address, file + header->header_size, header->image_size)) {
        return 0;
    }
    return header->entry;
}

// ---- Running ----

const char *process_format_name(const void *file, size_t size) {
    if (is_elf(file, size)) {
        return "ELF inside";
    }
    if (is_native(file, size)) {
        return "native AREOS";
    }
    return NULL;
}

struct run_result process_run(const void *file, size_t size) {
    struct run_result result = { .status = RUN_BAD_FORMAT };
    if (!is_elf(file, size) && !is_native(file, size)) {
        return result;
    }

    uint64_t space = vmm_create_address_space();
    if (space == 0) {
        result.status = RUN_NO_MEMORY;
        return result;
    }

    enum run_status error;
    uint64_t entry = is_elf(file, size) ? load_elf(space, file, size, &error)
                                        : load_native(space, file, size, &error);
    uint64_t stack_bottom = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    if (entry != 0 && !vmm_map_user_pages(space, stack_bottom, USER_STACK_PAGES)) {
        entry = 0;
        error = RUN_NO_MEMORY;
    }
    if (entry == 0) {
        vmm_destroy_address_space(space);
        result.status = error;
        return result;
    }

    current.running = true;
    current.space = space;
    current.heap_end = USER_HEAP_START;
    current.result = (struct run_result){ .status = RUN_FINISHED };

    vmm_switch_to(space);
    // Start with the stack the way a C function expects right after "call".
    usermode_enter(entry, USER_STACK_TOP - 8, &current.kernel_rsp);
    // We arrive back here when the program exits or crashes (via process_exit),
    // with interrupts off because that happened inside an interrupt.
    interrupts_enable();
    return current.result;
}

void process_exit(uint64_t exit_code) {
    current.result.exit_code = exit_code;
    current.running = false;
    vmm_switch_to_kernel();
    vmm_destroy_address_space(current.space);
    usermode_return(current.kernel_rsp, exit_code);
}

void process_crashed(struct interrupt_frame *frame) {
    current.result.status = RUN_CRASHED;
    current.result.crash_error = frame->vector;
    current.result.crash_where = frame->rip;
    current.result.crash_address = 0;
    if (frame->vector == 14) {  // page fault: the bad address is in CR2
        asm volatile ("mov %%cr2, %0" : "=r"(current.result.crash_address));
    }
    process_exit((uint64_t)-1);
}

uint64_t process_more_memory(uint64_t bytes) {
    uint64_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0 || current.heap_end + pages * PAGE_SIZE > USER_HEAP_START + USER_HEAP_MAX) {
        return 0;
    }
    uint64_t address = current.heap_end;
    if (!vmm_map_user_pages(current.space, address, pages)) {
        return 0;
    }
    current.heap_end += pages * PAGE_SIZE;
    return address;
}
