// app_files.c - the Files window: look around the in-memory file system,
// make folders and documents, rename and delete them, and open documents in Notes.
//
// Keyboard: Up and Down choose, Enter opens, Backspace goes up a folder,
// F4 renames, Delete deletes (press it twice), and typing a letter jumps to
// the next name that starts with it. Tab moves to the buttons.

#include "desktop.h"
#include "ramfs.h"

#define LIST_X 16
#define LIST_Y 44
#define ROW_H 26
#define BTN_H 38
#define BTN_ROW 46

enum { LIST, B_UP, B_OPEN, B_NEW_FOLDER, B_NEW_DOC, B_RENAME, B_DELETE, B_OK, B_CANCEL };
enum { M_BROWSE, M_NEW_FOLDER, M_NEW_DOC, M_RENAME };

static int cur = 0;                  // the folder being shown
static int ids[RAMFS_MAX_NODES];     // what is in it
static int count = 0;
static int sel = 0, scroll = 0;
static int mode = M_BROWSE;
static char input[RAMFS_NAME_MAX];
static int input_length = 0;
static bool input_fresh = false;     // the suggested name is still untouched: the first key replaces it
static int rename_uid = 0, rename_id = -1;
static bool delete_armed = false;
static char list_state[200];
static char field_name[48];
static char path_text[200];

int files_current_dir(void) {
    if (!ramfs_is_dir(cur)) cur = ramfs_root();
    return cur;
}

static void refresh(void) {
    if (!ramfs_is_dir(cur)) cur = ramfs_root();
    count = ramfs_list(cur, ids, RAMFS_MAX_NODES);
    if (sel >= count) sel = count - 1;
    if (sel < 0) sel = 0;
}

static void describe(int id, char *out, int max) {
    text_copy(out, ramfs_name(id), max);
    if (ramfs_is_dir(id)) {
        int n = ramfs_size(id);
        text_append(out, ", folder, ", max);
        if (n == 0) text_append(out, "empty", max);
        else { text_append_number(out, n, max); text_append(out, n == 1 ? " item" : " items", max); }
    } else {
        int n = ramfs_size(id);
        text_append(out, ", document, ", max);
        text_append_number(out, n, max);
        text_append(out, n == 1 ? " byte" : " bytes", max);
    }
}

static void say_selection(void) {
    char text[220];
    if (count == 0) { gui_announce("This folder is empty."); return; }
    describe(ids[sel], text, sizeof(text));
    text_append(text, ". ", sizeof(text));
    text_append_number(text, sel + 1, sizeof(text));
    text_append(text, " of ", sizeof(text));
    text_append_number(text, count, sizeof(text));
    text_append(text, ".", sizeof(text));
    gui_announce(text);
}

static void say_folder(void) {
    char text[220];
    text_copy(text, cur == ramfs_root() ? "Files" : ramfs_name(cur), sizeof(text));
    text_append(text, ", folder. ", sizeof(text));
    if (count == 0) text_append(text, "Empty.", sizeof(text));
    else { text_append_number(text, count, sizeof(text)); text_append(text, count == 1 ? " item." : " items.", sizeof(text)); }
    gui_announce(text);
}

// ---- Layout ----

static const int browse_widths[6] = { 90, 90, 130, 150, 110, 200 };
static const int naming_widths[2] = { 130, 110 };

static int button_rows(void) {
    const int *w = mode == M_BROWSE ? browse_widths : naming_widths;
    int n = mode == M_BROWSE ? 6 : 2;
    int x = LIST_X, rows = 1;
    for (int i = 0; i < n; i++) {
        if (x + w[i] > content_w - 16 && x > LIST_X) { x = LIST_X; rows++; }
        x += w[i] + 10;
    }
    return rows;
}

static int list_h(void) {
    int h = content_h - LIST_Y - button_rows() * BTN_ROW - 10;
    return h < ROW_H * 2 + 8 ? ROW_H * 2 + 8 : h;
}

