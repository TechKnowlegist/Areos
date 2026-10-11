// main.c - the AREOS desktop: start-up, the main loop, keyboard and mouse.
//
// Keys on the desktop:
//   F3 or the Windows key   open/close the Start menu
//   F6 (Shift+F6 back)      move between desktop icons, the taskbar and the front window
//   Tab / arrows            move;  Enter or Space: open or choose
//   Escape                  close the Start menu, or the front window
//   F1                      hear the keys;  F2: hear where you are
//   a letter                jump to the next item starting with it
// The mouse works too: anything it points at is described in text.

#include "desktop.h"

// ---- Shared state ----

struct gui_canvas screen;
const struct gui_theme *theme = &gui_theme_dark;
bool detailed = true;
bool dirty = true;
int mouse_x, mouse_y;
char user_name[33];
uint64_t now_ms;
int wallpaper_choice = WALLPAPER_NEBULA;
enum zone zone = ZONE_DESKTOP;

struct launchable launchables[MAX_LAUNCHABLES];
int launchable_count = 0;

static bool running = true;
static bool shift_held = false;
static int desktop_focus = 0;
static int taskbar_focus = 0;
static int start_focus = 0;
static enum zone zone_before_start = ZONE_DESKTOP;

// What the mouse is over (so it's only described when it changes).
enum area { AREA_NONE, AREA_DESKTOP, AREA_TASKBAR, AREA_START, AREA_WINDOW };
static enum area hover_area = AREA_NONE;
static int hover_index = -1;
static enum window_kind hover_window = WIN_SETTINGS;

static uint8_t mouse_buttons = 0;
static struct window *dragging = NULL;
static int drag_dx, drag_dy;
static uint64_t last_click_ms = 0;
static int last_click_icon = -1;

void request_redraw(void) {
    dirty = true;
}

// ---- Text helpers ----

int text_length(const char *s) {
    int n = 0;
    while (s[n] != '\0') n++;
    return n;
}

void text_copy(char *to, const char *from, int max) {
    int i = 0;
    for (; from[i] != '\0' && i < max - 1; i++) to[i] = from[i];
    to[i] = '\0';
}

void text_append(char *to, const char *from, int max) {
    int start = text_length(to);
    text_copy(to + start, from, max - start);
}

void text_append_number(char *to, int64_t number, int max) {
    char digits[24];
    int n = 0;
    bool negative = number < 0;
    uint64_t value = negative ? (uint64_t)(-number) : (uint64_t)number;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value > 0);
    char text[26];
    int t = 0;
    if (negative) text[t++] = '-';
    while (n > 0) text[t++] = digits[--n];
    text[t] = '\0';
    text_append(to, text, max);
}

bool text_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] != '\0' && a[i] == b[i]) i++;
    return a[i] == b[i];
}

char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// ---- Settings ----

void apply_theme(bool light) {
    theme = light ? &gui_theme_light : &gui_theme_dark;
    areos_setting_set(AREOS_SETTING_THEME, light ? 1 : 0);
    wallpaper_build((enum wallpaper_kind)wallpaper_choice, light);
    request_redraw();
}

void apply_wallpaper(int kind) {
    wallpaper_choice = kind;
    areos_setting_set(AREOS_SETTING_WALLPAPER, (uint64_t)kind);
    wallpaper_build((enum wallpaper_kind)kind, theme == &gui_theme_light);
    request_redraw();
}

void apply_detailed(bool on) {
    detailed = on;
    areos_setting_set(AREOS_SETTING_VERBOSITY, on ? 0 : 1);
}

void save_name(void) {
    for (int i = 0; i < 4; i++) {
        uint64_t chunk = 0;
        for (int b = 0; b < 8; b++) chunk |= (uint64_t)(uint8_t)user_name[i * 8 + b] << (b * 8);
        areos_setting_set(AREOS_SETTING_NAME_FIRST + i, chunk);
    }
}

