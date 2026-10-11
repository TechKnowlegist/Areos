// font.h - AREOS built-in bitmap font.
//
// The screen is just pixels, so to show text we keep a tiny picture of
// every character. Each character is 8 pixels wide and 16 pixels tall.

#ifndef AREOS_FONT_H
#define AREOS_FONT_H

#include <stdint.h>

#define FONT_WIDTH        8
#define FONT_HEIGHT       16
#define FONT_FIRST_CHAR   32   // ' ' (space)
#define FONT_LAST_CHAR    126  // '~'
#define FONT_GLYPH_COUNT  (FONT_LAST_CHAR - FONT_FIRST_CHAR + 1)

// font_glyphs[c - FONT_FIRST_CHAR][row] is one row of character c.
// The highest bit (0x80) is the leftmost pixel.
extern const uint8_t font_glyphs[FONT_GLYPH_COUNT][FONT_HEIGHT];

#endif
