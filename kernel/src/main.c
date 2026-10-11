#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "console.h"
#include "lang_en.h"
#include "shell.h"
#include "pmm.h"
#include "heap.h"
#include "panic.h"

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// Ask Limine for the memory map (which parts of RAM we can use)...
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

// ...and for the "higher half direct map": all of physical memory,
// mapped at one fixed offset so the kernel can reach any of it.
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// Halt and catch fire function.
static void hcf(void) {
    for (;;) {
#if defined (__x86_64__)
        asm ("hlt");
#elif defined (__aarch64__) || defined (__riscv)
        asm ("wfi");
#elif defined (__loongarch64)
        asm ("idle 0");
#endif
    }
}

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Use the first screen.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // Ensure the framebuffer has 32-bit RGB pixels, the only kind we handle.
    if (framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB || framebuffer->bpp != 32) {
        hcf();
    }

    console_init(framebuffer);

    console_set_color(COLOR_GREEN);
    console_print(STR_BOOT_TITLE "\n");
    console_set_color(COLOR_BLUE);
    console_print(STR_BOOT_SLOGAN "\n\n");

    console_set_color(COLOR_WARNING);
    console_print(STR_DEV_WARNING_1 "\n");
    console_print(STR_DEV_WARNING_2 "\n");
    console_set_color(COLOR_DIM);
    console_print(STR_REAL_MACHINE "\n\n");

    // Set up memory. The console already works, so if this fails the
    // crash screen can show why.
    if (memmap_request.response == NULL || hhdm_request.response == NULL) {
        PANIC("The bootloader didn't tell us about memory");
    }
    pmm_init(memmap_request.response, hhdm_request.response->offset);
    heap_init();

    console_set_color(COLOR_DIM);
    console_print(STR_BOOT_MEMORY_1);
    console_print_number(pmm_total_bytes() / (1024 * 1024));
    console_print(STR_BOOT_MEMORY_2 "\n\n");

    console_set_color(COLOR_GREEN);
    console_print(STR_SHELL_BANNER "\n");
    console_set_color(COLOR_DIM);
    console_print(STR_SHELL_HINT "\n\n");

    // Hand control to the command line. It never comes back.
    shell_run();
}