static void load_settings(void) {
    theme = areos_setting_get(AREOS_SETTING_THEME) == 1 ? &gui_theme_light : &gui_theme_dark;
    detailed = areos_setting_get(AREOS_SETTING_VERBOSITY) == 0;
    uint64_t wall = areos_setting_get(AREOS_SETTING_WALLPAPER);
    wallpaper_choice = wall <= WALLPAPER_PLAIN ? (int)wall : WALLPAPER_NEBULA;
    for (int i = 0; i < 4; i++) {
        uint64_t chunk = areos_setting_get(AREOS_SETTING_NAME_FIRST + i);
        for (int b = 0; b < 8; b++) user_name[i * 8 + b] = (char)((chunk >> (b * 8)) & 0xFF);
    }
    user_name[32] = '\0';
}

// ---- Things you can open ----

static void add_launchable(const char *label, enum icon_kind icon, enum launch_type type, int target,
                           const char *file, bool on_desktop) {
    if (launchable_count >= MAX_LAUNCHABLES) return;
    struct launchable *l = &launchables[launchable_count++];
    text_copy(l->label, label, sizeof(l->label));
    l->icon = icon;
    l->type = type;
    l->target = target;
    text_copy(l->file, file, sizeof(l->file));
    l->on_desktop = on_desktop;
}

static void load_launchables(void) {
    add_launchable("Calculator", ICON_CALCULATOR, LAUNCH_WINDOW, WIN_CALCULATOR, "", true);
    add_launchable("Notes", ICON_NOTES, LAUNCH_WINDOW, WIN_NOTES, "", true);
    // Every .arx app on the disc (except this desktop).
    char file[40];
    for (int i = 0; areos_app_name(i, file, sizeof(file)); i++) {
        if (text_equal(file, "desktop.arx")) continue;
        char label[40];
        text_copy(label, file, sizeof(label));
        int length = text_length(label);
        if (length > 4 && text_equal(label + length - 4, ".arx")) label[length - 4] = '\0';
        if (label[0] >= 'a' && label[0] <= 'z') label[0] = (char)(label[0] - 'a' + 'A');
        add_launchable(label, icon_for_app(label), LAUNCH_APP, i, file, true);
    }
    add_launchable("Settings", ICON_SETTINGS, LAUNCH_WINDOW, WIN_SETTINGS, "", true);
    add_launchable("About AREOS", ICON_ABOUT, LAUNCH_WINDOW, WIN_ABOUT, "", false);
    add_launchable("Command line", ICON_TERMINAL, LAUNCH_COMMAND_LINE, 0, "", true);
}

// ---- Announcements ----

static int zone_items(enum zone z, struct gui_item *items, int max) {
    switch (z) {
    case ZONE_DESKTOP: return desktop_items(items, max);
    case ZONE_TASKBAR: return taskbar_items(items, max);
    case ZONE_START:   return start_items(items, max);
    case ZONE_WINDOW: {
        struct window *w = window_top();
        return w != NULL ? window_items(w, items, max) : 0;
    }
    }
    return 0;
}

static int *zone_focus(enum zone z) {
    switch (z) {
    case ZONE_DESKTOP: return &desktop_focus;
    case ZONE_TASKBAR: return &taskbar_focus;
    case ZONE_START:   return &start_focus;
    case ZONE_WINDOW: {
        struct window *w = window_top();
        return w != NULL ? &w->focus : &desktop_focus;
    }
    }
    return &desktop_focus;
}

void announce_item(struct gui_item *items, int count, int focus) {
    if (count > 0 && focus >= 0 && focus < count) {
        gui_announce_item(&items[focus], focus + 1, count, detailed);
    }
}

static void announce_focus(void) {
    struct gui_item items[48];
    int count = zone_items(zone, items, 48);
    int *focus = zone_focus(zone);
    if (*focus >= count) *focus = count - 1;
    if (*focus < 0) *focus = 0;
    announce_item(items, count, *focus);
}

static void announce_zone_name(void) {
    switch (zone) {
    case ZONE_DESKTOP: gui_announce("Desktop icons."); break;
    case ZONE_TASKBAR: gui_announce("Taskbar."); break;
    case ZONE_START:   gui_announce("Start menu."); break;
    case ZONE_WINDOW: {
        struct window *w = window_top();
        gui_line_start();
        gui_line_add("Window: ");
        gui_line_add(w != NULL ? w->title : "none");
        gui_line_add(".");
        gui_line_say();
        break;
    }
    }
}

