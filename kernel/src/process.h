// process.h - loading and running .arx programs.
//
// A .arx file comes in one of two flavours, told apart by its first bytes:
//   "\x7F" "ELF"  - ELF inside: the standard format normal compilers make
//   "AREOSARX"    - native AREOS format: a small header, then the program
// Either way, the program runs in ring 3 with its own private memory, so
// if it crashes, only the program stops; AREOS carries on.

#ifndef AREOS_PROCESS_H
#define AREOS_PROCESS_H

#include <stddef.h>
#include <stdint.h>

enum run_status {
    RUN_FINISHED,      // the program exited normally
    RUN_CRASHED,       // the CPU stopped the program (bad address, etc.)
    RUN_BAD_FORMAT,    // not a .arx file AREOS understands
    RUN_NO_MEMORY,     // not enough memory to load it
};

struct run_result {
    enum run_status status;
    uint64_t exit_code;        // RUN_FINISHED: what the program returned
    uint64_t crash_error;      // RUN_CRASHED: CPU error number
    uint64_t crash_where;      // RUN_CRASHED: instruction address
    uint64_t crash_address;    // RUN_CRASHED (page fault): the bad address
};

// Load a program from memory and run it until it finishes or crashes.
struct run_result process_run(const void *file, size_t size);

// Which format a file is in, as text ("ELF inside" / "native"), or NULL.
const char *process_format_name(const void *file, size_t size);

// ---- Used by the system call and interrupt code ----

struct interrupt_frame;

// End the running program with an exit code. Doesn't return.
__attribute__((noreturn)) void process_exit(uint64_t exit_code);

// The CPU stopped the running program with an error. Doesn't return.
__attribute__((noreturn)) void process_crashed(struct interrupt_frame *frame);

// Give the running program more memory. Returns its address, or 0.
uint64_t process_more_memory(uint64_t bytes);

#endif
