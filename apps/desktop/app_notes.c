// app_notes.c - the Notes window: a simple place to type.
//
// Typing is echoed to the terminal so you can hear it. "Read it all"
// reads the whole note back line by line. Clearing asks you to press
// Clear twice, so a note isn't wiped by accident.
//
// A note can be saved into the in-memory file system (ramfs), so it shows up in
// Files and can be opened again. Ctrl+S saves. Files are kept while AREOS is on.

#include "desktop.h"
#include "ramfs.h"

#define NOTE_MAX 4000
#define AREA_X 16
#define AREA_Y 44
#define AREA_W (content_w - 32)
#define AREA_H (content_h - 44 - 16 - BTN_ROW * button_rows())

#define BTN_ROW 46
static int button_rows(void);

static char note[NOTE_MAX + 1];
static int length = 0;
static bool clear_armed = false;     // Clear was pressed once; press again to really clear
static char summary[48];             // "3 lines, 52 characters", for screen readers

enum { TEXT_AREA, SAVE, SAVE_AS, NEW_NOTE, READ_ALL, CLEAR };

static int file_id = -1;             // the file this note came from or was saved to (-1: none yet)
static uint32_t file_uid = 0;
static bool unsaved = false;           // typed since the last save
static bool new_armed = false;       // New was pressed once while there were unsaved changes
static bool naming = false;          // typing a file name for Save as
static char name_in[RAMFS_NAME_MAX];
static int name_length = 0;
static bool name_fresh = false;      // the suggested name is untouched: the first key replaces it
static char header[120];
static char field_label[40];

static bool has_file(void) { return file_id >= 0 && ramfs_valid(file_id) && ramfs_uid(file_id) == file_uid; }

static const int widths[6] = { 0, 90, 110, 80, 150, 190 };

static int button_rows(void) {
    int x = AREA_X, rows = 1;
    for (int i = 1; i < 6; i++) {
        if (x + widths[i] > content_w - 16 && x > AREA_X) { x = AREA_X; rows++; }
        x += widths[i] + 10;
    }
    return rows;
}

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
    if (naming) {
        text_copy(field_label, "File name", sizeof(field_label));
        items[0] = (struct gui_item){ .name = field_label, .role = GUI_ROLE_TEXT_FIELD,
            .action = ACTION(A_CONTENT, TEXT_AREA), .value = name_in,
            .x = AREA_X, .y = AREA_Y, .w = AREA_W, .h = AREA_H };
    } else {
        text_copy(field_label, "Note", sizeof(field_label));
        items[0] = (struct gui_item){ .name = field_label, .role = GUI_ROLE_TEXT_FIELD,
            .action = ACTION(A_CONTENT, TEXT_AREA), .state = summary, .value = NULL,
            .x = AREA_X, .y = AREA_Y, .w = AREA_W, .h = AREA_H };
    }
    static const char *const names[6] = { "", "Save", "Save as", "New", "Read it all", "Clear" };
    int x = AREA_X, y = content_h - BTN_ROW * button_rows() + 6;
    for (int i = 1; i < 6; i++) {
        if (x + widths[i] > content_w - 16 && x > AREA_X) { x = AREA_X; y += BTN_ROW; }
        const char *name = names[i];
        if (i == NEW_NOTE && new_armed) name = "New (press again)";
        if (i == CLEAR && clear_armed) name = "Clear (press again)";
        int w = widths[i];
        if ((i == NEW_NOTE && new_armed) ) w = 170;
        items[i] = (struct gui_item){ .name = name, .role = GUI_ROLE_BUTTON, .action = ACTION(A_CONTENT, i),
            .x = x, .y = y, .w = w, .h = 38 };
        x += w + 10;
    }
    return 6;
}