static int visible_rows(void) { return (list_h() - 8) / ROW_H; }

static void keep_visible(void) {
    int rows = visible_rows();
    if (rows < 1) rows = 1;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + rows) scroll = sel - rows + 1;
    if (scroll < 0) scroll = 0;
}

static int build(struct gui_item *items, int max) {
    (void)max;
    refresh();
    keep_visible();
    int n = 0;
    int list_w = content_w - 32;
    // path for the screen reader
    ramfs_path(cur, path_text, sizeof(path_text));
    if (mode == M_BROWSE) {
        list_state[0] = '\0';
        if (count == 0) text_copy(list_state, "empty", sizeof(list_state));
        else describe(ids[sel], list_state, sizeof(list_state));
        if (count > 0) {
            text_append(list_state, ". ", sizeof(list_state));
            text_append_number(list_state, sel + 1, sizeof(list_state));
            text_append(list_state, " of ", sizeof(list_state));
            text_append_number(list_state, count, sizeof(list_state));
        }
        field_name[0] = '\0';
        text_copy(field_name, "Files in ", sizeof(field_name));
        text_append(field_name, cur == ramfs_root() ? "the top folder" : ramfs_name(cur), sizeof(field_name));
        items[n++] = (struct gui_item){ .name = field_name, .role = GUI_ROLE_LIST, .action = ACTION(A_CONTENT, LIST),
            .state = list_state, .x = LIST_X, .y = LIST_Y, .w = list_w, .h = list_h() };
    } else {
        text_copy(field_name, mode == M_RENAME ? "New name" : (mode == M_NEW_FOLDER ? "Name for the new folder" : "Name for the new document"), sizeof(field_name));
        items[n++] = (struct gui_item){ .name = field_name, .role = GUI_ROLE_TEXT_FIELD, .action = ACTION(A_CONTENT, LIST),
            .value = input, .x = LIST_X, .y = 6, .w = list_w, .h = 32 };
    }
    int x = LIST_X, y = LIST_Y + list_h() + 8;
    const int *w = mode == M_BROWSE ? browse_widths : naming_widths;
    int total = mode == M_BROWSE ? 6 : 2;
    for (int i = 0; i < total; i++) {
        if (x + w[i] > content_w - 16 && x > LIST_X) { x = LIST_X; y += BTN_ROW; }
        struct gui_item it = { .role = GUI_ROLE_BUTTON, .x = x, .y = y, .w = w[i], .h = BTN_H };
        if (mode == M_BROWSE) {
            static const char *const names[6] = { "Go up", "Open", "New folder", "New document", "Rename", "Delete" };
            static const int acts[6] = { B_UP, B_OPEN, B_NEW_FOLDER, B_NEW_DOC, B_RENAME, B_DELETE };
            it.name = names[i]; it.action = ACTION(A_CONTENT, acts[i]);
            if (i == 0 && cur == ramfs_root()) it.disabled = true;
            if ((i == 1 || i == 4 || i == 5) && count == 0) it.disabled = true;
            if (i == 5 && delete_armed) it.name = "Delete (press again)";
        } else {
            it.name = i == 0 ? (mode == M_RENAME ? "Rename" : "Create") : "Cancel";
            it.action = ACTION(A_CONTENT, i == 0 ? B_OK : B_CANCEL);
        }
        items[n++] = it;
        x += w[i] + 10;
    }
    return n;
}

// ---- Drawing ----

static void draw_folder_icon(struct gui_canvas *c, int x, int y) {
    gui_rounded(c, x, y + 2, 10, 6, 2, 0xD9A92B);
    gui_rounded(c, x, y + 5, 20, 13, 3, 0xF2C94C);
}

static void draw_doc_icon(struct gui_canvas *c, int x, int y) {
    gui_rounded(c, x + 3, y, 14, 18, 2, 0xF4F4F4);
    gui_fill(c, x + 6, y + 5, 8, 1, 0x888888);
    gui_fill(c, x + 6, y + 9, 8, 1, 0x888888);
    gui_fill(c, x + 6, y + 13, 5, 1, 0x888888);
}

