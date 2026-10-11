// desktop.c - the AREOS desktop, and the setup screens you see first.
//
// How it's organised:
//   - The app is always on one "screen": one of the setup pages, or the
//     desktop itself (which can have a window open on top).
//   - Each time anything changes, the current screen is laid out from
//     scratch into a list of items (buttons, radio buttons, ...), then
//     drawn. Keyboard, mouse and screen-reader announcements all work
//     from that same list, so they can never disagree about what's there.
//
// Keys: Tab / arrows move, Enter or Space choose (on setup pages Enter also
// moves to the next page), Escape goes back,
// F1 explains the keys, F2 says where you are. Letters jump to items.

#include "areos.h"
#include "gui.h"

// ---- What can happen when an item is chosen ----

enum action {
    ACT_NONE,
    ACT_NEXT, ACT_BACK,
    ACT_NAME_FIELD,
    ACT_ENGLISH, ACT_SPANISH,
    ACT_DARK, ACT_LIGHT,
    ACT_DETAILED, ACT_BRIEF,
    ACT_FINISH,
    ACT_SETTINGS, ACT_ABOUT, ACT_TERMINAL, ACT_POWER_OFF,
    ACT_CLOSE_WINDOW, ACT_RUN_SETUP,
    ACT_APP_FIRST = 100,   // ACT_APP_FIRST + n opens app number n
};

// ---- Screens ----

enum screen {
    SCREEN_WELCOME, SCREEN_NAME, SCREEN_LANGUAGE, SCREEN_APPEARANCE,
    SCREEN_ACCESSIBILITY, SCREEN_DONE,
    SCREEN_DESKTOP,
};
#define SETUP_STEPS 6

enum window { WINDOW_NONE, WINDOW_SETTINGS, WINDOW_ABOUT };

// ---- State ----

#define MAX_ITEMS 40
#define MAX_APPS  24
#define NAME_MAX  32
#define STAR_COUNT 140

static struct gui_canvas canvas;
static const struct gui_theme *theme = &gui_theme_dark;
static bool detailed = true;              // announcement style

static enum screen screen = SCREEN_WELCOME;
static enum window window = WINDOW_NONE;
static int window_x, window_y;            // top-left of the open window
static bool window_placed = false;        // has it been positioned yet?

static struct gui_item items[MAX_ITEMS];
static int item_count = 0;
static int focus = 0;                     // which item has keyboard focus
static int hovered = -1;                  // which item the mouse is over
static int focus_before_window = 0;       // the tile to return to when a window closes

static char name[NAME_MAX + 1];
static char app_names[MAX_APPS][40];      // "hello.arx"
static char app_labels[MAX_APPS][40];     // "hello"
static int app_count = 0;

static int mouse_x, mouse_y;
static uint8_t mouse_buttons = 0;
static bool dragging = false;
static int drag_offset_x, drag_offset_y;
static bool shift_held = false;

static int star_x[STAR_COUNT], star_y[STAR_COUNT];
static bool dirty = true;                 // something changed: redraw
static bool running = true;

// ---- Small text helpers ----

static int text_length(const char *s) {
    int n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static void text_copy(char *to, const char *from, int max) {
    int i = 0;
    for (; from[i] != '\0' && i < max - 1; i++) to[i] = from[i];
    to[i] = '\0';
}

static bool text_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] != '\0' && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Put a two-digit number into text, like "07".
static void two_digits(char *out, int n) {
    out[0] = (char)('0' + (n / 10) % 10);
    out[1] = (char)('0' + n % 10);
}

// ---- Settings ----

static void load_settings(void) {
    theme = areos_setting_get(AREOS_SETTING_THEME) == 1 ? &gui_theme_light : &gui_theme_dark;
    detailed = areos_setting_get(AREOS_SETTING_VERBOSITY) == 0;
    for (int i = 0; i < 4; i++) {
        uint64_t chunk = areos_setting_get(AREOS_SETTING_NAME_FIRST + i);
        for (int b = 0; b < 8; b++) {
            name[i * 8 + b] = (char)((chunk >> (b * 8)) & 0xFF);
        }
    }
    name[NAME_MAX] = '\0';
}

static void save_name(void) {
    for (int i = 0; i < 4; i++) {
        uint64_t chunk = 0;
        for (int b = 0; b < 8; b++) {
            chunk |= (uint64_t)(uint8_t)name[i * 8 + b] << (b * 8);
        }
        areos_setting_set(AREOS_SETTING_NAME_FIRST + i, chunk);
    }
}

static void set_theme(bool light) {
    theme = light ? &gui_theme_light : &gui_theme_dark;
    areos_setting_set(AREOS_SETTING_THEME, light ? 1 : 0);
    dirty = true;
}