static void draw(struct gui_canvas *c, struct gui_item *items, int count, int focus) {
    if (naming) {
        text_copy(header, "Save as: ", sizeof(header));
        text_append(header, name_in, sizeof(header));
    } else {
        text_copy(header, has_file() ? ramfs_name(file_id) : "Untitled note", sizeof(header));
        text_append(header, unsaved ? "  (not saved)" : (has_file() ? "  (saved)" : ""), sizeof(header));
    }
    gui_text_bold(c, AREA_X, 14, header, naming ? theme->accent : theme->text, 1);
    if (naming) {
        int cx = AREA_X + gui_text_width(header, 1);
        gui_fill(c, cx + 2, 12, 2, 16, theme->accent);
    }
    gui_rounded(c, AREA_X, AREA_Y, AREA_W, AREA_H, 8, theme->background);
    if (focus == TEXT_AREA) gui_frame(c, AREA_X, AREA_Y, AREA_W, AREA_H, 2, naming ? theme->text_dim : theme->accent);

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
        char text[256];
        int n = ends[r] - starts[r];
        if (n > 255) n = 255;
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
    if (focus == TEXT_AREA && !naming) {
        gui_fill(c, cursor_x + 1, cursor_y, 2, 16, theme->accent);
    }

    (void)count;
    for (int i = 1; i < 6; i++) {
        draw_window_item(c, &items[i], i == focus, i == hovered_window_item);
    }
}

// ---- Saving and opening ----

static int save_dir(void) {
    int d = files_current_dir();
    if (d == ramfs_root()) {
        int docs = ramfs_find(d, "Documents");
        if (docs >= 0 && ramfs_is_dir(docs)) return docs;
    }
    return d;
}

static void toast_saved(const char *name) {
    char t[80];
    text_copy(t, "Saved ", sizeof(t));
    text_append(t, name, sizeof(t));
    toast_show(t);
}

static void begin_save_as(void) {
    naming = true;
    windows[WIN_NOTES].focus = TEXT_AREA;
    if (has_file()) text_copy(name_in, ramfs_name(file_id), sizeof(name_in));
    else ramfs_unique_name(save_dir(), "Note", ".txt", name_in, sizeof(name_in));
    name_length = text_length(name_in);
    name_fresh = true;
    gui_line_start();
    gui_line_add("Save as. Type a file name, then press Enter. Escape cancels. It says ");
    gui_line_add(name_in);
    gui_line_add(".");
    gui_line_say();
}

static void finish_save_as(void) {
    char name[RAMFS_NAME_MAX];
    text_copy(name, name_in, sizeof(name));
    bool dot = false;
    for (int i = 0; name[i]; i++) if (name[i] == '.') dot = true;
    if (!dot && text_length(name) + 4 < RAMFS_NAME_MAX) text_append(name, ".txt", sizeof(name));
    if (!ramfs_name_ok(name)) { gui_announce(ramfs_error_words(RAMFS_BAD_NAME)); return; }
    int dir = has_file() ? ramfs_parent(file_id) : save_dir();
    int existing = ramfs_find(dir, name);
    if (existing >= 0 && !(has_file() && existing == file_id)) {
        gui_announce(ramfs_is_dir(existing) ? "A folder has that name. Type another name." : "A file with that name already exists. Type another name.");
        return;
    }
    int id = existing;
    bool made = false;
    if (id < 0) {
        id = ramfs_create(dir, name, false);
        if (id < 0) { gui_announce(ramfs_error_words(id)); return; }
        made = true;
    }
    int result = ramfs_write(id, note, length);
    if (result != RAMFS_OK) {
        if (made) ramfs_remove(id);
        gui_announce(ramfs_error_words(result));
        return;
    }
    file_id = id; file_uid = ramfs_uid(id);
    unsaved = false; naming = false;
    char path[160];
    ramfs_path(id, path, sizeof(path));
    gui_line_start(); gui_line_add("Saved as "); gui_line_add(path); gui_line_add("."); gui_line_say();
    toast_saved(name);
}

static void save_now(void) {
    if (naming) { finish_save_as(); return; }
    if (!has_file()) { begin_save_as(); return; }
    int result = ramfs_write(file_id, note, length);
    if (result != RAMFS_OK) { gui_announce(ramfs_error_words(result)); return; }
    unsaved = false;
    gui_line_start(); gui_line_add("Saved "); gui_line_add(ramfs_name(file_id)); gui_line_add("."); gui_line_say();
    toast_saved(ramfs_name(file_id));
}

