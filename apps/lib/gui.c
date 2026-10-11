// gui.c - the AREOS GUI toolkit for apps.

#include "gui.h"
#include "areos.h"
#include "font.h"

// ---- Drawing ----

void gui_fill(struct gui_canvas *c, int x, int y, int w, int h, uint32_t color) {
    // Clip to the canvas so callers can draw partly off-screen safely.
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > c->width ? c->width : x + w;
    int y1 = y + h > c->height ? c->height : y + h;
    for (int row = y0; row < y1; row++) {
        uint32_t *line = c->pixels + (size_t)row * (size_t)c->width;
        for (int col = x0; col < x1; col++) {
            line[col] = color;
        }
    }
}

void gui_frame(struct gui_canvas *c, int x, int y, int w, int h, int thickness, uint32_t color) {
    gui_fill(c, x, y, w, thickness, color);
    gui_fill(c, x, y + h - thickness, w, thickness, color);
    gui_fill(c, x, y, thickness, h, color);
    gui_fill(c, x + w - thickness, y, thickness, h, color);
}

static void draw_char(struct gui_canvas *c, int x, int y, char ch, uint32_t color, int scale) {
    if (ch < FONT_FIRST_CHAR || ch > FONT_LAST_CHAR) {
        ch = '?';
    }
    const uint8_t *glyph = font_glyphs[ch - FONT_FIRST_CHAR];
    for (int gy = 0; gy < FONT_HEIGHT; gy++) {
        for (int gx = 0; gx < FONT_WIDTH; gx++) {
            if (glyph[gy] & (0x80 >> gx)) {
                gui_fill(c, x + gx * scale, y + gy * scale, scale, scale, color);
            }
        }
    }
}

void gui_text(struct gui_canvas *c, int x, int y, const char *text, uint32_t color, int scale) {
    for (int i = 0; text[i] != '\0'; i++) {
        draw_char(c, x + i * FONT_WIDTH * scale, y, text[i], color, scale);
    }
}

int gui_text_width(const char *text, int scale) {
    int length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length * FONT_WIDTH * scale;
}

int gui_text_height(int scale) {
    return FONT_HEIGHT * scale;
}

void gui_text_centered(struct gui_canvas *c, int center_x, int y, const char *text, uint32_t color, int scale) {
    gui_text(c, center_x - gui_text_width(text, scale) / 2, y, text, color, scale);
}

int gui_text_wrapped(struct gui_canvas *c, int x, int y, int max_width, const char *text,
                     uint32_t color, int scale) {
    int char_width = FONT_WIDTH * scale;
    int line_height = FONT_HEIGHT * scale + scale * 2;
    int max_chars = max_width / char_width;
    if (max_chars < 1) {
        max_chars = 1;
    }
    int line_y = y;
    int start = 0;

    while (text[start] != '\0') {
        // Take as many whole words as fit on this line.
        int end = start, last_space = -1;
        while (text[end] != '\0' && text[end] != '\n' && end - start < max_chars) {
            if (text[end] == ' ') {
                last_space = end;
            }
            end++;
        }
        if (text[end] != '\0' && text[end] != '\n' && text[end] != ' ' && last_space > start) {
            end = last_space;  // don't split a word
        }
        for (int i = start; i < end; i++) {
            draw_char(c, x + (i - start) * char_width, line_y, text[i], color, scale);
        }
        line_y += line_height;
        start = end;
        while (text[start] == ' ' || text[start] == '\n') {
            start++;
        }
    }
    return line_y - y;
}

// ---- Themes ----
// Dark: the AREOS look. Light: dark text on a soft background, with
// deeper green and blue so they stay readable on white.

const struct gui_theme gui_theme_dark = {
    .name = "dark",
    .background   = 0x0A0F14,
    .stars        = 0x1E3A40,
    .panel        = 0x111A22,
    .panel_border = 0x24414A,
    .text         = 0xE6F2EE,
    .text_dim     = 0x7A8C9E,
    .accent       = 0x3DFF9A,
    .accent2      = 0x4FC3F7,
    .hover        = 0x16262E,
    .selected     = 0x173A30,
};

const struct gui_theme gui_theme_light = {
    .name = "light",
    .background   = 0xEAF1F5,
    .stars        = 0xC9DCE6,
    .panel        = 0xFFFFFF,
    .panel_border = 0xB4C8D2,
    .text         = 0x0A0F14,
    .text_dim     = 0x4A5C6A,
    .accent       = 0x0B7A4B,
    .accent2      = 0x0B6FA4,
    .hover        = 0xE1EEF4,
    .selected     = 0xD3F0E2,
};

// ---- Accessibility ----

static bool partial_line = false;  // typed characters are waiting on the current line
static char line[512];
static size_t line_length = 0;

static void finish_partial_line(void) {
    if (partial_line) {
        areos_print("\n");
        partial_line = false;
    }
}

void gui_announce(const char *text) {
    finish_partial_line();
    areos_print(text);
    areos_print("\n");
}

void gui_line_start(void) {
    line_length = 0;
    line[0] = '\0';
}

void gui_line_add(const char *text) {
    for (size_t i = 0; text[i] != '\0' && line_length < sizeof(line) - 1; i++) {
        line[line_length++] = text[i];
    }
    line[line_length] = '\0';
}

void gui_line_add_number(int number) {
    char digits[12];
    int count = 0;
    if (number < 0) {
        gui_line_add("-");
        number = -number;
    }
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number > 0);
    char text[12];
    for (int i = 0; i < count; i++) {
        text[i] = digits[count - 1 - i];
    }
    text[count] = '\0';
    gui_line_add(text);
}

void gui_line_say(void) {
    gui_announce(line);
}

static const char *role_name(enum gui_role role) {
    switch (role) {
    case GUI_ROLE_BUTTON:     return "button";
    case GUI_ROLE_RADIO:      return "radio button";
    case GUI_ROLE_TEXT_FIELD: return "edit text";
    case GUI_ROLE_APP:        return "app";
    }
    return "item";
}

void gui_announce_item(const struct gui_item *item, int position, int count, bool detailed) {
    gui_line_start();
    gui_line_add(item->name);
    gui_line_add(", ");
    gui_line_add(role_name(item->role));
    if (item->role == GUI_ROLE_RADIO) {
        gui_line_add(item->checked ? ", selected" : ", not selected");
    }
    if (item->role == GUI_ROLE_TEXT_FIELD) {
        if (item->value != NULL && item->value[0] != '\0') {
            gui_line_add(", ");
            gui_line_add(item->value);
        } else {
            gui_line_add(", blank");
        }
    }
    if (item->disabled) {
        gui_line_add(", unavailable");
    }
    gui_line_add(".");
    if (detailed) {
        gui_line_add(" ");
        gui_line_add_number(position);
        gui_line_add(" of ");
        gui_line_add_number(count);
        gui_line_add(".");
    }
    gui_line_say();
}

void gui_echo_char(char c) {
    areos_write(&c, 1);
    partial_line = true;
}

void gui_echo_backspace(void) {
    areos_write("\b", 1);
}