static const char *greeting(void) {
    static char text[128];
    struct areos_date_time t = areos_date_time();
    text_copy(text, t.hour < 12 ? "Good morning" : (t.hour < 17 ? "Good afternoon" : "Good evening"), sizeof(text));
    if (user_name[0] != '\0') {
        text_append(text, ", ", sizeof(text));
        text_append(text, user_name, sizeof(text));
    }
    text_append(text, ".", sizeof(text));
    return text;
}

void announce_where(void) {
    gui_line_start();
    gui_line_add("AREOS desktop. ");
    gui_line_add(greeting());
    gui_line_add(" ");
    struct gui_item items[16];
    int n = taskbar_items(items, 16);
    gui_line_add(items[n - 1].name);   // the clock
    gui_line_add(".");
    int open = 0;
    for (int k = 0; k < WIN_COUNT; k++) if (windows[k].open) open++;
    if (open > 0) {
        gui_line_add(" Open windows: ");
        gui_line_add_number(open);
        gui_line_add(".");
    }
    gui_line_say();
    announce_zone_name();
    announce_focus();
}

void set_zone(enum zone z, bool announce) {
    zone = z;
    request_redraw();
    if (announce) {
        announce_zone_name();
        announce_focus();
    }
}

static void announce_help(void) {
    gui_announce("Keys: F3 or the Windows key opens the Start menu. "
                 "F6 moves between the desktop icons, the taskbar and the front window; Shift+F6 goes back. "
                 "Tab or the arrow keys move. Enter or Space opens or chooses. "
                 "Escape closes the Start menu or the front window. "
                 "F2 says where you are. Typing a letter jumps to the next item starting with it. "
                 "Ctrl stops the voice talking, and Ctrl+Alt+S turns the voice off or on.");
}

// ---- Doing things ----

static void open_window_announced(enum window_kind kind) {
    struct window *w = &windows[kind];
    bool was_open = w->open;
    gui_line_start();
    gui_line_add("Window: ");
    gui_line_add(w->title);
    gui_line_add(was_open ? "." : " opened.");
    gui_line_say();
    if (was_open) window_focus(kind);
    else window_open(kind);
    set_zone(ZONE_WINDOW, false);
    announce_focus();
}

void launch(int index) {
    if (index < 0 || index >= launchable_count) return;
    struct launchable *l = &launchables[index];
    if (start_is_open()) start_close();
    switch (l->type) {
    case LAUNCH_WINDOW:
        open_window_announced((enum window_kind)l->target);
        break;
    case LAUNCH_APP:
        gui_line_start();
        gui_line_add("Opening ");
        gui_line_add(l->label);
        gui_line_add(". The desktop comes back when it finishes.");
        gui_line_say();
        areos_setting_set(AREOS_SETTING_RETURN_TO, (uint64_t)index + 1);
        areos_launch(l->file, true);
        running = false;
        break;
    case LAUNCH_COMMAND_LINE:
        gui_announce("Going to the command line. Type desktop to come back.");
        running = false;
        break;
    }
}

// After a window closes or minimizes, put focus somewhere sensible.
static void focus_after_window_change(void) {
    if (window_top() != NULL) {
        set_zone(ZONE_WINDOW, true);
    } else {
        set_zone(ZONE_DESKTOP, true);
    }
}

