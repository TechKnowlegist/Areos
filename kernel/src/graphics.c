// graphics.c - letting programs draw pictures.

#include <stddef.h>
#include "graphics.h"
#include "console.h"
#include "heap.h"
#include "memory.h"
#include "lang_en.h"

static struct limine_framebuffer *screen;
static bool active = false;
static uint8_t *saved_screen = NULL;   // what was on screen before graphics mode

// Most screens store pixels exactly as 0x00RRGGBB. If this one does, we
// can copy pixels straight across instead of converting each one.
static bool screen_is_rgb(void) {
    return screen->red_mask_size == 8 && screen->red_mask_shift == 16
        && screen->green_mask_size == 8 && screen->green_mask_shift == 8
        && screen->blue_mask_size == 8 && screen->blue_mask_shift == 0;
}

static uint32_t channel(uint8_t value, uint8_t mask_size, uint8_t mask_shift) {
    uint64_t max = ((uint64_t)1 << mask_size) - 1;
    return (uint32_t)((value * max / 255) << mask_shift);
}

static uint32_t convert(uint32_t color) {
    return channel((color >> 16) & 0xFF, screen->red_mask_size, screen->red_mask_shift)
         | channel((color >> 8) & 0xFF, screen->green_mask_size, screen->green_mask_shift)
         | channel(color & 0xFF, screen->blue_mask_size, screen->blue_mask_shift);
}

void graphics_init(struct limine_framebuffer *framebuffer) {
    screen = framebuffer;
}

bool graphics_active(void) {
    return active;
}

uint32_t graphics_screen_width(void) {
    return (uint32_t)screen->width;
}

uint32_t graphics_screen_height(void) {
    return (uint32_t)screen->height;
}

static size_t screen_bytes(void) {
    return screen->pitch * screen->height;
}

bool graphics_start(void) {
    if (active) {
        return true;
    }
    saved_screen = kmalloc(screen_bytes());
    if (saved_screen == NULL) {
        return false;
    }
    memcpy(saved_screen, screen->address, screen_bytes());
    console_pause_screen(true);
    active = true;
    // The screen is off-limits to text now, but this still reaches the
    // terminal, so a screen reader user knows what just happened.
    console_print(STR_GRAPHICS_ON "\n");

    // Start from a clean black screen.
    memset(screen->address, 0, screen_bytes());
    return true;
}

void graphics_stop(void) {
    if (!active) {
        return;
    }
    console_print(STR_GRAPHICS_OFF "\n");  // terminal only, like above
    memcpy(screen->address, saved_screen, screen_bytes());
    kfree(saved_screen);
    saved_screen = NULL;
    active = false;
    console_pause_screen(false);
}

void graphics_draw(const uint32_t *pixels, uint32_t width, uint32_t height) {
    if (!active || width == 0 || height == 0) {
        return;
    }

    // Biggest whole-number zoom that fits, so pixels stay crisp and square.
    uint32_t scale_x = (uint32_t)screen->width / width;
    uint32_t scale_y = (uint32_t)screen->height / height;
    uint32_t scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale == 0) {
        scale = 1;  // bigger than the screen: show the top-left part
    }
    uint32_t shown_width = width * scale;
    uint32_t shown_height = height * scale;
    if (shown_width > screen->width) shown_width = (uint32_t)screen->width;
    if (shown_height > screen->height) shown_height = (uint32_t)screen->height;

    // Center it on the screen.
    uint32_t left = ((uint32_t)screen->width - shown_width) / 2;
    uint32_t top = ((uint32_t)screen->height - shown_height) / 2;

    bool direct = screen_is_rgb();
    uint8_t *base = screen->address;

    // Fast path: same size, same pixel format. Copy whole rows at once.
    if (direct && scale == 1) {
        for (uint32_t y = 0; y < shown_height; y++) {
            memcpy(base + (size_t)(top + y) * screen->pitch + (size_t)left * 4,
                   pixels + (size_t)y * width, (size_t)shown_width * 4);
        }
        return;
    }
    for (uint32_t y = 0; y < shown_height; y++) {
        const uint32_t *source_row = pixels + (size_t)(y / scale) * width;
        volatile uint32_t *row = (volatile uint32_t *)(base + (size_t)(top + y) * screen->pitch) + left;
        for (uint32_t x = 0; x < shown_width; x++) {
            uint32_t color = source_row[x / scale] & 0x00FFFFFF;
            row[x] = direct ? color : convert(color);
        }
    }
}
