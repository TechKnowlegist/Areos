// app_notes.c - the Notes window: a simple place to type.
//
// Typing is echoed to the terminal so you can hear it. "Read it all"
// reads the whole note back line by line. Clearing asks you to press
// Clear twice, so a note isn't wiped by accident.
//
// Notes live in memory: they're kept while the desktop is open.

#include "desktop.h"

#define NOTE_MAX 4000
#define AREA_X 16
#define AREA_Y 16
#define AREA_W 568
#define AREA_H 316

static char note[NOTE_MAX + 1];
static int length = 0;
static bool clear_armed = false;     // Clear was pressed once; press again to really clear
static char summary[48];             // "3 lines, 52 characters", for screen readers

enum { TEXT_AREA, READ_ALL, CLEAR };

static int line_count(void) {
    int lines = 1;
    for (int i = 0; i < length; i++) if (note[i] == '\n') lines++;
    return lines;
}

static void update_summary(void) {
    summary[0] = '\0';
    if (length == 0) {
        text_copy(summary, "empty", sizeof(summary));
        return;
    }
    text_append_number(summary, line_count(), sizeof(summary));
    text_append(summary, line_count() == 1 ? " line, " : " lines, ", sizeof(summary));
    text_append_number(summary, length, sizeof(summary));
    text_append(summary, " characters", sizeof(summary));
}

static int build(struct gui_item *items, int max) {
    (void)max;
    update_summary();
    items[0] = (struct gui_item){ .name = "Note", .role = GUI_ROLE_TEXT_FIELD,
        .action = ACTION(A_CONTENT, TEXT_AREA), .state = summary, .value = NULL,
        .x = AREA_X, .y = AREA_Y, .w = AREA_W, .h = AREA_H };
    items[1] = (struct gui_item){ .name = "Read it all", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_CONTENT, READ_ALL), .x = AREA_X, .y = 346, .w = 170, .h = 38 };
    items[2] = (struct gui_item){ .name = clear_armed ? "Clear (press again)" : "Clear",
        .role = GUI_ROLE_BUTTON, .action = ACTION(A_CONTENT, CLEAR),
        .x = AREA_X + 184, .y = 346, .w = 190, .h = 38 };
    return 3;
}

static void draw(struct gui_canvas *c, struct gui_item *items, int count, int focus) {
    gui_rounded(c, AREA_X, AREA_Y, AREA_W, AREA_H, 8, theme->background);
    if (focus == TEXT_AREA) gui_frame(c, AREA_X, AREA_Y, AREA_W, AREA_H, 2, theme->accent);

    // Break the note into screen lines (at newlines, and when a line gets too wide),
    // then show the last ones that fit, so the newest typing is always visible.
    int columns = (AREA_W - 24) / 8;
    int visible_rows = (AREA_H - 20) / 18;
    static int starts[NOTE_MAX + 2], ends[NOTE_MAX + 2];
    int rows = 0, start = 0;
    for (int i = 0; i <= length; i++) {
        if (i == length || note[i] == '\n' || i - start >= columns) {
            starts[rows] = start;
            ends[rows] = i;
            rows++;
            start = (i < length && note[i] == '\n') ? i + 1 : i;
        }
    }
    int first = rows > visible_rows ? rows - visible_rows : 0;
    int cursor_x = AREA_X + 12, cursor_y = AREA_Y + 10;
    for (int r = first; r < rows; r++) {
        char text[128];
        int n = ends[r] - starts[r];
        if (n > 127) n = 127;
        for (int k = 0; k < n; k++) text[k] = note[starts[r] + k];
        text[n] = '\0';
        int y = AREA_Y + 10 + (r - first) * 18;
        gui_text(c, AREA_X + 12, y, text, theme->text, 1);
        cursor_x = AREA_X + 12 + n * 8;
        cursor_y = y;
    }
    if (length > 0 && note[length - 1] == '\n') {   // the cursor sits on the new empty line
        cursor_x = AREA_X + 12;
    }
    if (length == 0 && focus != TEXT_AREA) {
        gui_text(c, AREA_X + 12, AREA_Y + 10, "Type something...", theme->text_dim, 1);
    }
    if (focus == TEXT_AREA) {
        gui_fill(c, cursor_x + 1, cursor_y, 2, 16, theme->accent);
    }

    for (int i = 1; i < count; i++) {
        draw_window_item(c, &items[i], i == focus, i == hovered_window_item);
    }
}

static bool key(struct areos_key_event *e, struct gui_item *items, int count, int *focus) {
    (void)items; (void)count;
    if (*focus != TEXT_AREA) {
        return false;
    }
    if (e->key == AREOS_KEY_BACKSPACE) {
        if (length > 0) {
            bool was_newline = note[length - 1] == '\n';
            length--;
            note[length] = '\0';
            if (was_newline) gui_announce("Joined lines.");
            else gui_echo_backspace();
            request_redraw();
        }
        return true;
    }
    if (e->key == AREOS_KEY_ENTER) {
        if (length < NOTE_MAX) {
            note[length++] = '\n';
            note[length] = '\0';
            gui_announce("New line.");
            request_redraw();
        }
        return true;
    }
    char c = e->character;
    if (c >= 32 && c <= 126) {
        if (length < NOTE_MAX) {
            note[length++] = c;
            note[length] = '\0';
            gui_echo_char(c);
            clear_armed = false;
            request_redraw();
        }
        return true;
    }
    return false;  // Tab, arrows, Escape... handled as usual
}

static void read_all(void) {
    if (length == 0) {
        gui_announce("The note is empty.");
        return;
    }
    int line = 1, start = 0;
    for (int i = 0; i <= length; i++) {
        if (i == length || note[i] == '\n') {
            char text[160];
            text_copy(text, "Line ", sizeof(text));
            text_append_number(text, line, sizeof(text));
            text_append(text, ": ", sizeof(text));
            int used = text_length(text);
            int n = i - start;
            if (used + n > 159) n = 159 - used;
            for (int k = 0; k < n; k++) text[used + k] = note[start + k];
            text[used + n] = '\0';
            if (n == 0) text_append(text, "blank", sizeof(text));
            gui_announce(text);
            line++;
            start = i + 1;
        }
    }
}

static void choose(int index) {
    if (index == TEXT_AREA) {
        return;   // just focusing the text area
    }
    if (index == READ_ALL) {
        read_all();
    } else if (index == CLEAR) {
        if (length == 0) {
            gui_announce("The note is already empty.");
        } else if (!clear_armed) {
            clear_armed = true;
            gui_announce("Press Clear again to erase the whole note.");
        } else {
            length = 0;
            note[0] = '\0';
            clear_armed = false;
            gui_announce("Note cleared.");
        }
    }
    request_redraw();
}

static void opened(void) {
    gui_announce("Type to write. Tab moves to the buttons.");
}

const struct window_app app_notes = {
    .title = "Notes", .icon = ICON_NOTES, .width = 600, .height = 400,
    .build = build, .draw = draw, .key = key, .choose = choose, .opened = opened,
};
