// web_font.h - text: sizes, widths and pictures of letters.
//
// The letters come from TrueType fonts built into the browser (Liberation
// Sans, Serif and Mono, and DejaVu Sans for symbols). Everything here uses
// whole numbers: sizes are in pixels, widths in 1/64 of a pixel.

#ifndef WEB_FONT_H
#define WEB_FONT_H

#include "web_port.h"

enum { WF_SANS, WF_SERIF, WF_MONO };

// A font is picked by family, weight and slant. Returns a font number.
int wfont_pick(int family, bool bold, bool italic);

// Ascent (above the baseline) and descent (below it), and the font's extra
// line gap, for text `px` pixels tall.
void wfont_vmetrics(int font, int px, int *ascent, int *descent, int *line_gap);

// How wide a letter is, in 1/64 pixels.
int wfont_advance(int font, int px, uint32_t cp);

// Width of some UTF-8 text in 1/64 pixels.
int wfont_text_width(int font, int px, const char *text, int length);

// The picture of one letter: an 8-bit coverage map, drawn with its top-left
// at (pen_x + x, baseline + y).
struct wglyph {
    int x, y;
    int w, h;
    const uint8_t *alpha;
};
const struct wglyph *wfont_glyph(int font, int px, uint32_t cp);

// Is this letter drawn with an emoji-like symbol the fonts lack? (Nothing is drawn.)
bool wfont_has(int font, uint32_t cp);

#endif
