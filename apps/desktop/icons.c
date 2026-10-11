// icons.c - the little pictures for apps and buttons.
//
// Each icon is a rounded colored tile with a simple picture on it, built
// from rectangles, circles and lines, so it looks sharp at any size.
// Sizes below are written as fractions of `s` (the icon's size).

#include "desktop.h"

#define WHITE 0xFFFFFF

// Fill a triangle (used for the rocket and the warning sign).
static void triangle(struct gui_canvas *c, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    // Sort the corners top to bottom.
    if (y1 < y0) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y2 < y0) { int t = x0; x0 = x2; x2 = t; t = y0; y0 = y2; y2 = t; }
    if (y2 < y1) { int t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    for (int y = y0; y <= y2; y++) {
        // Where the long edge (0 to 2) and the short edges cross this row.
        int xa = y2 == y0 ? x0 : x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int xb;
        if (y < y1) xb = y1 == y0 ? x0 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        else        xb = y2 == y1 ? x1 : x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        if (xa > xb) { int t = xa; xa = xb; xb = t; }
        gui_fill(c, xa, y, xb - xa + 1, 1, color);
    }
}

// The rounded tile behind each picture, with a lighter top half for depth.
static void tile(struct gui_canvas *c, int x, int y, int s, uint32_t color) {
    gui_rounded(c, x, y, s, s, s / 5, color);
    gui_rounded_alpha(c, x + 2, y + 2, s - 4, s / 2 - 2, s / 6, WHITE, 28);
}

// 16 points around a circle (cos and sin times 1000), for rings and gears.
static const int circle_x[16] = { 1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383, 0, 383, 707, 924 };
static const int circle_y[16] = { 0, 383, 707, 924, 1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383 };

// Part of a ring, made of straight pieces between the circle points,
// going round from point `from` to point `to` (0-15, clockwise from the right).
static void arc(struct gui_canvas *c, int cx, int cy, int r, int from, int to, int thickness, uint32_t color) {
    for (int i = from; i % 16 != to; i++) {
        int a = i % 16, b = (i + 1) % 16;
        gui_line(c, cx + circle_x[a] * r / 1000, cy + circle_y[a] * r / 1000,
                 cx + circle_x[b] * r / 1000, cy + circle_y[b] * r / 1000, thickness, color);
    }
}

static void draw_gear(struct gui_canvas *c, int cx, int cy, int r, uint32_t color, uint32_t hole) {
    for (int i = 0; i < 16; i += 2) {  // 8 teeth
        int tx = cx + circle_x[i] * r / 1000, ty = cy + circle_y[i] * r / 1000;
        gui_circle(c, tx, ty, r / 4 + 1, color);
    }
    gui_circle(c, cx, cy, r * 4 / 5, color);
    gui_circle(c, cx, cy, r / 3, hole);
}

static void draw_logo(struct gui_canvas *c, int x, int y, int s) {
    int cx = x + s / 2, cy = y + s / 2, r = s * 3 / 10;
    // The planet: green on top fading to blue.
    for (int dy = -r; dy <= r; dy++) {
        int half = 0;
        while ((half + 1) * (half + 1) + dy * dy <= r * r) half++;
        uint32_t color = gui_blend(0x3DFF9A, 0x4FC3F7, (dy + r) * 255 / (2 * r));
        gui_fill(c, cx - half, cy + dy, half * 2 + 1, 1, color);
    }
    // A tilted ring around it.
    int prev_x = 0, prev_y = 0;
    for (int i = 0; i <= 16; i++) {
        int px = cx + circle_x[i % 16] * (s * 46 / 100) / 1000;
        int py = cy + circle_y[i % 16] * (s * 12 / 100) / 1000 - (px - cx) / 4;
        if (i > 0 && !(i > 4 && i < 12 && py < cy)) {  // skip the part behind the planet
            gui_line(c, prev_x, prev_y, px, py, s / 24 + 1, 0xE6F2EE);
        }
        prev_x = px;
        prev_y = py;
    }
}