void notes_open_file(int id) {
    if (!ramfs_valid(id) || ramfs_is_dir(id)) return;
    if (has_file() && file_id == id) { open_window(WIN_NOTES); gui_announce("That file is already open in Notes."); return; }
    if (unsaved) {
        open_window(WIN_NOTES);
        gui_announce("Notes has changes that aren't saved. Save or clear them first, then open the file again.");
        return;
    }
    const char *data = ramfs_data(id);
    int size = ramfs_size(id);
    length = 0;
    bool cut = false;
    for (int i = 0; i < size; i++) {
        char ch = data[i];
        if (ch == '\r') continue;
        if (ch == '\t') ch = ' ';
        if (ch != '\n' && (ch < 32 || ch > 126)) ch = '?';
        if (length >= NOTE_MAX) { cut = true; break; }
        note[length++] = ch;
    }
    note[length] = '\0';
    clear_armed = new_armed = naming = false;
    unsaved = false;
    file_id = cut ? -1 : id;
    file_uid = cut ? 0 : ramfs_uid(id);
    open_window(WIN_NOTES);
    windows[WIN_NOTES].focus = TEXT_AREA;
    gui_line_start();
    gui_line_add("Opened "); gui_line_add(ramfs_name(id)); gui_line_add(". ");
    update_summary();
    gui_line_add(summary); gui_line_add(".");
    if (cut) gui_line_add(" It was too long, so only the first part is here. Save as a new name to keep it.");
    gui_line_say();
    if (cut) unsaved = true;
}

// A file or folder is about to be deleted in Files.
void notes_file_deleted(int id) {
    if (file_id >= 0 && ramfs_valid(file_id) && ramfs_is_inside(file_id, id)) {
        file_id = -1;
        if (length > 0) unsaved = true;
    }
}

static bool key(struct areos_key_event *e, struct gui_item *items, int count, int *focus) {
    (void)items; (void)count;
    if (ctrl_held && e->key == 's') { save_now(); return true; }
    if (*focus != TEXT_AREA) {
        return false;
    }
    if (naming) {
        if (e->key == AREOS_KEY_ESCAPE) { naming = false; gui_announce("Cancelled."); return true; }
        if (e->key == AREOS_KEY_ENTER) { finish_save_as(); return true; }
        if (e->key == AREOS_KEY_BACKSPACE) {
            if (name_fresh) { name_in[0] = '\0'; name_length = 0; name_fresh = false; gui_announce("Name cleared."); return true; }
            if (name_length > 0) { name_in[--name_length] = '\0'; gui_echo_backspace(); }
            return true;
        }
        char ch = e->character;
        if (ch >= 32 && ch <= 126 && ch != '/' && ch != '\\') {
            if (name_fresh) { name_in[0] = '\0'; name_length = 0; name_fresh = false; }
            if (name_length < RAMFS_NAME_MAX - 1) { name_in[name_length++] = ch; name_in[name_length] = '\0'; gui_echo_char(ch); }
            return true;
        }
        return false;
    }
    if (e->key == AREOS_KEY_BACKSPACE) {
        if (length > 0) {
            bool was_newline = note[length - 1] == '\n';
            length--;
            note[length] = '\0';
            unsaved = true;
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
            unsaved = true;
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
            unsaved = true;
            gui_echo_char(c);
            clear_armed = false;
            new_armed = false;
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
    if (naming && index != SAVE && index != SAVE_AS) naming = false;
    if (index != CLEAR) clear_armed = false;
    if (index != NEW_NOTE) new_armed = false;
    if (index == SAVE) {
        save_now();
    } else if (index == SAVE_AS) {
        if (naming) finish_save_as(); else begin_save_as();
    } else if (index == NEW_NOTE) {
        if (length == 0 && !has_file()) {
            gui_announce("This is already a new, empty note.");
        } else if (unsaved && !new_armed) {
            new_armed = true;
            gui_announce("This note has changes that aren't saved. Press New again to start over without saving.");
        } else {
            length = 0; note[0] = '\0';
            file_id = -1; unsaved = false; new_armed = false;
            windows[WIN_NOTES].focus = TEXT_AREA;
            gui_announce("New note.");
        }
    } else if (index == READ_ALL) {
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
            unsaved = true;
            gui_announce("Note cleared.");
        }
    }
    request_redraw();
}

static void opened(void) {
    gui_announce("Type to write. Control S saves. Tab moves to the buttons.");
}

const struct window_app app_notes = {
    .title = "Notes", .icon = ICON_NOTES, .width = 700, .height = 440, .resizable = true,
    .build = build, .draw = draw, .key = key, .choose = choose, .opened = opened,
};