static void choose_action(int action) {
    int index = ACTION_INDEX(action);
    switch (ACTION_KIND(action)) {
    case A_LAUNCH:
        launch(index);
        break;
    case A_START_BUTTON:
        if (start_is_open()) {
            start_close();
            set_zone(zone_before_start, true);
        } else {
            zone_before_start = zone == ZONE_START ? ZONE_DESKTOP : zone;
            start_open();
            start_focus = 0;
            set_zone(ZONE_START, true);
        }
        break;
    case A_TASKBAR_WINDOW: {
        struct window *w = &windows[index];
        if (!w->minimized && window_top() == w) {
            window_minimize((enum window_kind)index);
            gui_line_start();
            gui_line_add(w->title);
            gui_line_add(" minimized.");
            gui_line_say();
            announce_focus();
        } else {
            open_window_announced((enum window_kind)index);
        }
        break;
    }
    case A_MINIMIZE: {
        window_minimize((enum window_kind)index);
        gui_line_start();
        gui_line_add(windows[index].title);
        gui_line_add(" minimized. It's on the taskbar.");
        gui_line_say();
        focus_after_window_change();
        break;
    }
    case A_CLOSE:
        window_close((enum window_kind)index);
        gui_line_start();
        gui_line_add(windows[index].title);
        gui_line_add(" closed.");
        gui_line_say();
        focus_after_window_change();
        break;
    case A_RESTART:
        gui_announce("Restarting.");
        areos_restart();
        break;
    case A_POWER_OFF:
        gui_announce("Turning off. Goodbye!");
        areos_power_off();
        break;
    case A_CONTENT: {
        struct window *w = window_top();
        if (w == NULL) break;
        const struct window_app *apps[] = { &app_settings, &app_about, &app_calculator, &app_notes };
        apps[w->kind]->choose(index);
        // A radio button changes state: say its new state.
        struct gui_item items[48];
        int count = window_items(w, items, 48);
        if (zone == ZONE_WINDOW && w == window_top() && w->focus < count
            && items[w->focus].role == GUI_ROLE_RADIO && !setup_active()) {
            announce_item(items, count, w->focus);
        }
        break;
    }
    default:
        // The clock: read out the time and date.
        if (zone == ZONE_TASKBAR) announce_focus();
        break;
    }
    request_redraw();
}

// ---- Keyboard ----

static void move_focus(int step) {
    struct gui_item items[48];
    int count = zone_items(zone, items, 48);
    if (count == 0) return;
    int *focus = zone_focus(zone);
    *focus = (*focus + step + count) % count;
    request_redraw();
    announce_item(items, count, *focus);
}

static void jump_to_letter(char letter) {
    struct gui_item items[48];
    int count = zone_items(zone, items, 48);
    int *focus = zone_focus(zone);
    for (int step = 1; step <= count; step++) {
        int i = (*focus + step) % count;
        if (to_lower(items[i].name[0]) == to_lower(letter)) {
            *focus = i;
            request_redraw();
            announce_item(items, count, i);
            return;
        }
    }
}

static void cycle_zone(int direction) {
    // The order F6 goes in: desktop icons, taskbar, then the front window (if any).
    enum zone ring[3] = { ZONE_DESKTOP, ZONE_TASKBAR, ZONE_WINDOW };
    int ring_size = window_top() != NULL ? 3 : 2;
    if (start_is_open()) start_close();
    int position = 0;
    for (int i = 0; i < ring_size; i++) if (ring[i] == zone) position = i;
    position = (position + direction + ring_size) % ring_size;
    set_zone(ring[position], true);
}