static void set_detailed(bool on) {
    detailed = on;
    areos_setting_set(AREOS_SETTING_VERBOSITY, on ? 0 : 1);
}

// ---- Apps ----

static void load_apps(void) {
    char buffer[40];
    app_count = 0;
    for (int i = 0; app_count < MAX_APPS && areos_app_name(i, buffer, sizeof(buffer)); i++) {
        if (text_equal(buffer, "desktop.arx")) {
            continue;  // that's us
        }
        text_copy(app_names[app_count], buffer, 40);
        text_copy(app_labels[app_count], buffer, 40);
        int length = text_length(app_labels[app_count]);
        if (length > 4 && text_equal(app_labels[app_count] + length - 4, ".arx")) {
            app_labels[app_count][length - 4] = '\0';
        }
        app_labels[app_count][0] = (char)(app_labels[app_count][0] >= 'a' && app_labels[app_count][0] <= 'z'
                                    ? app_labels[app_count][0] - 'a' + 'A' : app_labels[app_count][0]);
        app_count++;
    }
}

// ---- Building the item list for the current screen ----

static struct gui_item *add_item(const char *label, enum gui_role role, int action,
                                 int x, int y, int w, int h) {
    if (item_count >= MAX_ITEMS) {
        return &items[MAX_ITEMS - 1];
    }
    struct gui_item *item = &items[item_count++];
    item->name = label;
    item->role = role;
    item->action = action;
    item->checked = false;
    item->disabled = false;
    item->value = NULL;
    item->x = x; item->y = y; item->w = w; item->h = h;
    return item;
}

// The card that setup pages sit on.
#define CARD_W 860
#define CARD_H 600
static int card_x(void) { return (canvas.width - CARD_W) / 2; }
static int card_y(void) { return (canvas.height - CARD_H) / 2; }

#define BUTTON_W 260
#define BUTTON_H 56
#define CHOICE_H 52

// Back / Next along the bottom of the setup card.
static void add_back_next(const char *next_label, int next_action, bool with_back) {
    int y = card_y() + CARD_H - BUTTON_H - 36;
    if (with_back) {
        add_item("Back", GUI_ROLE_BUTTON, ACT_BACK, card_x() + 48, y, BUTTON_W, BUTTON_H);
    }
    add_item(next_label, GUI_ROLE_BUTTON, next_action, card_x() + CARD_W - 48 - BUTTON_W, y, BUTTON_W, BUTTON_H);
}

static void add_choice(const char *label, int action, bool checked, int index) {
    struct gui_item *item = add_item(label, GUI_ROLE_RADIO, action,
                                     card_x() + 48, card_y() + 300 + index * (CHOICE_H + 14),
                                     CARD_W - 96, CHOICE_H);
    item->checked = checked;
}

#define TOP_BAR_H 64
#define TILE_W 200
#define TILE_H 150
#define TILE_GAP 28

// Lay out the desktop's tiles: apps first, then the system ones.
static void build_desktop_items(void) {
    static const char *system_labels[] = { "Settings", "About", "Terminal", "Turn off" };
    static const int system_actions[] = { ACT_SETTINGS, ACT_ABOUT, ACT_TERMINAL, ACT_POWER_OFF };
    int columns = (canvas.width - 2 * 60 + TILE_GAP) / (TILE_W + TILE_GAP);
    if (columns < 1) columns = 1;
    int total = app_count + 4;
    for (int i = 0; i < total; i++) {
        int column = i % columns, row = i / columns;
        int x = 60 + column * (TILE_W + TILE_GAP);
        int y = TOP_BAR_H + 90 + row * (TILE_H + TILE_GAP);
        if (i < app_count) {
            add_item(app_labels[i], GUI_ROLE_APP, ACT_APP_FIRST + i, x, y, TILE_W, TILE_H);
        } else {
            add_item(system_labels[i - app_count], GUI_ROLE_BUTTON, system_actions[i - app_count],
                     x, y, TILE_W, TILE_H);
        }
    }
}

#define WINDOW_W 720
#define WINDOW_H 560
#define TITLE_H 56

