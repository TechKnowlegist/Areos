// console.c - AREOS text console.

#include <stdbool.h>
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

// Move every line of text up by one, and blank the bottom line.
static void scroll_up(void) {
    volatile uint32_t *pixels = screen->address;
    uint64_t pixels_per_row = screen->pitch / 4;
    uint64_t text_top = CONSOLE_MARGIN;
    uint64_t text_bottom = CONSOLE_MARGIN + total_rows * CELL_HEIGHT;
    uint64_t text_left = CONSOLE_MARGIN;
    uint64_t text_width = total_cols * CELL_WIDTH;

    for (uint64_t y = text_top; y + CELL_HEIGHT < text_bottom; y++) {
        volatile uint32_t *to = pixels + y * pixels_per_row + text_left;
        volatile uint32_t *from = pixels + (y + CELL_HEIGHT) * pixels_per_row + text_left;
        for (uint64_t x = 0; x < text_width; x++) {
            to[x] = from[x];
        }
    }
    fill_rect(text_left, text_bottom - CELL_HEIGHT, text_width, CELL_HEIGHT, COLOR_BACKGROUND);
}

static void new_line(void) {
    cursor_col = 0;
    cursor_row++;
    if (cursor_row >= total_rows) {
        // Screen is full: push everything up one line, stay on the last line.
        scroll_up();
        cursor_row = total_rows - 1;
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
static bool cursor_visible = true;

static void draw_cursor(void) {
    if (cursor_visible) {
        draw_char('_', cursor_col, cursor_row);
    }
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
        char c = text[i];

        // Word wrap: at the start of a word, if the whole word won't fit
        // on this line (but would fit on a fresh one), move down first.
        bool word_start = (c != ' ' && c != '\n') && (i == 0 || text[i - 1] == ' ');
        if (word_start && cursor_col > 0) {
            uint64_t word_length = 0;
            while (text[i + word_length] != '\0' && text[i + word_length] != ' '
                   && text[i + word_length] != '\n') {
                word_length++;
            }
            if (cursor_col + word_length > total_cols && word_length <= total_cols) {
                console_putc('\n');
            }
        }

        // Don't start a wrapped line with a leftover space.
        if (c == ' ' && cursor_col == 0 && i > 0 && text[i - 1] != '\n') {
            continue;
        }

        console_putc(c);
    }
}

void console_print_number(uint64_t number) {
    // Build the digits backwards (ones first), then print them in order.
    char digits[21];  // enough for the biggest 64-bit number
    int count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number > 0);
    while (count > 0) {
        console_putc(digits[--count]);
    }
}

void console_hide_cursor(void) {
    erase_cursor();
    cursor_visible = false;
}