static void handle_key(struct areos_key_event *e) {
    if (e->key == AREOS_KEY_SHIFT) {
        shift_held = e->pressed;
        return;
    }
    if (!e->pressed) return;

    // Keys that work everywhere.
    if (e->key == AREOS_KEY_F1) { announce_help(); return; }
    if (e->key == AREOS_KEY_F1 + 1) { announce_where(); return; }
    if (e->key == AREOS_KEY_F1 + 2 || e->key == AREOS_KEY_SUPER) { choose_action(ACTION(A_START_BUTTON, 0)); return; }
    if (e->key == AREOS_KEY_F1 + 5) { cycle_zone(shift_held ? -1 : 1); return; }

    // The front window gets first go at keys (typing in Notes, numbers in Calculator).
    if (zone == ZONE_WINDOW) {
        struct window *w = window_top();
        if (w == NULL) {
            set_zone(ZONE_DESKTOP, true);
            return;
        }
        const struct window_app *apps[] = { &app_settings, &app_about, &app_calculator, &app_notes };
        struct gui_item items[48];
        int count = window_items(w, items, 48);
        if (w->focus < count - 2 && apps[w->kind]->key(e, items, count, &w->focus)) {
            request_redraw();
            return;
        }
    }

    switch (e->key) {
    case AREOS_KEY_TAB:
        move_focus(shift_held ? -1 : 1);
        break;
    case AREOS_KEY_DOWN: case AREOS_KEY_RIGHT:
        move_focus(1);
        break;
    case AREOS_KEY_UP: case AREOS_KEY_LEFT:
        move_focus(-1);
        break;
    case AREOS_KEY_HOME: case AREOS_KEY_END: {
        struct gui_item items[48];
        int count = zone_items(zone, items, 48);
        int *focus = zone_focus(zone);
        *focus = e->key == AREOS_KEY_HOME ? 0 : count - 1;
        request_redraw();
        announce_item(items, count, *focus);
        break;
    }
    case AREOS_KEY_ENTER: case AREOS_KEY_SPACE: {
        struct gui_item items[48];
        int count = zone_items(zone, items, 48);
        int *focus = zone_focus(zone);
        if (*focus >= 0 && *focus < count) {
            if (items[*focus].disabled) {
                gui_announce("That isn't available yet.");
            } else {
                choose_action(items[*focus].action);
            }
        }
        break;
    }
    case AREOS_KEY_ESCAPE:
        if (zone == ZONE_START) {
            start_close();
            set_zone(zone_before_start, true);
        } else if (zone == ZONE_WINDOW && window_top() != NULL) {
            choose_action(ACTION(A_CLOSE, window_top()->kind));
        }
        break;
    default:
        if (e->character > ' ' && e->character <= '~') jump_to_letter(e->character);
        break;
    }
}

// ---- Mouse ----

static int item_at(struct gui_item *items, int count, int x, int y) {
    for (int i = 0; i < count; i++) {
        if (x >= items[i].x && x < items[i].x + items[i].w && y >= items[i].y && y < items[i].y + items[i].h) return i;
    }
    return -1;
}

// Work out what's under the mouse: which area, which item, and its name.
static enum area hit_test(int *index, const char **name, enum window_kind *which) {
    static struct gui_item items[48];
    *index = -1;
    *name = NULL;
    if (start_contains(mouse_x, mouse_y)) {
        int count = start_items(items, 48);
        *index = item_at(items, count, mouse_x, mouse_y);
        if (*index >= 0) *name = items[*index].name;
        return AREA_START;
    }
    struct window *w = window_at(mouse_x, mouse_y);
    if (w != NULL) {
        int count = window_items(w, items, 48);
        *index = item_at(items, count, mouse_x - w->x, mouse_y - w->y);
        *which = w->kind;
        if (*index >= 0) *name = items[*index].name;
        else if (window_title_at(w, mouse_x, mouse_y)) *name = w->title;
        return AREA_WINDOW;
    }
    if (mouse_y >= screen.height - TASKBAR_H) {
        int count = taskbar_items(items, 48);
        *index = item_at(items, count, mouse_x, mouse_y);
        if (*index >= 0) *name = items[*index].name;
        return AREA_TASKBAR;
    }
    int count = desktop_items(items, 48);
    *index = item_at(items, count, mouse_x, mouse_y);
    if (*index >= 0) *name = items[*index].name;
    return AREA_DESKTOP;
}

static void describe_hover(void) {
    int index;
    const char *name;
    enum window_kind which = WIN_SETTINGS;
    enum area area = hit_test(&index, &name, &which);
    if (area == hover_area && index == hover_index && (area != AREA_WINDOW || which == hover_window)) return;
    hover_area = area;
    hover_index = index;
    hover_window = which;
    request_redraw();
    // Wobbling on the edge of a button shouldn't repeat its name over and over:
    // the same name again within 1.5 seconds is skipped.
    static char last_name[48];
    static uint64_t last_time;
    if (name != NULL && text_equal(name, last_name) && now_ms - last_time < 1500) {
        last_time = now_ms;
        return;
    }
    if (name != NULL) {
        areos_speech_stop();   // the newest thing under the mouse is what you hear
        text_copy(last_name, name, sizeof(last_name));
        last_time = now_ms;
        gui_line_start();
        gui_line_add("Mouse: ");
        gui_line_add(name);
        gui_line_say();
    }
}