static void build_window_items(void) {
    if (!window_placed) {
        window_x = (canvas.width - WINDOW_W) / 2;
        window_y = (canvas.height - WINDOW_H) / 2 + 20;
        window_placed = true;
    }
    int x = window_x + 40, w = WINDOW_W - 80;
    int y = window_y + TITLE_H + 70;
    if (window == WINDOW_SETTINGS) {
        add_item("Dark mode", GUI_ROLE_RADIO, ACT_DARK, x, y, w, CHOICE_H)->checked = theme == &gui_theme_dark;
        add_item("Light mode", GUI_ROLE_RADIO, ACT_LIGHT, x, y + 62, w, CHOICE_H)->checked = theme == &gui_theme_light;
        add_item("Detailed descriptions", GUI_ROLE_RADIO, ACT_DETAILED, x, y + 180, w, CHOICE_H)->checked = detailed;
        add_item("Brief descriptions", GUI_ROLE_RADIO, ACT_BRIEF, x, y + 242, w, CHOICE_H)->checked = !detailed;
        add_item("Run setup again", GUI_ROLE_BUTTON, ACT_RUN_SETUP, x, window_y + WINDOW_H - BUTTON_H - 28,
                 BUTTON_W + 20, BUTTON_H);
    }
    add_item("Close", GUI_ROLE_BUTTON, ACT_CLOSE_WINDOW,
             window_x + WINDOW_W - 40 - 180, window_y + WINDOW_H - BUTTON_H - 28, 180, BUTTON_H);
}

static void build_items(void) {
    item_count = 0;
    int field_y = card_y() + 300;
    switch (screen) {
    case SCREEN_WELCOME:
        add_back_next("Get started", ACT_NEXT, false);
        break;
    case SCREEN_NAME: {
        struct gui_item *field = add_item("Your name", GUI_ROLE_TEXT_FIELD, ACT_NAME_FIELD,
                                          card_x() + 48, field_y, CARD_W - 96, 64);
        field->value = name;
        add_back_next("Next", ACT_NEXT, true);
        break;
    }
    case SCREEN_LANGUAGE:
        add_choice("English", ACT_ENGLISH, true, 0);
        add_choice("Spanish (coming soon)", ACT_SPANISH, false, 1);
        items[item_count - 1].disabled = true;
        add_back_next("Next", ACT_NEXT, true);
        break;
    case SCREEN_APPEARANCE:
        add_choice("Dark mode", ACT_DARK, theme == &gui_theme_dark, 0);
        add_choice("Light mode", ACT_LIGHT, theme == &gui_theme_light, 1);
        add_back_next("Next", ACT_NEXT, true);
        break;
    case SCREEN_ACCESSIBILITY:
        add_choice("Detailed: names, positions and hints", ACT_DETAILED, detailed, 0);
        add_choice("Brief: just names", ACT_BRIEF, !detailed, 1);
        add_back_next("Next", ACT_NEXT, true);
        break;
    case SCREEN_DONE:
        add_back_next("Start using AREOS", ACT_FINISH, true);
        break;
    case SCREEN_DESKTOP:
        if (window != WINDOW_NONE) {
            build_window_items();  // a window takes all the focus while it's open
        } else {
            build_desktop_items();
        }
        break;
    }
    if (focus >= item_count) {
        focus = item_count - 1;
    }
    if (focus < 0) {
        focus = 0;
    }
}

// ---- Words for each screen ----

static const char *screen_title(void) {
    switch (screen) {
    case SCREEN_WELCOME:       return "Welcome to AREOS";
    case SCREEN_NAME:          return "What should we call you?";
    case SCREEN_LANGUAGE:      return "Choose your language";
    case SCREEN_APPEARANCE:    return "Pick a look";
    case SCREEN_ACCESSIBILITY: return "How much should AREOS describe?";
    case SCREEN_DONE:          return "You're all set";
    case SCREEN_DESKTOP:
        if (window == WINDOW_SETTINGS) return "Settings";
        if (window == WINDOW_ABOUT) return "About AREOS";
        return "Desktop";
    }
    return "";
}

static const char *screen_body(void) {
    switch (screen) {
    case SCREEN_WELCOME:
        return "It can run Doom. That's it. Let's set a few things up first; it takes about a minute. "
               "Everything here is also described in text, for screen readers.";
    case SCREEN_NAME:
        return "This is just for greetings. Type your name, or leave it blank.";
    case SCREEN_LANGUAGE:
        return "More languages are on the way.";
    case SCREEN_APPEARANCE:
        return "We've defaulted to dark mode, because bright screens can be hard on the eyes. "
               "You can change this anytime in Settings.";
    case SCREEN_ACCESSIBILITY:
        return "AREOS describes what's on screen in text, so screen readers like NVDA can follow along. "
               "Choose how much it says.";
    case SCREEN_DONE:
        return "The fourth OS is ready. Press Start to go to your desktop.";
    case SCREEN_DESKTOP:
        if (window == WINDOW_SETTINGS) return "Change how AREOS looks and how much it describes.";
        if (window == WINDOW_ABOUT)
            return "AREOS v0.1, a hobby operating system built from scratch. Not Linux, not Windows, "
                   "not macOS: its own thing. No ads, no bloat, no telemetry. It can run Doom. That's it.";
        return "Your apps and the system tiles.";
    }
    return "";
}