static void draw(struct gui_canvas *c, struct gui_item *items, int n, int focus) {
    int list_w = content_w - 32;
    int lh = list_h();
    if (mode == M_BROWSE) {
        char head[240];
        text_copy(head, "Location: ", sizeof(head));
        text_append(head, path_text, sizeof(head));
        gui_text_bold(c, LIST_X, 14, head, theme->text, 1);
    } else {
        gui_rounded(c, LIST_X, 6, list_w, 32, 8, theme->background);
        gui_frame(c, LIST_X, 6, list_w, 32, 2, theme->accent);
        char line[96];
        text_copy(line, field_name, sizeof(line));
        text_append(line, ": ", sizeof(line));
        text_append(line, input, sizeof(line));
        gui_text(c, LIST_X + 10, 14, line, theme->text, 1);
        int cx = LIST_X + 10 + gui_text_width(line, 1);
        gui_fill(c, cx + 1, 12, 2, 16, theme->accent);
    }
    gui_rounded(c, LIST_X, LIST_Y, list_w, lh, 8, theme->background);
    if (mode == M_BROWSE && focus == LIST) gui_frame(c, LIST_X, LIST_Y, list_w, lh, 2, theme->accent);
    int rows = visible_rows();
    if (count == 0) gui_text(c, LIST_X + 14, LIST_Y + 14, "This folder is empty.", theme->text_dim, 1);
    for (int r = 0; r < rows && scroll + r < count; r++) {
        int idx = scroll + r;
        int id = ids[idx];
        int y = LIST_Y + 4 + r * ROW_H;
        if (idx == sel) gui_rounded(c, LIST_X + 4, y, list_w - 8, ROW_H - 2, 6, theme->selected);
        if (ramfs_is_dir(id)) draw_folder_icon(c, LIST_X + 12, y + 3); else draw_doc_icon(c, LIST_X + 12, y + 3);
        gui_text(c, LIST_X + 42, y + 5, ramfs_name(id), theme->text, 1);
        char detail[40];
        detail[0] = '\0';
        if (ramfs_is_dir(id)) {
            int k = ramfs_size(id);
            if (k == 0) text_copy(detail, "empty", sizeof(detail));
            else { text_append_number(detail, k, sizeof(detail)); text_append(detail, k == 1 ? " item" : " items", sizeof(detail)); }
        } else {
            text_append_number(detail, ramfs_size(id), sizeof(detail));
            text_append(detail, " bytes", sizeof(detail));
        }
        gui_text(c, LIST_X + list_w - 16 - gui_text_width(detail, 1), y + 5, detail, theme->text_dim, 1);
    }
    if (count > rows) {   // a thin scroll marker on the right
        int track = lh - 12;
        int bar = track * rows / count;
        if (bar < 12) bar = 12;
        int pos = (track - bar) * scroll / (count - rows);
        gui_fill(c, LIST_X + list_w - 6, LIST_Y + 6 + pos, 3, bar, theme->text_dim);
    }
    for (int i = 1; i < n; i++) draw_window_item(c, &items[i], i == focus, i == hovered_window_item);
}

// ---- Actions ----

static void go_to(int dir) {
    cur = dir;
    sel = 0; scroll = 0;
    delete_armed = false;
    refresh();
    say_folder();
}

static void go_up(void) {
    if (cur == ramfs_root()) { gui_announce("Already at the top."); return; }
    int old = cur;
    cur = ramfs_parent(cur);
    refresh();
    sel = 0;
    for (int i = 0; i < count; i++) if (ids[i] == old) sel = i;
    scroll = 0;
    delete_armed = false;
    keep_visible();
    say_folder();
}

static void open_selected(void) {
    if (count == 0) { gui_announce("This folder is empty."); return; }
    int id = ids[sel];
    if (ramfs_is_dir(id)) go_to(id);
    else notes_open_file(id);
}

