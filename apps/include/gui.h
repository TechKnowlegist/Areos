// gui.h - the AREOS GUI toolkit for apps.
//
// Three parts:
//   1. Drawing: rectangles and text onto a canvas (a grid of pixels).
//   2. Themes: the dark and light color sets.
//   3. Accessibility: describing things in words. Every item on screen
//      (button, radio button, text field...) has a name and a role, and
//      whenever focus moves, the toolkit announces it to the terminal,
//      where a screen reader like NVDA reads it out.

#ifndef AREOS_GUI_H
#define AREOS_GUI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---- Drawing ----

struct gui_canvas {
    uint32_t *pixels;   // 0x00RRGGBB, row by row
    int width, height;
};

void gui_fill(struct gui_canvas *c, int x, int y, int w, int h, uint32_t color);
void gui_frame(struct gui_canvas *c, int x, int y, int w, int h, int thickness, uint32_t color);

// Text uses the built-in 8x16 font, made `scale` times bigger.
void gui_text(struct gui_canvas *c, int x, int y, const char *text, uint32_t color, int scale);
int gui_text_width(const char *text, int scale);
int gui_text_height(int scale);
void gui_text_centered(struct gui_canvas *c, int center_x, int y, const char *text, uint32_t color, int scale);

// Text that wraps onto new lines to fit `max_width`. Returns the height used.
int gui_text_wrapped(struct gui_canvas *c, int x, int y, int max_width, const char *text,
                     uint32_t color, int scale);

// Slightly heavier text (drawn twice, one pixel apart), for titles.
void gui_text_bold(struct gui_canvas *c, int x, int y, const char *text, uint32_t color, int scale);

// ---- Shapes and see-through drawing ----
// `alpha` is how solid something is: 0 = invisible, 255 = fully solid.

// Mix two colors: alpha 255 gives `top`, 0 gives `bottom`.
static inline uint32_t gui_blend(uint32_t bottom, uint32_t top, int alpha) {
    if (alpha >= 255) return top;
    if (alpha <= 0) return bottom;
    uint32_t r = (((top >> 16) & 0xFF) * (uint32_t)alpha + ((bottom >> 16) & 0xFF) * (uint32_t)(255 - alpha)) / 255;
    uint32_t g = (((top >> 8) & 0xFF) * (uint32_t)alpha + ((bottom >> 8) & 0xFF) * (uint32_t)(255 - alpha)) / 255;
    uint32_t b = ((top & 0xFF) * (uint32_t)alpha + (bottom & 0xFF) * (uint32_t)(255 - alpha)) / 255;
    return (r << 16) | (g << 8) | b;
}

void gui_fill_alpha(struct gui_canvas *c, int x, int y, int w, int h, uint32_t color, int alpha);
void gui_rounded(struct gui_canvas *c, int x, int y, int w, int h, int radius, uint32_t color);
void gui_rounded_alpha(struct gui_canvas *c, int x, int y, int w, int h, int radius, uint32_t color, int alpha);
void gui_circle(struct gui_canvas *c, int center_x, int center_y, int radius, uint32_t color);
void gui_ring(struct gui_canvas *c, int center_x, int center_y, int radius, int thickness, uint32_t color);
void gui_line(struct gui_canvas *c, int x0, int y0, int x1, int y1, int thickness, uint32_t color);

// A soft shadow around a rectangle, for things floating above others.
void gui_shadow(struct gui_canvas *c, int x, int y, int w, int h, int size);

// Copy one canvas onto another at (x, y).
void gui_blit(struct gui_canvas *to, int x, int y, const struct gui_canvas *from);

// Copy a canvas onto another, stretched to w x h and see-through by `alpha`.
// Used for animations (zooming and fading windows).
void gui_blit_scaled(struct gui_canvas *to, int x, int y, int w, int h,
                     const struct gui_canvas *from, int alpha);

// Ease-out curve for animations: progress 0..1000 -> smoothed 0..1000.
int gui_ease_out(int progress);

// ---- Themes ----

struct gui_theme {
    const char *name;
    uint32_t background;
    uint32_t stars;        // little background dots, for the space look
    uint32_t panel;
    uint32_t panel_border;
    uint32_t text;
    uint32_t text_dim;
    uint32_t accent;       // AREOS green (or a darker green on light backgrounds)
    uint32_t accent2;      // AREOS blue
    uint32_t hover;        // fill for something under the mouse
    uint32_t selected;     // fill for something focused
};

extern const struct gui_theme gui_theme_dark;
extern const struct gui_theme gui_theme_light;

// ---- Accessibility ----

enum gui_role {
    GUI_ROLE_BUTTON,
    GUI_ROLE_RADIO,
    GUI_ROLE_TEXT_FIELD,
    GUI_ROLE_APP,
    GUI_ROLE_ICON,        // a desktop icon
    GUI_ROLE_MENU_ITEM,   // an entry in a menu (like the Start menu)
    GUI_ROLE_TASKBAR,     // a button on the taskbar
    GUI_ROLE_SLIDER,      // a value you slide left and right (value says what it's at)
    GUI_ROLE_SWITCH,      // an on/off toggle (checked = on)
    GUI_ROLE_TAB,         // one of a set of pages, like Settings categories (checked = showing)
    GUI_ROLE_LINK,        // a link on a web page
    GUI_ROLE_HEADING,     // a heading on a web page
    GUI_ROLE_DOCUMENT,    // a whole web page
};

struct gui_item {
    const char *name;     // what it's called, read out by screen readers
    enum gui_role role;
    int action;           // what happens when it's chosen (app-defined number)
    bool checked;         // radio buttons: is this the chosen one?
    bool disabled;        // shown, but can't be chosen
    const char *value;    // text fields: what's typed in it; sliders: where it's at ("150%")
    const char *state;    // extra words to say, like "minimized" (or NULL)
    int x, y, w, h;       // where it is on screen
};

// Say a whole line in the terminal.
void gui_announce(const char *text);

// Build a line bit by bit, then say it.
void gui_line_start(void);
void gui_line_add(const char *text);
void gui_line_add_number(int number);
void gui_line_say(void);

// Say what an item is: "Light mode, radio button, not selected. 2 of 2."
// `detailed` adds the position ("2 of 2"); brief mode leaves it out.
void gui_announce_item(const struct gui_item *item, int position, int count, bool detailed);

// Echo a typed character to the terminal (so typing can be heard), or
// erase the last one for Backspace.
void gui_echo_char(char c);
void gui_echo_backspace(void);

#endif