// ---- The greeting and clock, as text ----

static const char *month_names[] = { "January", "February", "March", "April", "May", "June", "July",
                                     "August", "September", "October", "November", "December" };

static void format_time(char *out, struct areos_date_time now) {
    int hour12 = now.hour % 12 == 0 ? 12 : now.hour % 12;
    int i = 0;
    if (hour12 >= 10) out[i++] = '1';
    out[i++] = (char)('0' + hour12 % 10);
    out[i++] = ':';
    two_digits(out + i, now.minute);
    i += 2;
    out[i++] = ' ';
    out[i++] = now.hour < 12 ? 'A' : 'P';
    out[i++] = 'M';
    out[i] = '\0';
}

static void format_greeting(char *out, int size, struct areos_date_time now) {
    const char *part = now.hour < 12 ? "Good morning" : (now.hour < 17 ? "Good afternoon" : "Good evening");
    text_copy(out, part, size);
    if (name[0] != '\0') {
        int length = text_length(out);
        out[length] = ',';
        out[length + 1] = ' ';
        text_copy(out + length + 2, name, size - length - 2);
    }
}

// "Good morning, Gavin. It's 10:42 AM on October 11."
static const char *desktop_status(void) {
    static char status[128];
    struct areos_date_time now = areos_date_time();
    char time[12];
    format_greeting(status, 64, now);
    format_time(time, now);
    int length = text_length(status);
    text_copy(status + length, ". It's ", (int)sizeof(status) - length);
    length = text_length(status);
    text_copy(status + length, time, (int)sizeof(status) - length);
    length = text_length(status);
    text_copy(status + length, " on ", (int)sizeof(status) - length);
    length = text_length(status);
    const char *month = month_names[(now.month >= 1 && now.month <= 12) ? now.month - 1 : 0];
    text_copy(status + length, month, (int)sizeof(status) - length);
    length = text_length(status);
    status[length++] = ' ';
    if (now.day >= 10) status[length++] = (char)('0' + now.day / 10);
    status[length++] = (char)('0' + now.day % 10);
    status[length++] = '.';
    status[length] = '\0';
    return status;
}

// ---- Announcing (for screen readers) ----

static void announce_focus(void) {
    if (item_count > 0) {
        gui_announce_item(&items[focus], focus + 1, item_count, detailed);
    }
}

// Say where we are: the screen (and setup step), what it's about, then the focused item.
static void announce_screen(void) {
    gui_line_start();
    if (screen < SCREEN_DESKTOP) {
        gui_line_add("Setup, step ");
        gui_line_add_number((int)screen + 1);
        gui_line_add(" of ");
        gui_line_add_number(SETUP_STEPS);
        gui_line_add(": ");
    } else if (window != WINDOW_NONE) {
        gui_line_add("Window: ");
    }
    const char *title = screen_title();
    gui_line_add(title);
    char last = title[text_length(title) - 1];
    gui_line_add((last == '?' || last == '!' || last == '.') ? " " : ". ");
    gui_line_add(screen_body());
    if (screen == SCREEN_DESKTOP && window == WINDOW_NONE) {
        gui_line_add(" ");
        gui_line_add(desktop_status());
    }
    gui_line_say();
    if (detailed && screen == SCREEN_WELCOME) {
        gui_announce("Tip: press F1 any time to hear the keys.");
    }
    announce_focus();
}

static void announce_help(void) {
    gui_announce("Keys: Tab or the arrow keys move between items. Shift+Tab moves back. "
                 "Space chooses. Enter chooses and, during setup, moves to the next page. "
                 "Escape goes back or closes a window. "
                 "F2 says where you are. Typing a letter jumps to the next item starting with it. "
                 "The mouse works too: anything it points at is described.");
}

static void go_to(enum screen next) {
    screen = next;
    focus = 0;
    hovered = -1;
    build_items();
    // On setup pages, start on the main choice rather than "Back".
    for (int i = 0; i < item_count; i++) {
        if (items[i].role != GUI_ROLE_BUTTON || items[i].action != ACT_BACK) {
            focus = i;
            break;
        }
    }
    dirty = true;
    announce_screen();
}

static void open_window(enum window which) {
    focus_before_window = focus;
    window = which;
    window_placed = false;
    focus = 0;
    hovered = -1;
    build_items();
    dirty = true;
    announce_screen();
}