static void start_naming(int new_mode) {
    if (new_mode == M_RENAME) {
        if (count == 0) { gui_announce("There is nothing to rename."); return; }
        rename_id = ids[sel];
        rename_uid = (int)ramfs_uid(rename_id);
        text_copy(input, ramfs_name(rename_id), sizeof(input));
    } else if (new_mode == M_NEW_FOLDER) {
        ramfs_unique_name(cur, "Untitled folder", "", input, sizeof(input));
    } else {
        ramfs_unique_name(cur, "Untitled", ".txt", input, sizeof(input));
    }
    input_length = text_length(input);
    input_fresh = true;
    mode = new_mode;
    delete_armed = false;
    windows[WIN_FILES].focus = 0;
    gui_line_start();
    gui_line_add(new_mode == M_RENAME ? "Rename. Type the new name, then press Enter. Escape cancels. It says " : "Type a name, then press Enter. Escape cancels. It says ");
    gui_line_add(input);
    gui_line_add(".");
    gui_line_say();
}

static void cancel_naming(void) {
    mode = M_BROWSE;
    windows[WIN_FILES].focus = 0;
    gui_announce("Cancelled.");
}

static void finish_naming(void) {
    int result;
    if (mode == M_RENAME) {
        if (!ramfs_valid(rename_id) || (int)ramfs_uid(rename_id) != rename_uid) { mode = M_BROWSE; gui_announce("That isn't there any more."); return; }
        result = ramfs_rename(rename_id, input);
        if (result == RAMFS_OK) {
            gui_line_start(); gui_line_add("Renamed to "); gui_line_add(input); gui_line_add("."); gui_line_say();
        }
    } else {
        result = ramfs_create(cur, input, mode == M_NEW_FOLDER);
        if (result >= 0) {
            if (mode == M_NEW_DOC) ramfs_write(result, "", 0);
            gui_line_start(); gui_line_add(mode == M_NEW_FOLDER ? "Folder " : "Document ");
            gui_line_add(input); gui_line_add(" created."); gui_line_say();
            result = RAMFS_OK;
        }
    }
    if (result != RAMFS_OK) { gui_announce(ramfs_error_words(result)); return; }   // stay, so the name can be fixed
    int made = ramfs_find(cur, input);
    mode = M_BROWSE;
    refresh();
    for (int i = 0; i < count; i++) if (ids[i] == made) sel = i;
    keep_visible();
    windows[WIN_FILES].focus = 0;
}

static void delete_selected(void) {
    if (count == 0) { gui_announce("There is nothing to delete."); return; }
    int id = ids[sel];
    if (!delete_armed) {
        delete_armed = true;
        char text[200];
        text_copy(text, "Press Delete again to delete ", sizeof(text));
        text_append(text, ramfs_name(id), sizeof(text));
        if (ramfs_is_dir(id) && ramfs_size(id) > 0) text_append(text, " and everything in it", sizeof(text));
        text_append(text, ".", sizeof(text));
        gui_announce(text);
        return;
    }
    char name[RAMFS_NAME_MAX];
    text_copy(name, ramfs_name(id), sizeof(name));
    notes_file_deleted(id);
    int result = ramfs_remove(id);
    delete_armed = false;
    if (result != RAMFS_OK) { gui_announce(ramfs_error_words(result)); return; }
    refresh();
    keep_visible();
    gui_line_start(); gui_line_add("Deleted "); gui_line_add(name); gui_line_add(". ");
    if (count == 0) gui_line_add("The folder is empty.");
    else { gui_line_add(ramfs_name(ids[sel])); gui_line_add(" is selected."); }
    gui_line_say();
}

static void select_index(int index, bool say) {
    if (count == 0) return;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    if (index != sel) delete_armed = false;
    sel = index;
    keep_visible();
    if (say) say_selection();
}

