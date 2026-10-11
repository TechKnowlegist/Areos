// graphics.h - letting programs draw pictures.
//
// A program draws into its own grid of pixels (any size it likes, say
// 320x200 like Doom), then hands it to AREOS to show. AREOS scales it up
// to fill as much of the screen as it can, keeping the shape right.
//
// While a program is in graphics mode, the text console steps aside.
// Text still goes to the terminal. When the program finishes (or
// crashes), the screen is put back exactly as it was.

#ifndef AREOS_GRAPHICS_H
#define AREOS_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>
#include <limine.h>

#define GRAPHICS_MAX_SIZE 4096  // biggest width or height a program may draw

void graphics_init(struct limine_framebuffer *framebuffer);

// Switch to graphics mode. Returns false if there isn't memory to do so.
bool graphics_start(void);

// Show a picture: `width` x `height` pixels, each 0x00RRGGBB, row by row.
void graphics_draw(const uint32_t *pixels, uint32_t width, uint32_t height);

// Go back to text, restoring what was on the screen before.
void graphics_stop(void);

bool graphics_active(void);

// Turn a point on the screen into a pixel of the app's picture (which
// may be zoomed and centered).
void graphics_screen_to_picture(uint32_t *x, uint32_t *y);

uint32_t graphics_screen_width(void);
uint32_t graphics_screen_height(void);

#endif