static void close_window(void) {
    window = WINDOW_NONE;
    focus = focus_before_window;
    hovered = -1;
    build_items();
    dirty = true;
    announce_screen();
}

// ---- Choosing things ----

static bool quiet_choice = false;  // set when the page is about to change anyway

static void choose(int index) {
    if (index < 0 || index >= item_count) {
        return;
    }
    struct gui_item *item = &items[index];
    focus = index;
    if (item->disabled) {
        gui_line_start();
        gui_line_add(item->name);
        gui_line_add(" isn't available yet.");
        gui_line_say();
        return;
    }
    switch (item->action) {
    case ACT_NEXT:
        go_to((enum screen)(screen + 1));
        return;
    case ACT_BACK:
        go_to((enum screen)(screen - 1));
        return;
    case ACT_NAME_FIELD:
        go_to((enum screen)(screen + 1));  // Enter in the name field moves on
        return;
    case ACT_ENGLISH:
        break;
    case ACT_DARK:
    case ACT_LIGHT:
        set_theme(item->action == ACT_LIGHT);
        break;
    case ACT_DETAILED:
    case ACT_BRIEF:
        set_detailed(item->action == ACT_DETAILED);
        break;
    case ACT_FINISH:
        save_name();
        areos_setting_set(AREOS_SETTING_SETUP_DONE, 1);
        go_to(SCREEN_DESKTOP);
        return;
    case ACT_SETTINGS:
        open_window(WINDOW_SETTINGS);
        return;
    case ACT_ABOUT:
        open_window(WINDOW_ABOUT);
        return;
    case ACT_CLOSE_WINDOW:
        close_window();
        return;
    case ACT_RUN_SETUP:
        window = WINDOW_NONE;
        go_to(SCREEN_WELCOME);
        return;
    case ACT_TERMINAL:
        gui_announce("Leaving the desktop for the command line. Type desktop to come back.");
        running = false;
        return;
    case ACT_POWER_OFF:
        gui_announce("Turning off. Goodbye!");
        areos_power_off();
        return;
    default:
        if (item->action >= ACT_APP_FIRST && item->action < ACT_APP_FIRST + app_count) {
            int app = item->action - ACT_APP_FIRST;
            gui_line_start();
            gui_line_add("Opening ");
            gui_line_add(app_labels[app]);
            gui_line_add(". The desktop comes back when it finishes.");
            gui_line_say();
            areos_launch(app_names[app], true);
            running = false;
        }
        return;
    }
    // A radio button changed: rebuild so its "selected" state updates, then say it.
    build_items();
    dirty = true;
    if (!quiet_choice) {
        announce_focus();
    }
}

static void move_focus(int step) {
    if (item_count == 0) return;
    focus = (focus + step + item_count) % item_count;
    dirty = true;
    announce_focus();
}

// Jump to the next item whose name starts with this letter.
static void jump_to_letter(char letter) {
    for (int step = 1; step <= item_count; step++) {
        int i = (focus + step) % item_count;
        if (lower(items[i].name[0]) == lower(letter)) {
            focus = i;
            dirty = true;
            announce_focus();
            return;
        }
    }
}

static bool focused_is_text_field(void) {
    return item_count > 0 && items[focus].role == GUI_ROLE_TEXT_FIELD;
}

static void handle_key(struct areos_key_event *e) {
    if (e->key == AREOS_KEY_SHIFT) {
        shift_held = e->pressed;
        return;
    }
    if (!e->pressed) {
        return;
    }

    // Typing into the name field.
    if (focused_is_text_field()) {
        int length = text_length(name);
        if (e->key == AREOS_KEY_BACKSPACE) {
            if (length > 0) {
                name[length - 1] = '\0';
                gui_echo_backspace();
                dirty = true;
            }
            return;
        }
        char c = e->character;
        if (c >= 32 && c <= 126 && c != '\t') {
            if (length < NAME_MAX) {
                name[length] = c;
                name[length + 1] = '\0';
                gui_echo_char(c);
                dirty = true;
            }
            return;
        }
    }

    switch (e->key) {
    case AREOS_KEY_TAB:
        move_focus(shift_held ? -1 : 1);
        break;
    case AREOS_KEY_DOWN:
    case AREOS_KEY_RIGHT:
        move_focus(1);
        break;
    case AREOS_KEY_UP:
    case AREOS_KEY_LEFT:
        move_focus(-1);
        break;
    case AREOS_KEY_ENTER:
        // On setup pages, Enter on a choice picks it and moves on, so the
        // defaults are just "Enter, Enter, Enter".
        if (screen < SCREEN_DESKTOP && item_count > 0 && items[focus].role == GUI_ROLE_RADIO) {
            bool available = !items[focus].disabled;
            quiet_choice = available;
            choose(focus);
            quiet_choice = false;
            if (available) {
                go_to((enum screen)(screen + 1));
            }
        } else {
            choose(focus);
        }
        break;
    case AREOS_KEY_SPACE:
        choose(focus);  // Space picks without moving on (handy for previewing)
        break;
    case AREOS_KEY_ESCAPE:
        if (screen == SCREEN_DESKTOP && window != WINDOW_NONE) {
            close_window();
        } else if (screen > SCREEN_WELCOME && screen < SCREEN_DESKTOP) {
            go_to((enum screen)(screen - 1));
        }
        break;
    case AREOS_KEY_F1:
        announce_help();
        break;
    case AREOS_KEY_F1 + 1:
        announce_screen();
        break;
    default:
        if (e->character != 0 && e->character != ' ') {
            jump_to_letter(e->character);
        }
        break;
    }
}

