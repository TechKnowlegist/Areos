// console.h - AREOS text console.
//
// Draws text onto the framebuffer (the screen's pixels) using the
// built-in font. This is how AREOS "prints" anything at all.

#ifndef AREOS_CONSOLE_H
#define AREOS_CONSOLE_H

#include <stdint.h>
#include <limine.h>

// Colors are written as 0xRRGGBB, like web colors (#RRGGBB).
#define COLOR_BACKGROUND  0x0A0F14  // near-black with a hint of blue
#define COLOR_GREEN       0x3DFF9A  // AREOS terminal green
#define COLOR_BLUE        0x4FC3F7  // AREOS sky blue
#define COLOR_DIM         0x7A8C9E  // soft gray-blue for quiet text
#define COLOR_WARNING     0xFFB347  // amber, for things you should read

// How much to enlarge each character. 1 = tiny, 2 = chunky and readable.
#define CONSOLE_SCALE 2

// Empty space around the edge of the screen, in pixels.
#define CONSOLE_MARGIN 32

// Set up the console on a framebuffer and clear the screen.
void console_init(struct limine_framebuffer *fb);

// Fill the whole screen with the background color and go back to the top.
void console_clear(void);

// Change the color used for the text printed after this.
void console_set_color(uint32_t color);

// Print one character. Understands '\n' for a new line.
void console_putc(char c);

// Print a whole string.
void console_print(const char *text);

#endif