static bool key(struct areos_key_event *e, struct gui_item *items, int n, int *focus) {
    (void)items; (void)n;
    if (mode != M_BROWSE) {
        if (*focus != LIST) {
            if (e->key == AREOS_KEY_ESCAPE) { cancel_naming(); return true; }
            return false;
        }
        if (e->key == AREOS_KEY_ESCAPE) { cancel_naming(); return true; }
        if (e->key == AREOS_KEY_ENTER) { finish_naming(); return true; }
        if (e->key == AREOS_KEY_BACKSPACE) {
            if (input_fresh) { input[0] = '\0'; input_length = 0; input_fresh = false; gui_announce("Name cleared."); return true; }
            if (input_length > 0) { input[--input_length] = '\0'; gui_echo_backspace(); }
            return true;
        }
        char ch = e->character;
        if (ch >= 32 && ch <= 126 && ch != '/' && ch != '\\') {
            if (input_fresh) { input[0] = '\0'; input_length = 0; input_fresh = false; }
            if (input_length < RAMFS_NAME_MAX - 1) { input[input_length++] = ch; input[input_length] = '\0'; gui_echo_char(ch); }
            return true;
        }
        return false;
    }
    if (*focus != LIST) return false;
    switch (e->key) {
    case AREOS_KEY_UP: select_index(sel - 1, true); return true;
    case AREOS_KEY_DOWN: select_index(sel + 1, true); return true;
    case AREOS_KEY_HOME: select_index(0, true); return true;
    case AREOS_KEY_END: select_index(count - 1, true); return true;
    case AREOS_KEY_PAGE_UP: select_index(sel - visible_rows(), true); return true;
    case AREOS_KEY_PAGE_DOWN: select_index(sel + visible_rows(), true); return true;
    case AREOS_KEY_LEFT: case AREOS_KEY_BACKSPACE: go_up(); return true;
    case AREOS_KEY_RIGHT: if (count > 0 && ramfs_is_dir(ids[sel])) open_selected(); return true;
    case AREOS_KEY_DELETE: delete_selected(); return true;
    case AREOS_KEY_F1 + 3: start_naming(M_RENAME); return true;   // F4 (F2 is "where am I" everywhere)
    default: break;
    }
    char ch = e->character;
    if (ch > 32 && ch <= 126 && count > 0 && !alt_held) {
        // type-ahead: the next name (after this one) that starts with this letter
        for (int k = 1; k <= count; k++) {
            int i = (sel + k) % count;
            if (to_lower(ramfs_name(ids[i])[0]) == to_lower(ch)) { select_index(i, true); return true; }
        }
        gui_announce("No match.");
        return true;
    }
    return false;
}

static void choose(int index) {
    if (index != B_DELETE) delete_armed = false;
    if (mode != M_BROWSE) {
        if (index == B_OK) finish_naming();
        else if (index == B_CANCEL) cancel_naming();
        return;
    }
    switch (index) {
    case LIST:
        if (click_y >= 0) {                       // a mouse click: choose the row it landed on
            int row = (click_y - LIST_Y - 4) / ROW_H + scroll;
            if (click_y >= LIST_Y + 4 && row >= 0 && row < count) {
                if (row == sel) open_selected(); else select_index(row, true);
            }
        } else open_selected();                   // Enter
        break;
    case B_UP: go_up(); break;
    case B_OPEN: open_selected(); break;
    case B_NEW_FOLDER: start_naming(M_NEW_FOLDER); break;
    case B_NEW_DOC: start_naming(M_NEW_DOC); break;
    case B_RENAME: start_naming(M_RENAME); break;
    case B_DELETE: delete_selected(); break;
    }
    request_redraw();
}

static void opened(void) {
    refresh();
    gui_announce("Files. Up and Down choose, Enter opens, Backspace goes up a folder. F4 renames, Delete deletes. Tab moves to the buttons.");
    say_folder();
}

const struct window_app app_files = {
    .title = "Files", .icon = ICON_FILES, .width = 860, .height = 470, .resizable = true,
    .build = build, .draw = draw, .key = key, .choose = choose, .opened = opened,
};
