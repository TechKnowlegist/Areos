// console.c - AREOS text console.

#include <stddef.h>
#include "console.h"
#include "font.h"

static struct limine_framebuffer *screen;
static uint32_t text_color = COLOR_GREEN;

// Where the next character goes, counted in characters (not pixels).
static uint64_t cursor_col = 0;
static uint64_t cursor_row = 0;
static uint64_t total_cols = 0;
static uint64_t total_rows = 0;

// The size of one character on screen, in pixels.
#define CELL_WIDTH   (FONT_WIDTH * CONSOLE_SCALE)
#define CELL_HEIGHT  (FONT_HEIGHT * CONSOLE_SCALE)

// Turn an 8-bit color channel into whatever layout this screen uses.
static uint32_t channel(uint8_t value, uint8_t mask_size, uint8_t mask_shift) {
    uint64_t max = ((uint64_t)1 << mask_size) - 1;
    return (uint32_t)((value * max / 255) << mask_shift);
}

// Turn a 0xRRGGBB color into a pixel value for this screen.
static uint32_t to_pixel(uint32_t color) {
    uint8_t red   = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue  = color & 0xFF;
    return channel(red, screen->red_mask_size, screen->red_mask_shift)
         | channel(green, screen->green_mask_size, screen->green_mask_shift)
         | channel(blue, screen->blue_mask_size, screen->blue_mask_shift);
}

static void put_pixel(uint64_t x, uint64_t y, uint32_t pixel) {
    if (x >= screen->width || y >= screen->height) {
        return;
    }
    volatile uint32_t *pixels = screen->address;
    pixels[y * (screen->pitch / 4) + x] = pixel;
}

// Fill a rectangle with one color.
static void fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    uint32_t pixel = to_pixel(color);
    for (uint64_t row = 0; row < h; row++) {
        for (uint64_t col = 0; col < w; col++) {
            put_pixel(x + col, y + row, pixel);
        }
    }
}

// Draw one character at a character position on the screen.
static void draw_char(char c, uint64_t col, uint64_t row) {
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) {
        c = '?';  // unknown characters show up as a question mark
    }
    const uint8_t *glyph = font_glyphs[c - FONT_FIRST_CHAR];
    uint32_t fg = to_pixel(text_color);
    uint32_t bg = to_pixel(COLOR_BACKGROUND);
    uint64_t left = CONSOLE_MARGIN + col * CELL_WIDTH;
    uint64_t top  = CONSOLE_MARGIN + row * CELL_HEIGHT;

    for (uint64_t gy = 0; gy < FONT_HEIGHT; gy++) {
        for (uint64_t gx = 0; gx < FONT_WIDTH; gx++) {
            uint32_t pixel = (glyph[gy] & (0x80 >> gx)) ? fg : bg;
            // Each font pixel becomes a CONSOLE_SCALE x CONSOLE_SCALE block.
            for (uint64_t sy = 0; sy < CONSOLE_SCALE; sy++) {
                for (uint64_t sx = 0; sx < CONSOLE_SCALE; sx++) {
                    put_pixel(left + gx * CONSOLE_SCALE + sx,
                              top + gy * CONSOLE_SCALE + sy, pixel);
                }
            }
        }
    }
}

static void new_line(void) {
    cursor_col = 0;
    cursor_row++;
    if (cursor_row >= total_rows) {
        // No scrolling yet: when the screen fills up, start again at the top.
        console_clear();
    }
}

void console_init(struct limine_framebuffer *fb) {
    screen = fb;
    total_cols = (screen->width - 2 * CONSOLE_MARGIN) / CELL_WIDTH;
    total_rows = (screen->height - 2 * CONSOLE_MARGIN) / CELL_HEIGHT;
    console_clear();
}

void console_clear(void) {
    fill_rect(0, 0, screen->width, screen->height, COLOR_BACKGROUND);
    cursor_col = 0;
    cursor_row = 0;
}

void console_set_color(uint32_t color) {
    text_color = color;
}

// The blinking-line cursor isn't blinking yet (no timer until step 5),
// so it's just an underscore showing where the next letter will go.
static void draw_cursor(void) {
    draw_char('_', cursor_col, cursor_row);
}

static void erase_cursor(void) {
    draw_char(' ', cursor_col, cursor_row);
}

void console_putc(char c) {
    erase_cursor();

    if (c == '\n') {
        new_line();
    } else if (c == '\b') {
        // Backspace: step back one letter and blank it out.
        if (cursor_col > 0) {
            cursor_col--;
            draw_char(' ', cursor_col, cursor_row);
        }
    } else if (c == '\t') {
        // Tab: jump forward to the next multiple of 4 columns.
        do {
            console_putc(' ');
        } while (cursor_col % 4 != 0);
        return;  // the spaces already redrew the cursor
    } else {
        draw_char(c, cursor_col, cursor_row);
        cursor_col++;
        if (cursor_col >= total_cols) {
            new_line();
        }
    }

    draw_cursor();
}

void console_print(const char *text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        console_putc(text[i]);
    }
}
