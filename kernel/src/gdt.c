// gdt.c - the GDT and TSS.

#include "gdt.h"
#include "memory.h"

// The CPU's TSS layout. In 64-bit mode we only really use rsp0: the
// stack the CPU switches to when an interrupt arrives while a program
// (ring 3) is running.
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t size;
    uint64_t address;
} __attribute__((packed));

#define INTERRUPT_STACK_SIZE (16 * 1024)

static struct tss tss;
static uint8_t interrupt_stack[INTERRUPT_STACK_SIZE] __attribute__((aligned(16)));

// Each entry packs base, limit and flags in the CPU's odd format. In
// 64-bit mode base and limit are ignored for code/data, so these are
// the standard fixed values.
static uint64_t gdt[7] = {
    0x0000000000000000,  // 0x00: required empty entry
    0x00AF9A000000FFFF,  // 0x08: kernel code (ring 0, 64-bit)
    0x00CF92000000FFFF,  // 0x10: kernel data (ring 0)
    0x00CFF2000000FFFF,  // 0x18: user data   (ring 3)
    0x00AFFA000000FFFF,  // 0x20: user code   (ring 3, 64-bit)
    0,                   // 0x28: TSS (filled in below; it takes two slots)
    0,
};

void gdt_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.rsp0 = (uint64_t)(interrupt_stack + INTERRUPT_STACK_SIZE);
    tss.io_map_base = sizeof(tss);  // "no I/O permission map"

    uint64_t base = (uint64_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    gdt[5] = (limit & 0xFFFF)
           | ((base & 0xFFFFFF) << 16)
           | (0x89ULL << 40)                  // present, 64-bit TSS (available)
           | (((limit >> 16) & 0xF) << 48)
           | (((base >> 24) & 0xFF) << 56);
    gdt[6] = base >> 32;

    struct gdt_pointer pointer = {
        .size = sizeof(gdt) - 1,
        .address = (uint64_t)gdt,
    };

    asm volatile (
        "lgdt %0\n"
        // Reload the code segment: push the new selector and a return
        // address, then do a "far return" into it.
        "pushq %1\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        // Reload the data segments.
        "movw %2, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        // Load the TSS.
        "movw %3, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(pointer), "i"(SELECTOR_KERNEL_CODE), "i"(SELECTOR_KERNEL_DATA), "i"(SELECTOR_TSS)
        : "rax", "memory");
}