static void handle_click(void) {
    int index;
    const char *name;
    enum window_kind which = WIN_SETTINGS;

    // Clicking outside an open Start menu just closes it.
    if (start_is_open() && !start_contains(mouse_x, mouse_y)) {
        bool on_start_button = mouse_y >= screen.height - TASKBAR_H && mouse_x < 60;
        if (!on_start_button) {
            start_close();
            set_zone(zone_before_start, false);
            return;
        }
    }

    enum area area = hit_test(&index, &name, &which);
    struct gui_item items[48];
    switch (area) {
    case AREA_START:
        if (index >= 0) {
            int count = start_items(items, 48);
            start_focus = index;
            choose_action(items[index].action);
            (void)count;
        }
        break;
    case AREA_WINDOW: {
        struct window *w = &windows[which];
        if (window_top() != w) window_focus(which);
        zone = ZONE_WINDOW;
        if (index < 0 && window_title_at(w, mouse_x, mouse_y)) {
            dragging = w;              // start dragging the window by its title bar
            drag_dx = mouse_x - w->x;
            drag_dy = mouse_y - w->y;
        } else if (index >= 0) {
            window_items(w, items, 48);
            w->focus = index;
            if (items[index].role == GUI_ROLE_TEXT_FIELD) {
                int count = window_items(w, items, 48);
                announce_item(items, count, index);
            } else {
                choose_action(items[index].action);
            }
        }
        break;
    }
    case AREA_TASKBAR:
        if (index >= 0) {
            taskbar_items(items, 48);
            taskbar_focus = index;
            choose_action(items[index].action);
        }
        break;
    case AREA_DESKTOP:
        if (index >= 0) {
            int count = desktop_items(items, 48);
            bool double_click = index == last_click_icon && now_ms - last_click_ms < 500;
            desktop_focus = index;
            zone = ZONE_DESKTOP;
            if (double_click) {
                choose_action(items[index].action);
                last_click_icon = -1;
            } else {
                announce_item(items, count, index);
                if (detailed) gui_announce("Double-click to open.");
                last_click_icon = index;
                last_click_ms = now_ms;
            }
        }
        break;
    default:
        break;
    }
    request_redraw();
}

static void handle_mouse(struct areos_mouse_event *e) {
    mouse_x += e->dx;
    mouse_y += e->dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= screen.width) mouse_x = screen.width - 1;
    if (mouse_y >= screen.height) mouse_y = screen.height - 1;
    request_redraw();

    bool left = (e->buttons & AREOS_MOUSE_LEFT) != 0;
    bool was_left = (mouse_buttons & AREOS_MOUSE_LEFT) != 0;
    mouse_buttons = e->buttons;

    if (setup_active()) {
        setup_mouse_move();
        if (left && !was_left) setup_click();
        return;
    }
    if (dragging != NULL) {
        if (!left) {
            dragging = NULL;
        } else {
            dragging->x = mouse_x - drag_dx;
            dragging->y = mouse_y - drag_dy;
            window_moved(dragging);
        }
        return;
    }
    describe_hover();
    if (left && !was_left) handle_click();
}

// ---- Drawing ----

// The arrow pointer: '#' outline, '.' fill.
static const char *pointer_rows[] = {
    "#", "##", "#.#", "#..#", "#...#", "#....#", "#.....#", "#......#", "#.......#",
    "#........#", "#.....####", "#..#..#", "#.# #..#", "##  #..#", "#    #..#", "      ##",
};

static void draw_pointer(void) {
    for (int row = 0; row < 16; row++) {
        for (int col = 0; pointer_rows[row][col] != '\0'; col++) {
            char p = pointer_rows[row][col];
            if (p == '#' || p == '.') {
                gui_fill(&screen, mouse_x + col * 2, mouse_y + row * 2, 2, 2, p == '#' ? 0x000000 : 0xFFFFFF);
            }
        }
    }
}