// ---- Mouse ----

static int item_at(int x, int y) {
    for (int i = 0; i < item_count; i++) {
        if (x >= items[i].x && x < items[i].x + items[i].w && y >= items[i].y && y < items[i].y + items[i].h) {
            return i;
        }
    }
    return -1;
}

static bool on_window_title(int x, int y) {
    return screen == SCREEN_DESKTOP && window != WINDOW_NONE
        && x >= window_x && x < window_x + WINDOW_W && y >= window_y && y < window_y + TITLE_H;
}

static void handle_mouse(struct areos_mouse_event *e) {
    mouse_x += e->dx;
    mouse_y += e->dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= canvas.width) mouse_x = canvas.width - 1;
    if (mouse_y >= canvas.height) mouse_y = canvas.height - 1;
    dirty = true;

    bool left_now = (e->buttons & AREOS_MOUSE_LEFT) != 0;
    bool left_before = (mouse_buttons & AREOS_MOUSE_LEFT) != 0;
    mouse_buttons = e->buttons;

    if (dragging) {
        if (!left_now) {
            dragging = false;
        } else {
            window_x = mouse_x - drag_offset_x;
            window_y = mouse_y - drag_offset_y;
            build_items();
        }
        return;
    }

    // Describe whatever the mouse is now pointing at.
    int under = item_at(mouse_x, mouse_y);
    if (under != hovered) {
        hovered = under;
        if (under >= 0) {
            gui_line_start();
            gui_line_add("Mouse: ");
            gui_line_add(items[under].name);
            gui_line_say();
        }
    }

    if (left_now && !left_before) {  // a click
        if (on_window_title(mouse_x, mouse_y)) {
            dragging = true;
            drag_offset_x = mouse_x - window_x;
            drag_offset_y = mouse_y - window_y;
        } else if (under >= 0) {
            focus = under;
            if (items[under].role == GUI_ROLE_TEXT_FIELD) {
                announce_focus();
            } else {
                choose(under);
            }
        }
    }
}

// ---- Drawing ----

static void draw_background(void) {
    gui_fill(&canvas, 0, 0, canvas.width, canvas.height, theme->background);
    for (int i = 0; i < STAR_COUNT; i++) {
        int size = (i % 7 == 0) ? 3 : 2;
        gui_fill(&canvas, star_x[i], star_y[i], size, size, theme->stars);
    }
}

static void draw_radio(const struct gui_item *item, int x, int y) {
    gui_frame(&canvas, x, y, 28, 28, 3, theme->accent2);
    if (item->checked) {
        gui_fill(&canvas, x + 8, y + 8, 12, 12, theme->accent);
    }
}

