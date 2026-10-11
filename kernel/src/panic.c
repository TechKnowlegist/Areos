// panic.c - the AREOS crash screen.

#include "panic.h"
#include "console.h"
#include "cpu.h"
#include "idt.h"
#include "lang_en.h"

#define PAGE_FAULT_VECTOR 14

static const char *cpu_error_names[32] = STR_CPU_ERROR_NAMES;

// The friendly part, the same for every crash.
static void print_header(void) {
    interrupts_disable();  // nothing should interrupt the crash screen
    console_pause_screen(false);  // even if a program was showing graphics
    console_clear();
    console_set_color(COLOR_GREEN);
    console_print(STR_PANIC_TITLE "\n");
    console_set_color(COLOR_BLUE);
    console_print(STR_PANIC_CALM "\n\n");
    console_set_color(COLOR_DIM);
    console_print(STR_PANIC_DETAILS "\n");
    console_set_color(COLOR_WARNING);
}

static void print_footer(void) {
    console_set_color(COLOR_DIM);
    console_print("\n" STR_PANIC_HINT "\n");
    console_hide_cursor();
}

void kernel_panic(const char *reason, const char *file, int line) {
    print_header();
    console_print("  " STR_PANIC_ERROR);
    console_print(reason);
    console_print("\n  " STR_PANIC_WHERE);
    console_print(file);
    console_print(":");
    console_print_number((uint64_t)line);
    console_print("\n");
    print_footer();
    cpu_halt_forever();
}

// Print "name=0x0000000000001234" for one register.
static void print_register(const char *name, uint64_t value) {
    console_print(name);
    console_print("=");
    console_print_hex(value);
    console_print("  ");
}

void kernel_panic_cpu(struct interrupt_frame *frame) {
    print_header();

    console_print("  " STR_PANIC_ERROR);
    console_print(cpu_error_names[frame->vector]);
    console_print(" (CPU error ");
    console_print_number(frame->vector);
    console_print(")\n  " STR_PANIC_WHERE);
    console_print_hex(frame->rip);
    console_print("\n");

    if (frame->vector == PAGE_FAULT_VECTOR) {
        // For a page fault, the CPU stores the bad address in register CR2.
        uint64_t bad_address;
        asm volatile ("mov %%cr2, %0" : "=r"(bad_address));
        console_print("  " STR_PANIC_BAD_ADDRESS);
        console_print_hex(bad_address);
        console_print("\n");
    }

    console_set_color(COLOR_DIM);
    console_print("\n  " STR_PANIC_REGISTERS "\n  ");
    print_register("rax", frame->rax); print_register("rbx", frame->rbx);
    print_register("rcx", frame->rcx); console_print("\n  ");
    print_register("rdx", frame->rdx); print_register("rsi", frame->rsi);
    print_register("rdi", frame->rdi); console_print("\n  ");
    print_register("rbp", frame->rbp); print_register("rsp", frame->rsp);
    print_register("r8 ", frame->r8);  console_print("\n  ");
    print_register("r9 ", frame->r9);  print_register("r10", frame->r10);
    print_register("r11", frame->r11); console_print("\n  ");
    print_register("r12", frame->r12); print_register("r13", frame->r13);
    print_register("r14", frame->r14); console_print("\n  ");
    print_register("r15", frame->r15); print_register("err", frame->error_code);
    print_register("flg", frame->rflags);
    console_print("\n");

    print_footer();
    cpu_halt_forever();
}
