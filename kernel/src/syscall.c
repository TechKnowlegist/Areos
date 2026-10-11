// syscall.c - system calls.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "syscall.h"
#include "idt.h"
#include "process.h"
#include "console.h"
#include "input.h"
#include "timer.h"
#include "vmm.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "rtc.h"
#include "settings.h"
#include "files.h"
#include "power.h"

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

static uint64_t do_graphics_draw(uint64_t pixels, uint64_t width, uint64_t height) {
    if (width == 0 || height == 0 || width > GRAPHICS_MAX_SIZE || height > GRAPHICS_MAX_SIZE
        || !vmm_user_range_ok(pixels, width * height * 4)) {
        return SYSCALL_ERROR;
    }
    // Copying a whole screen takes a little while; let the timer keep ticking.
    interrupts_enable();
    graphics_draw((const uint32_t *)pixels, (uint32_t)width, (uint32_t)height);
    interrupts_disable();
    return 0;
}

// Copy a name (ending in '\0') out of a program's memory, safely.
static bool copy_name_from_user(uint64_t address, char *out, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (!vmm_user_range_ok(address + i, 1)) {
            return false;
        }
        out[i] = ((const char *)address)[i];
        if (out[i] == '\0') {
            return true;
        }
    }
    out[size - 1] = '\0';
    return true;
}

static uint64_t do_app_name(uint64_t index, uint64_t buffer, uint64_t size) {
    const struct file *f = files_get(index);
    if (f == NULL || size == 0 || !vmm_user_range_ok(buffer, size)) {
        return SYSCALL_ERROR;
    }
    char *out = (char *)buffer;
    uint64_t i = 0;
    for (; f->name[i] != '\0' && i < size - 1; i++) {
        out[i] = f->name[i];
    }
    out[i] = '\0';
    return i;
}

void syscall_dispatch(struct interrupt_frame *frame) {
    uint64_t number = frame->rax;
    uint64_t arg1 = frame->rdi;
    uint64_t arg2 = frame->rsi;
    uint64_t arg3 = frame->rdx;
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

    case SYS_GRAPHICS_START:
        if (graphics_start()) {
            result = ((uint64_t)graphics_screen_width() << 32) | graphics_screen_height();
        }
        break;

    case SYS_GRAPHICS_DRAW:
        result = do_graphics_draw(arg1, arg2, arg3);
        break;

    case SYS_GRAPHICS_STOP:
        graphics_stop();
        result = 0;
        break;

    case SYS_KEY_EVENT: {
        struct key_event event;
        result = 0;  // "nothing happened"
        if (input_try_get_event(&event)) {
            result = event.key | (event.pressed ? 0x100 : 0)
                   | ((uint64_t)(uint8_t)event.character << 16);
        }
        break;
    }

    case SYS_MOUSE_EVENT: {
        // Packed: bits 0-15 dx, 16-31 dy, 32-34 buttons, bit 40 "there was an event".
        struct mouse_event event;
        result = 0;
        if (mouse_try_get_event(&event)) {
            result = (uint64_t)(uint16_t)event.dx
                   | ((uint64_t)(uint16_t)event.dy << 16)
                   | ((uint64_t)event.buttons << 32)
                   | (1ULL << 40);
        }
        break;
    }

    case SYS_DATE_TIME: {
        struct date_time t = rtc_now();
        result = ((uint64_t)t.year << 40) | ((uint64_t)t.month << 32) | ((uint64_t)t.day << 24)
               | ((uint64_t)t.hour << 16) | ((uint64_t)t.minute << 8) | t.second;
        break;
    }

    case SYS_SETTING_GET: {
        uint64_t value;
        if (settings_get(arg1, &value)) {
            result = value;
        }
        break;
    }

    case SYS_SETTING_SET:
        result = settings_set(arg1, arg2) ? 0 : SYSCALL_ERROR;
        break;

    case SYS_APP_NAME:
        result = do_app_name(arg1, arg2, arg3);
        break;

    case SYS_LAUNCH: {
        char name[LAUNCH_NAME_MAX];
        if (copy_name_from_user(arg1, name, sizeof(name)) && files_find(name) != NULL) {
            process_request_launch(name, arg2 != 0);
            result = 0;
        }
        break;
    }

    case SYS_POWER:
        if (arg1 == 0) {
            power_off();
        } else if (arg1 == 1) {
            power_restart();
        }
        break;

    default:
        break;  // unknown request: answer with an error
    }

    frame->rax = result;
}