static void draw(void) {
    if (setup_active()) {
        setup_draw();
    } else {
        wallpaper_draw(&screen);
        shell_draw_desktop_icons(zone == ZONE_DESKTOP ? desktop_focus : -1,
                                 hover_area == AREA_DESKTOP ? hover_index : -1);
        windows_draw();
        shell_draw_taskbar(zone == ZONE_TASKBAR ? taskbar_focus : -1,
                           hover_area == AREA_TASKBAR ? hover_index : -1);
        shell_draw_start(zone == ZONE_START ? start_focus : -1,
                         hover_area == AREA_START ? hover_index : -1);
        toast_draw();
    }
    draw_pointer();
    areos_graphics_draw(screen.pixels, (uint32_t)screen.width, (uint32_t)screen.height);
}

// ---- Start-up ----

void desktop_enter(bool after_setup) {
    zone = ZONE_DESKTOP;
    desktop_focus = 0;
    request_redraw();
    // Coming back from an app? Put focus back on its icon.
    int came_from = (int)areos_setting_get(AREOS_SETTING_RETURN_TO) - 1;
    if (came_from >= 0) {
        areos_setting_set(AREOS_SETTING_RETURN_TO, 0);
        struct gui_item items[48];
        int count = desktop_items(items, 48);
        for (int i = 0; i < count; i++) {
            if (items[i].action == ACTION(A_LAUNCH, came_from)) desktop_focus = i;
        }
        if (came_from < launchable_count) {
            gui_line_start();
            gui_line_add("Back from ");
            gui_line_add(launchables[came_from].label);
            gui_line_add(".");
            gui_line_say();
        }
        announce_item(items, count, desktop_focus);
        return;
    }
    announce_where();
    if (detailed) {
        gui_announce("Press F3 for the Start menu, F6 to move between areas, F1 for all the keys.");
    }
    if (after_setup) {
        char text[96] = "Welcome to AREOS";
        if (user_name[0] != '\0') {
            text_append(text, ", ", sizeof(text));
            text_append(text, user_name, sizeof(text));
        }
        text_append(text, "! Your desktop is ready.", sizeof(text));
        toast_show(text);
    }
}

int main(void) {
    uint32_t width, height;
    if (!areos_graphics_start(&width, &height)) {
        areos_print("The desktop couldn't switch to graphics mode.\n");
        return 1;
    }
    screen.width = (int)width;
    screen.height = (int)height;
    screen.pixels = areos_more_memory((size_t)width * height * 4);
    if (screen.pixels == NULL) {
        areos_graphics_stop();
        areos_print("Not enough memory for the desktop.\n");
        return 1;
    }
    mouse_x = screen.width / 2;
    mouse_y = screen.height / 2;
    now_ms = areos_time_ms();

    load_settings();
    load_launchables();
    wallpaper_init(screen.width, screen.height);
    wallpaper_build((enum wallpaper_kind)wallpaper_choice, theme == &gui_theme_light);
    windows_init();
    shell_init();

    if (areos_setting_get(AREOS_SETTING_SETUP_DONE)) desktop_enter(false);
    else setup_begin();

    int last_minute = -1;
    while (running) {
        now_ms = areos_time_ms();
        struct areos_key_event key;
        while (running && areos_key_event(&key)) {
            if (setup_active()) setup_key(&key);
            else handle_key(&key);
        }
        struct areos_mouse_event mouse;
        while (running && areos_mouse_event(&mouse)) {
            handle_mouse(&mouse);
        }
        if (!running) break;

        windows_update();
        bool animating = windows_animating() || start_animating() || toast_active();
        int minute = areos_date_time().minute;
        if (minute != last_minute) {
            last_minute = minute;
            request_redraw();
        }
        if (dirty || animating) {
            dirty = false;
            draw();
        }
        areos_sleep_ms(animating ? 4 : 15);
    }
    areos_graphics_stop();
    return 0;
}
