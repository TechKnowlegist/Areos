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
};

struct gui_item {
    const char *name;     // what it's called, read out by screen readers
    enum gui_role role;
    int action;           // what happens when it's chosen (app-defined number)
    bool checked;         // radio buttons: is this the chosen one?
    bool disabled;        // shown, but can't be chosen
    const char *value;    // text fields: what's typed in it
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