void icon_draw(struct gui_canvas *c, enum icon_kind kind, int x, int y, int s) {
    int cx = x + s / 2, cy = y + s / 2;
    int line = s / 16 + 1;
    switch (kind) {
    case ICON_LOGO:
        draw_logo(c, x, y, s);
        break;

    case ICON_SETTINGS:
        tile(c, x, y, s, 0x4A5C6A);
        draw_gear(c, cx, cy, s * 3 / 10, 0xE6F2EE, 0x4A5C6A);
        break;

    case ICON_ABOUT:
        tile(c, x, y, s, 0x1B9E6A);
        gui_circle(c, cx, y + s * 27 / 100, s / 12 + 1, WHITE);
        gui_rounded(c, cx - s / 12, y + s * 40 / 100, s / 6, s * 38 / 100, s / 16, WHITE);
        break;

    case ICON_TERMINAL:
        tile(c, x, y, s, 0x0E151C);
        gui_frame(c, x, y, s, s, 1, 0x24414A);
        gui_line(c, x + s * 25 / 100, y + s * 35 / 100, x + s * 42 / 100, y + s / 2, line + 1, 0x3DFF9A);
        gui_line(c, x + s * 42 / 100, y + s / 2, x + s * 25 / 100, y + s * 65 / 100, line + 1, 0x3DFF9A);
        gui_fill(c, x + s * 50 / 100, y + s * 63 / 100, s / 4, line + 1, 0x3DFF9A);
        break;

    case ICON_CALCULATOR:
        tile(c, x, y, s, 0xE07B39);
        gui_rounded(c, x + s / 5, y + s / 6, s * 3 / 5, s / 5, s / 20, 0xFFF4E8);   // display
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                gui_rounded(c, x + s / 5 + col * s / 5 + s / 40, y + s * 45 / 100 + row * s / 7,
                            s / 7, s / 10, s / 30, WHITE);
            }
        }
        break;

    case ICON_NOTES:
        tile(c, x, y, s, 0xF2C94C);
        gui_rounded(c, x + s / 5, y + s / 6, s * 3 / 5, s * 2 / 3, s / 16, 0xFFFBEA);
        for (int i = 0; i < 4; i++) {
            gui_fill(c, x + s * 28 / 100, y + s * 32 / 100 + i * s / 8, s * 44 / 100 - (i == 3 ? s / 6 : 0),
                     line, 0xB08A20);
        }
        break;

    case ICON_STARFIELD:
        tile(c, x, y, s, 0x101C3A);
        gui_circle(c, cx, cy, s / 14 + 1, 0x4FC3F7);
        gui_circle(c, x + s / 4, y + s / 3, s / 24 + 1, 0x3DFF9A);
        gui_circle(c, x + s * 7 / 10, y + s / 4, s / 30 + 1, WHITE);
        gui_circle(c, x + s * 3 / 10, y + s * 3 / 4, s / 30 + 1, WHITE);
        gui_circle(c, x + s * 3 / 4, y + s * 7 / 10, s / 20 + 1, 0x3DFF9A);
        gui_line(c, cx, cy, x + s * 85 / 100, y + s * 85 / 100, line, 0x4FC3F7);
        break;

    case ICON_HELLO:
        tile(c, x, y, s, 0x2A78B0);
        gui_rounded(c, x + s / 6, y + s / 5, s * 2 / 3, s * 2 / 5, s / 8, WHITE);
        triangle(c, x + s * 30 / 100, y + s * 58 / 100, x + s * 45 / 100, y + s * 58 / 100,
                 x + s * 26 / 100, y + s * 76 / 100, WHITE);
        for (int i = 0; i < 3; i++) {
            gui_circle(c, x + s * 35 / 100 + i * s * 15 / 100, y + s * 40 / 100, s / 22 + 1, 0x2A78B0);
        }
        break;

    case ICON_COUNTDOWN:
        tile(c, x, y, s, 0x2B2F4A);
        triangle(c, cx, y + s / 7, cx - s / 8, y + s * 35 / 100, cx + s / 8, y + s * 35 / 100, 0xE6F2EE);
        gui_fill(c, cx - s / 8, y + s * 35 / 100, s / 4 + 1, s * 3 / 10, 0xE6F2EE);
        gui_circle(c, cx, y + s * 45 / 100, s / 16 + 1, 0x4FC3F7);
        triangle(c, cx - s / 8, y + s * 50 / 100, cx - s / 4, y + s * 68 / 100, cx - s / 8, y + s * 65 / 100, 0xD0643A);
        triangle(c, cx + s / 8, y + s * 50 / 100, cx + s / 4, y + s * 68 / 100, cx + s / 8, y + s * 65 / 100, 0xD0643A);
        triangle(c, cx - s / 12, y + s * 65 / 100, cx + s / 12, y + s * 65 / 100, cx, y + s * 86 / 100, 0xFFB347);
        break;

    case ICON_OOPS:
        tile(c, x, y, s, 0x3A2A1C);
        triangle(c, cx, y + s / 6, x + s / 6, y + s * 80 / 100, x + s * 5 / 6, y + s * 80 / 100, 0xFFB347);
        gui_fill(c, cx - line / 2 - 1, y + s * 38 / 100, line + 2, s / 4, 0x3A2A1C);
        gui_circle(c, cx, y + s * 70 / 100, line, 0x3A2A1C);
        break;

    case ICON_POWER:
        // A ring with a gap at the top, and a line through the gap.
        arc(c, cx, cy + s / 20, s * 3 / 10, 13, 11, line + 1, 0xFF6B6B);
        gui_fill(c, cx - line / 2 - 1, y + s / 8, line + 2, s * 7 / 20, 0xFF6B6B);
        break;

    case ICON_RESTART:
        // A ring with a gap, and an arrow head at one end.
        arc(c, cx, cy, s * 3 / 10, 14, 11, line + 1, 0x4FC3F7);
        triangle(c, cx + s / 12, y + s * 12 / 100, cx + s / 12, y + s * 34 / 100, cx + s / 4, y + s * 22 / 100, 0x4FC3F7);
        break;

    case ICON_BROWSER:
        // A globe: a blue ball with lines of latitude and longitude.
        tile(c, x, y, s, 0x1F5FA8);
        gui_circle(c, cx, cy, s * 3 / 10, 0x8FD3FF);
        gui_ring(c, cx, cy, s * 3 / 10, line, WHITE);
        gui_fill(c, cx - s * 3 / 10, cy - line / 2, s * 3 / 5, line, WHITE);
        gui_fill(c, cx - line / 2, cy - s * 3 / 10, line, s * 3 / 5, WHITE);
        gui_fill(c, cx - s / 4, cy - s * 15 / 100, s / 2, line > 1 ? line - 1 : 1, WHITE);
        gui_fill(c, cx - s / 4, cy + s * 15 / 100, s / 2, line > 1 ? line - 1 : 1, WHITE);
        gui_ring(c, cx, cy, s / 8, line > 1 ? line - 1 : 1, WHITE);
        break;
    case ICON_QUICK:
        // Two sliders: quick settings.
        gui_fill(c, x + s / 6, y + s / 3, s * 2 / 3, line, theme->text);
        gui_fill(c, x + s / 6, y + s * 2 / 3, s * 2 / 3, line, theme->text);
        gui_circle(c, x + s * 2 / 5, y + s / 3, s / 9 + 1, theme->accent);
        gui_circle(c, x + s * 2 / 3, y + s * 2 / 3, s / 9 + 1, theme->accent);
        break;
    case ICON_PROGRAM:
    default:
        tile(c, x, y, s, 0x2E5E8A);
        gui_rounded(c, x + s / 6, y + s / 5, s * 2 / 3, s * 3 / 5, s / 14, WHITE);
        gui_fill(c, x + s / 6, y + s / 5 + s / 14, s * 2 / 3, s / 8, 0x4FC3F7);
        break;
    }
}

// Pick an icon for an app from its name.
enum icon_kind icon_for_app(const char *label) {
    if (text_equal(label, "Starfield")) return ICON_STARFIELD;
    if (text_equal(label, "Browser")) return ICON_BROWSER;
    if (text_equal(label, "Hello")) return ICON_HELLO;
    if (text_equal(label, "Countdown")) return ICON_COUNTDOWN;
    if (text_equal(label, "Oops")) return ICON_OOPS;
    return ICON_PROGRAM;
}