static void draw_item(int index) {
    const struct gui_item *item = &items[index];
    bool focused = index == focus;
    bool under_mouse = index == hovered;
    uint32_t fill = focused ? theme->selected : (under_mouse ? theme->hover : theme->panel);
    uint32_t text_color = item->disabled ? theme->text_dim : theme->text;

    gui_fill(&canvas, item->x, item->y, item->w, item->h, fill);
    gui_frame(&canvas, item->x, item->y, item->w, item->h, focused ? 4 : 2,
              focused ? theme->accent : theme->panel_border);

    int text_y = item->y + (item->h - gui_text_height(2)) / 2;
    switch (item->role) {
    case GUI_ROLE_BUTTON:
        if (item->h > 100) {  // a desktop tile
            gui_text_centered(&canvas, item->x + item->w / 2, item->y + 22, (char[]){ item->name[0], 0 },
                              theme->accent2, 5);
            gui_text_centered(&canvas, item->x + item->w / 2, item->y + item->h - 48, item->name, text_color, 2);
        } else {
            gui_text_centered(&canvas, item->x + item->w / 2, text_y, item->name, text_color, 2);
        }
        break;
    case GUI_ROLE_APP:
        gui_text_centered(&canvas, item->x + item->w / 2, item->y + 22, (char[]){ item->name[0], 0 },
                          theme->accent, 5);
        gui_text_centered(&canvas, item->x + item->w / 2, item->y + item->h - 48, item->name, text_color, 2);
        break;
    case GUI_ROLE_RADIO:
        draw_radio(item, item->x + 16, item->y + (item->h - 28) / 2);
        gui_text(&canvas, item->x + 64, text_y, item->name, text_color, 2);
        break;
    case GUI_ROLE_TEXT_FIELD: {
        const char *shown = (item->value != NULL && item->value[0] != '\0') ? item->value : "";
        gui_text(&canvas, item->x + 20, text_y, shown, theme->text, 2);
        if (shown[0] == '\0' && !focused) {
            gui_text(&canvas, item->x + 20, text_y, "Your name", theme->text_dim, 2);
        }
        if (focused) {  // the typing cursor
            gui_fill(&canvas, item->x + 20 + gui_text_width(shown, 2) + 2, text_y + 4, 4,
                     gui_text_height(2) - 8, theme->accent);
        }
        break;
    }
    }
}

static void draw_setup_card(void) {
    int x = card_x(), y = card_y();
    gui_fill(&canvas, x, y, CARD_W, CARD_H, theme->panel);
    gui_frame(&canvas, x, y, CARD_W, CARD_H, 2, theme->panel_border);

    // "Step 3 of 6" and a progress bar.
    char step[] = "Step 0 of 6";
    step[5] = (char)('0' + (int)screen + 1);
    gui_text(&canvas, x + 48, y + 36, step, theme->text_dim, 2);
    int bar_w = CARD_W - 96 - gui_text_width(step, 2) - 32;
    int bar_x = x + 48 + gui_text_width(step, 2) + 32;
    gui_fill(&canvas, bar_x, y + 46, bar_w, 8, theme->panel_border);
    gui_fill(&canvas, bar_x, y + 46, bar_w * ((int)screen + 1) / SETUP_STEPS, 8, theme->accent);

    gui_text(&canvas, x + 48, y + 92, screen_title(), theme->accent, 4);
    gui_text_wrapped(&canvas, x + 48, y + 170, CARD_W - 96, screen_body(), theme->text, 2);

    if (screen == SCREEN_DONE && name[0] != '\0') {
        char greeting[64] = "Welcome aboard, ";
        int length = text_length(greeting);
        text_copy(greeting + length, name, (int)sizeof(greeting) - length - 1);
        length = text_length(greeting);
        greeting[length] = '.';
        greeting[length + 1] = '\0';
        gui_text(&canvas, x + 48, y + 300, greeting, theme->accent2, 3);
    }
}

static void draw_top_bar(void) {
    gui_fill(&canvas, 0, 0, canvas.width, TOP_BAR_H, theme->panel);
    gui_fill(&canvas, 0, TOP_BAR_H - 2, canvas.width, 2, theme->panel_border);
    gui_text(&canvas, 28, 16, "AREOS", theme->accent, 2);

    struct areos_date_time now = areos_date_time();

    // Greeting in the middle.
    char greeting[64];
    format_greeting(greeting, (int)sizeof(greeting), now);
    gui_text_centered(&canvas, canvas.width / 2, 16, greeting, theme->text, 2);

    // Clock on the right: "Oct 11  10:32 AM".
    char clock[24];
    const char *month = month_names[(now.month >= 1 && now.month <= 12) ? now.month - 1 : 0];
    clock[0] = month[0]; clock[1] = month[1]; clock[2] = month[2]; clock[3] = ' ';
    int i = 4;
    if (now.day >= 10) clock[i++] = (char)('0' + now.day / 10);
    clock[i++] = (char)('0' + now.day % 10);
    clock[i++] = ' ';
    clock[i++] = ' ';
    format_time(clock + i, now);
    gui_text(&canvas, canvas.width - 28 - gui_text_width(clock, 2), 16, clock, theme->text, 2);
}

