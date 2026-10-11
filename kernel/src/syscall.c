// syscall.c - system calls.

#include <stdint.h>
#include "syscall.h"
#include "idt.h"
#include "process.h"
#include "console.h"
#include "input.h"
#include "timer.h"
#include "vmm.h"

#define WRITE_MAX_LENGTH  (1024 * 1024)  // biggest single write
#define SLEEP_MAX_MS      (60 * 60 * 1000)
#define SYSCALL_ERROR     ((uint64_t)-1)

static uint64_t do_write(uint64_t text, uint64_t length) {
    // Never trust a pointer from a program: check it's really its memory.
    if (length > WRITE_MAX_LENGTH || !vmm_user_range_ok(text, length)) {
        return SYSCALL_ERROR;
    }
    const char *characters = (const char *)text;
    for (uint64_t i = 0; i < length; i++) {
        console_putc(characters[i]);
    }
    return length;
}

void syscall_dispatch(struct interrupt_frame *frame) {
    uint64_t number = frame->rax;
    uint64_t arg1 = frame->rdi;
    uint64_t arg2 = frame->rsi;
    uint64_t result = SYSCALL_ERROR;

    switch (number) {
    case SYS_EXIT:
        process_exit(arg1);

    case SYS_WRITE:
        result = do_write(arg1, arg2);
        break;

    case SYS_READ_KEY:
        // Waiting needs the timer and keyboard, so let interrupts in.
        interrupts_enable();
        result = (uint8_t)input_wait_key(true);
        interrupts_disable();
        break;

    case SYS_SLEEP_MS:
        interrupts_enable();
        timer_sleep_ms(arg1 > SLEEP_MAX_MS ? SLEEP_MAX_MS : arg1);
        interrupts_disable();
        result = 0;
        break;

    case SYS_TIME_MS:
        result = timer_ms();
        break;

    case SYS_MORE_MEMORY:
        result = process_more_memory(arg1);
        break;

    case SYS_SET_COLOR:
        console_set_color((uint32_t)(arg1 & 0xFFFFFF));
        result = 0;
        break;

    default:
        break;  // unknown request: answer with an error
    }

    frame->rax = result;
}