static void draw_window(void) {
    // A shadow, the window, its title bar.
    gui_fill(&canvas, window_x + 10, window_y + 10, WINDOW_W, WINDOW_H, theme->panel_border);
    gui_fill(&canvas, window_x, window_y, WINDOW_W, WINDOW_H, theme->panel);
    gui_frame(&canvas, window_x, window_y, WINDOW_W, WINDOW_H, 2, theme->accent2);
    gui_fill(&canvas, window_x, window_y, WINDOW_W, TITLE_H, theme->hover);
    gui_text(&canvas, window_x + 24, window_y + 12, screen_title(), theme->accent, 2);

    int x = window_x + 40, w = WINDOW_W - 80;
    if (window == WINDOW_SETTINGS) {
        gui_text(&canvas, x, window_y + TITLE_H + 26, "Look", theme->text_dim, 2);
        gui_text(&canvas, x, window_y + TITLE_H + 206, "Descriptions", theme->text_dim, 2);
    } else {
        gui_text_wrapped(&canvas, x, window_y + TITLE_H + 30, w, screen_body(), theme->text, 2);
    }
}

// The arrow pointer, drawn as rows of pixels: '#' outline, '.' fill.
static const char *pointer[] = {
    "#", "##", "#.#", "#..#", "#...#", "#....#", "#.....#", "#......#", "#.......#",
    "#........#", "#.....####", "#..#..#", "#.# #..#", "##  #..#", "#    #..#", "      ##",
};

static void draw_pointer(void) {
    for (int row = 0; row < 16; row++) {
        for (int col = 0; pointer[row][col] != '\0'; col++) {
            char p = pointer[row][col];
            if (p == '#' || p == '.') {
                uint32_t color = p == '#' ? 0x000000 : 0xFFFFFF;
                gui_fill(&canvas, mouse_x + col * 2, mouse_y + row * 2, 2, 2, color);
            }
        }
    }
}

static void draw(void) {
    draw_background();
    if (screen < SCREEN_DESKTOP) {
        draw_setup_card();
    } else {
        draw_top_bar();
        gui_text(&canvas, 60, TOP_BAR_H + 36, "Apps", theme->text_dim, 2);
        if (window != WINDOW_NONE) {
            // Draw the tiles underneath (not focusable while the window is open).
            int saved_count = item_count, saved_focus = focus;
            struct gui_item saved[MAX_ITEMS];
            for (int i = 0; i < item_count; i++) saved[i] = items[i];
            item_count = 0;
            build_desktop_items();
            focus = -1;
            int keep_hover = hovered;
            hovered = -1;
            for (int i = 0; i < item_count; i++) draw_item(i);
            hovered = keep_hover;
            for (int i = 0; i < saved_count; i++) items[i] = saved[i];
            item_count = saved_count;
            focus = saved_focus;
            draw_window();
        }
        gui_text(&canvas, 28, canvas.height - 40,
                 "Tab/arrows: move   Enter: open   Esc: close   F1: keys   F2: where am I",
                 theme->text_dim, 2);
    }
    for (int i = 0; i < item_count; i++) {
        draw_item(i);
    }
    draw_pointer();
    areos_graphics_draw(canvas.pixels, (uint32_t)canvas.width, (uint32_t)canvas.height);
}

// ---- Main ----

int main(void) {
    uint32_t width, height;
    if (!areos_graphics_start(&width, &height)) {
        areos_print("The desktop couldn't switch to graphics mode.\n");
        return 1;
    }
    canvas.width = (int)width;
    canvas.height = (int)height;
    canvas.pixels = areos_more_memory((size_t)width * height * sizeof(uint32_t));
    if (canvas.pixels == NULL) {
        areos_graphics_stop();
        areos_print("Not enough memory for the desktop.\n");
        return 1;
    }

    // Scatter the background stars (the same pattern every time).
    uint32_t seed = 4242;
    for (int i = 0; i < STAR_COUNT; i++) {
        seed = seed * 1103515245 + 12345;
        star_x[i] = (int)((seed >> 8) % width);
        seed = seed * 1103515245 + 12345;
        star_y[i] = (int)((seed >> 8) % height);
    }
    mouse_x = canvas.width / 2;
    mouse_y = canvas.height / 2;

    load_settings();
    load_apps();
    go_to(areos_setting_get(AREOS_SETTING_SETUP_DONE) ? SCREEN_DESKTOP : SCREEN_WELCOME);

    int last_minute = -1;
    while (running) {
        struct areos_key_event key;
        while (running && areos_key_event(&key)) {
            handle_key(&key);
        }
        struct areos_mouse_event mouse;
        while (running && areos_mouse_event(&mouse)) {
            handle_mouse(&mouse);
        }
        // Redraw when the clock's minute changes.
        int minute = areos_date_time().minute;
        if (minute != last_minute) {
            last_minute = minute;
            dirty = true;
        }
        if (dirty && running) {
            build_items();
            draw();
            dirty = false;
        }
        areos_sleep_ms(15);
    }
    areos_graphics_stop();
    return 0;
}
