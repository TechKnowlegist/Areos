// shell.c - the desktop's furniture: icons, taskbar, the AREOS launcher
// (the start menu), quick settings, notifications.

#include "desktop.h"

// ---- Desktop icons ----

#define ICON_CELL_W 104
#define ICON_CELL_H 108
#define ICON_SIZE   56

int desktop_items(struct gui_item *items, int max) {
    int rows = (screen.height - TASKBAR_H - 24) / ICON_CELL_H;
    if (rows < 1) rows = 1;
    int count = 0;
    for (int i = 0; i < launchable_count && count < max; i++) {
        if (!launchables[i].on_desktop) continue;
        int column = count / rows, row = count % rows;
        items[count] = (struct gui_item){
            .name = launchables[i].label, .role = GUI_ROLE_ICON, .action = ACTION(A_LAUNCH, i),
            .x = 16 + column * ICON_CELL_W, .y = 16 + row * ICON_CELL_H,
            .w = ICON_CELL_W - 8, .h = ICON_CELL_H - 8,
        };
        count++;
    }
    return count;
}

// Text with a soft outline, so it's readable on any wallpaper.
static void outlined_text(int x, int y, const char *text, uint32_t color, uint32_t outline) {
    gui_text(&screen, x + 1, y + 1, text, outline, 1);
    gui_text(&screen, x - 1, y, text, outline, 1);
    gui_text(&screen, x + 1, y - 1, text, outline, 1);
    gui_text(&screen, x, y, text, color, 1);
}

void shell_draw_desktop_icons(int focus, int hovered) {
    struct gui_item items[MAX_LAUNCHABLES];
    int count = desktop_items(items, MAX_LAUNCHABLES);
    bool light = light_mode;
    uint32_t label_color = light ? 0x0A0F14 : 0xFFFFFF;
    uint32_t outline = light ? 0xFFFFFF : 0x000000;
    for (int i = 0; i < count; i++) {
        struct gui_item *it = &items[i];
        if (i == focus) {
            gui_rounded_alpha(&screen, it->x, it->y, it->w, it->h, 10, theme->accent2, 80);
            gui_frame(&screen, it->x, it->y, it->w, it->h, 2, theme->accent);
        } else if (i == hovered) {
            gui_rounded_alpha(&screen, it->x, it->y, it->w, it->h, 10, 0xFFFFFF, 40);
        }
        int index = ACTION_INDEX(it->action);
        icon_draw(&screen, launchables[index].icon, it->x + (it->w - ICON_SIZE) / 2, it->y + 8, ICON_SIZE);
        // The label, split onto two lines if it's long.
        const char *label = it->name;
        int length = text_length(label);
        int split = -1;
        if (length > 12) {
            for (int c = 0; c < length && c <= 12; c++) if (label[c] == ' ') split = c;
        }
        int label_y = it->y + ICON_SIZE + 16;
        if (split < 0) {
            outlined_text(it->x + (it->w - gui_text_width(label, 1)) / 2, label_y, label, label_color, outline);
        } else {
            char first[40], second[40];
            text_copy(first, label, split + 1);
            text_copy(second, label + split + 1, 40);
            outlined_text(it->x + (it->w - gui_text_width(first, 1)) / 2, label_y, first, label_color, outline);
            outlined_text(it->x + (it->w - gui_text_width(second, 1)) / 2, label_y + 16, second, label_color, outline);
        }
    }
}

// ---- Taskbar ----

#define START_BUTTON_SIZE 44
#define TASK_BUTTON_W 184

static char clock_text[24];
static char date_text[32];
static char clock_spoken[64];

static const char *month_short[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *month_long[] = { "January", "February", "March", "April", "May", "June", "July",
                                    "August", "September", "October", "November", "December" };

static void update_clock(void) {
    struct areos_date_time t = areos_date_time();
    int month = (t.month >= 1 && t.month <= 12) ? t.month - 1 : 0;
    int hour12 = t.hour % 12 == 0 ? 12 : t.hour % 12;
    clock_text[0] = '\0';
    if (clock_24h) {
        if (t.hour < 10) text_append(clock_text, "0", 24);
        text_append_number(clock_text, t.hour, 24);
    } else {
        text_append_number(clock_text, hour12, 24);
    }
    text_append(clock_text, t.minute < 10 ? ":0" : ":", 24);
    text_append_number(clock_text, t.minute, 24);
    if (!clock_24h) text_append(clock_text, t.hour < 12 ? " AM" : " PM", 24);
    date_text[0] = '\0';
    text_append(date_text, month_short[month], 32);
    text_append(date_text, " ", 32);
    text_append_number(date_text, t.day, 32);
    text_append(date_text, ", ", 32);
    text_append_number(date_text, t.year, 32);
    text_copy(clock_spoken, "Clock: ", 64);
    text_append(clock_spoken, clock_text, 64);
    text_append(clock_spoken, ", ", 64);
    text_append(clock_spoken, month_long[month], 64);
    text_append(clock_spoken, " ", 64);
    text_append_number(clock_spoken, t.day, 64);
}

int taskbar_items(struct gui_item *items, int max) {
    int top = screen.height - TASKBAR_H;
    int count = 0;
    update_clock();
    items[count++] = (struct gui_item){ .name = "AREOS launcher", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_START_BUTTON, 0), .x = 8, .y = top + 4,
        .w = START_BUTTON_SIZE, .h = START_BUTTON_SIZE };
    // One button per open window, in a steady order so they don't jump about.
    struct window *front = window_top();
    int x = 8 + START_BUTTON_SIZE + 12;
    for (int k = 0; k < WIN_COUNT && count < max - 1; k++) {
        struct window *w = &windows[k];
        if (!w->open) continue;
        items[count++] = (struct gui_item){ .name = w->title, .role = GUI_ROLE_TASKBAR,
            .action = ACTION(A_TASKBAR_WINDOW, k), .x = x, .y = top + 4, .w = TASK_BUTTON_W,
            .h = START_BUTTON_SIZE, .state = w->minimized ? "minimized" : (w == front ? "active" : "") };
        x += TASK_BUTTON_W + 6;
    }
    items[count++] = (struct gui_item){ .name = "Quick settings", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_QUICK_BUTTON, 0), .x = screen.width - 132 - START_BUTTON_SIZE - 8, .y = top + 4,
        .w = START_BUTTON_SIZE, .h = START_BUTTON_SIZE };
    items[count++] = (struct gui_item){ .name = clock_spoken, .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_NONE, 0), .x = screen.width - 132, .y = top + 4, .w = 124,
        .h = START_BUTTON_SIZE };
    return count;
}

void shell_draw_taskbar(int focus, int hovered) {
    int top = screen.height - TASKBAR_H;
    gui_fill_alpha(&screen, 0, top, screen.width, TASKBAR_H, theme->panel, 225);
    gui_fill(&screen, 0, top, screen.width, 1, theme->panel_border);

    struct gui_item items[16];
    int count = taskbar_items(items, 16);
    for (int i = 0; i < count; i++) {
        struct gui_item *it = &items[i];
        bool is_focus = i == focus, is_hover = i == hovered;
        if (is_focus || is_hover) {
            gui_rounded_alpha(&screen, it->x, it->y, it->w, it->h, 8,
                              is_focus ? theme->selected : 0xFFFFFF, is_focus ? 255 : 30);
        }
        if (ACTION_KIND(it->action) == A_START_BUTTON) {
            icon_draw(&screen, ICON_LOGO, it->x + 2, it->y + 2, START_BUTTON_SIZE - 4);
        } else if (ACTION_KIND(it->action) == A_QUICK_BUTTON) {
            icon_draw(&screen, ICON_QUICK, it->x + 6, it->y + 6, START_BUTTON_SIZE - 12);
        } else if (ACTION_KIND(it->action) == A_TASKBAR_WINDOW) {
            struct window *w = &windows[ACTION_INDEX(it->action)];
            bool active = it->state != NULL && text_equal(it->state, "active");
            if (active) {
                gui_rounded_alpha(&screen, it->x, it->y, it->w, it->h, 8, 0xFFFFFF, 22);
                gui_fill(&screen, it->x + it->w / 2 - 20, it->y + it->h - 3, 40, 3, theme->accent);
            } else if (!w->minimized) {
                gui_fill(&screen, it->x + it->w / 2 - 8, it->y + it->h - 3, 16, 3, theme->text_dim);
            }
            icon_draw(&screen, w->icon, it->x + 8, it->y + 10, 24);
            gui_text(&screen, it->x + 42, it->y + 14, w->title, w->minimized ? theme->text_dim : theme->text, 1);
        } else {
            // The clock.
            gui_text_bold(&screen, it->x + it->w - 10 - gui_text_width(clock_text, 1), it->y + 4,
                          clock_text, theme->text, 1);
            gui_text(&screen, it->x + it->w - 10 - gui_text_width(date_text, 1), it->y + 23,
                     date_text, theme->text_dim, 1);
        }
        if (is_focus) {
            gui_frame(&screen, it->x, it->y, it->w, it->h, 2, theme->accent);
        }
    }
}

// ---- The AREOS launcher (the start menu) ----

#define START_W 400
#define START_H 560
#define START_ROW_H 38
#define START_ANIM_MS 170

static bool start_open_now = false;
static bool start_closing = false;
static uint64_t start_anim_start = 0;

static int start_x(void) { return 8; }
static int start_y(void) { return screen.height - TASKBAR_H - 8 - START_H; }

int start_items(struct gui_item *items, int max) {
    int count = 0;
    int x = start_x(), y = start_y() + 100;
    for (int i = 0; i < launchable_count && count < max - 2; i++) {
        items[count] = (struct gui_item){ .name = launchables[i].label, .role = GUI_ROLE_MENU_ITEM,
            .action = ACTION(A_LAUNCH, i), .x = x + 12, .y = y + count * START_ROW_H,
            .w = START_W - 24, .h = START_ROW_H - 4 };
        count++;
    }
    int bottom = start_y() + START_H - 56;
    items[count++] = (struct gui_item){ .name = "Restart", .role = GUI_ROLE_MENU_ITEM,
        .action = ACTION(A_RESTART, 0), .x = x + 12, .y = bottom, .w = START_W / 2 - 18, .h = 44 };
    items[count++] = (struct gui_item){ .name = "Turn off", .role = GUI_ROLE_MENU_ITEM,
        .action = ACTION(A_POWER_OFF, 0), .x = x + START_W / 2 + 6, .y = bottom, .w = START_W / 2 - 18, .h = 44 };
    return count;
}

void start_open(void) {
    start_open_now = true;
    start_closing = false;
    start_anim_start = now_ms;
    request_redraw();
}

void start_close(void) {
    if (!start_open_now || start_closing) return;
    start_closing = true;
    start_anim_start = now_ms;
    request_redraw();
}

bool start_is_open(void) {
    return start_open_now && !start_closing;
}

bool start_animating(void) {
    if (!start_open_now) return false;
    if (now_ms - start_anim_start < START_ANIM_MS) return true;
    if (start_closing) {           // finished closing
        start_open_now = false;
        start_closing = false;
        request_redraw();
    }
    return false;
}

bool start_contains(int x, int y) {
    return start_is_open() && x >= start_x() && x < start_x() + START_W && y >= start_y() && y < start_y() + START_H;
}

void shell_draw_start(int focus, int hovered) {
    if (!start_open_now) return;
    int progress = (int)((now_ms - start_anim_start) * 1000 / START_ANIM_MS);
    if (progress > 1000) progress = 1000;
    int amount = gui_ease_out(progress);
    if (start_closing) amount = 1000 - amount;
    int slide = (1000 - amount) * 40 / 1000;   // slides up 40 pixels as it opens
    int alpha = amount * 255 / 1000;
    if (alpha <= 0) return;

    int x = start_x(), y = start_y() + slide;
    if (alpha > 200) gui_shadow(&screen, x, y, START_W, START_H, 16);
    gui_rounded_alpha(&screen, x, y, START_W, START_H, 12, theme->panel, alpha * 252 / 255);
    if (alpha < 160) return;   // too faint to show details yet

    // Header: an avatar with your initial, and your name.
    char initial[2] = { user_name[0] != '\0' ? user_name[0] : 'A', '\0' };
    if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] = (char)(initial[0] - 'a' + 'A');
    gui_circle(&screen, x + 44, y + 46, 26, theme->accent2);
    gui_text_centered(&screen, x + 44, y + 30, initial, 0xFFFFFF, 2);
    gui_text_bold(&screen, x + 84, y + 24, user_name[0] != '\0' ? user_name : "Welcome", theme->text, 2);
    gui_text(&screen, x + 86, y + 58, "AREOS launcher", theme->text_dim, 1);
    gui_fill(&screen, x + 16, y + 86, START_W - 32, 1, theme->panel_border);

    struct gui_item items[MAX_LAUNCHABLES + 2];
    int count = start_items(items, MAX_LAUNCHABLES + 2);
    gui_fill(&screen, x + 16, start_y() + START_H - 68 + slide, START_W - 32, 1, theme->panel_border);
    for (int i = 0; i < count; i++) {
        struct gui_item *it = &items[i];
        int iy = it->y + slide;
        if (i == focus) {
            gui_rounded(&screen, it->x, iy, it->w, it->h, 8, theme->selected);
            gui_frame(&screen, it->x, iy, it->w, it->h, 2, theme->accent);
        } else if (i == hovered) {
            gui_rounded(&screen, it->x, iy, it->w, it->h, 8, theme->hover);
        }
        enum icon_kind icon;
        int kind = ACTION_KIND(it->action);
        if (kind == A_RESTART) icon = ICON_RESTART;
        else if (kind == A_POWER_OFF) icon = ICON_POWER;
        else icon = launchables[ACTION_INDEX(it->action)].icon;
        icon_draw(&screen, icon, it->x + 8, iy + (it->h - 26) / 2, 26);
        gui_text(&screen, it->x + 46, iy + (it->h - 16) / 2, it->name, theme->text, 1);
    }
}

// ---- Quick settings ----
// A panel above the clock with the things you change most: toggles for
// the screen reader, dark mode, a big pointer and the 24-hour clock, and
// sliders for volume and speech speed.

#define QUICK_W 372
#define QUICK_H 384
#define QUICK_ANIM_MS 170

enum { Q_SPEECH, Q_DARK, Q_POINTER, Q_CLOCK, Q_VOLUME, Q_SPEED, Q_ALL_SETTINGS };

static bool quick_open_now = false;
static bool quick_closing = false;
static uint64_t quick_anim_start = 0;
static char volume_text[24], speed_text[24];

static int quick_x(void) { return screen.width - QUICK_W - 8; }
static int quick_y(void) { return screen.height - TASKBAR_H - 8 - QUICK_H; }

int quick_items(struct gui_item *items, int max) {
    (void)max;
    int x = quick_x() + 16, y = quick_y() + 52;
    int tile_w = (QUICK_W - 48) / 2, tile_h = 60;
    struct { const char *name; bool on; } tiles[4] = {
        { "Screen reader", speech_on() },
        { "Dark mode", !light_mode },
        { "Large pointer", mouse_size > 0 },
        { "24-hour clock", clock_24h },
    };
    int count = 0;
    for (int i = 0; i < 4; i++) {
        items[count++] = (struct gui_item){ .name = tiles[i].name, .role = GUI_ROLE_SWITCH,
            .action = ACTION(A_QUICK, i), .checked = tiles[i].on,
            .x = x + (i % 2) * (tile_w + 16), .y = y + (i / 2) * (tile_h + 12), .w = tile_w, .h = tile_h };
    }
    volume_text[0] = '\0';
    text_append_number(volume_text, volume_level(), sizeof(volume_text));
    text_append(volume_text, "%", sizeof(volume_text));
    speed_text[0] = '\0';
    text_append_number(speed_text, speech_speed(), sizeof(speed_text));
    text_append(speed_text, "%", sizeof(speed_text));
    int slider_y = y + 2 * (tile_h + 12) + 26;
    items[count++] = (struct gui_item){ .name = "Volume", .role = GUI_ROLE_SLIDER,
        .action = ACTION(A_QUICK, Q_VOLUME), .value = volume_text,
        .x = x, .y = slider_y, .w = QUICK_W - 32, .h = 30 };
    items[count++] = (struct gui_item){ .name = "Speech speed", .role = GUI_ROLE_SLIDER,
        .action = ACTION(A_QUICK, Q_SPEED), .value = speed_text,
        .x = x, .y = slider_y + 54, .w = QUICK_W - 32, .h = 30 };
    items[count++] = (struct gui_item){ .name = "All settings", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_QUICK, Q_ALL_SETTINGS),
        .x = x, .y = quick_y() + QUICK_H - 50, .w = 140, .h = 36 };
    return count;
}

void quick_choose(int index) {
    switch (index) {
    case Q_SPEECH:  apply_speech(!speech_on()); break;
    case Q_DARK:    apply_theme(!light_mode); break;
    case Q_POINTER: apply_mouse_size(mouse_size > 0 ? 0 : 1); break;
    case Q_CLOCK:   apply_clock_24h(!clock_24h); break;
    case Q_VOLUME:
        if (slider_click >= 0) apply_volume((slider_click + 5) / 10);
        else if (slider_step != 0) apply_volume(volume_level() + slider_step * 5);
        break;
    case Q_SPEED:
        if (slider_click >= 0) apply_speech_speed(50 + slider_click * 250 / 1000);
        else if (slider_step != 0) apply_speech_speed(speech_speed() + slider_step * 10);
        break;
    case Q_ALL_SETTINGS:
        quick_close();
        open_window(WIN_SETTINGS);
        break;
    }
    request_redraw();
}

void quick_open(void) {
    quick_open_now = true;
    quick_closing = false;
    quick_anim_start = now_ms;
    request_redraw();
}

void quick_close(void) {
    if (!quick_open_now || quick_closing) return;
    quick_closing = true;
    quick_anim_start = now_ms;
    request_redraw();
}

bool quick_is_open(void) {
    return quick_open_now && !quick_closing;
}

bool quick_animating(void) {
    if (!quick_open_now) return false;
    if (now_ms - quick_anim_start < QUICK_ANIM_MS) return true;
    if (quick_closing) {
        quick_open_now = false;
        quick_closing = false;
        request_redraw();
    }
    return false;
}

bool quick_contains(int x, int y) {
    return quick_is_open() && x >= quick_x() && x < quick_x() + QUICK_W && y >= quick_y() && y < quick_y() + QUICK_H;
}

// A slider: a track, the filled part up to the value, and a round knob.
static void draw_slider(struct gui_canvas *c, const struct gui_item *it, int permille, bool focused, bool hovered,
                        int dy) {
    int y = it->y + dy;
    gui_text(c, it->x, y - 20, it->name, theme->text, 1);
    if (it->value) gui_text(c, it->x + it->w - gui_text_width(it->value, 1), y - 20, it->value, theme->text_dim, 1);
    int track_y = y + it->h / 2 - 2;
    int knob_x = it->x + 10 + (it->w - 20) * permille / 1000;
    gui_rounded(c, it->x + 4, track_y, it->w - 8, 5, 2, theme->panel_border);
    gui_rounded(c, it->x + 4, track_y, knob_x - it->x - 4, 5, 2, theme->accent);
    if (focused || hovered) gui_circle(c, knob_x, track_y + 2, 13, gui_blend(theme->panel, theme->accent, 70));
    gui_circle(c, knob_x, track_y + 2, 9, theme->accent);
    gui_circle(c, knob_x, track_y + 2, 4, theme->panel);
    if (focused) gui_frame(c, it->x - 4, y - 2, it->w + 8, it->h + 4, 2, theme->accent);
}

void shell_draw_quick(int focus, int hovered) {
    if (!quick_open_now) return;
    int progress = (int)((now_ms - quick_anim_start) * 1000 / QUICK_ANIM_MS);
    if (progress > 1000) progress = 1000;
    int amount = gui_ease_out(progress);
    if (quick_closing) amount = 1000 - amount;
    int slide = (1000 - amount) * 40 / 1000;
    int alpha = amount * 255 / 1000;
    if (alpha <= 0) return;

    int x = quick_x(), y = quick_y() + slide;
    if (alpha > 200) gui_shadow(&screen, x, y, QUICK_W, QUICK_H, 16);
    gui_rounded_alpha(&screen, x, y, QUICK_W, QUICK_H, 12, theme->panel, alpha * 252 / 255);
    if (alpha < 160) return;

    gui_text_bold(&screen, x + 16, y + 18, "Quick settings", theme->text, 1);
    struct gui_item items[12];
    int count = quick_items(items, 12);
    for (int i = 0; i < count; i++) {
        struct gui_item *it = &items[i];
        int iy = it->y + slide;
        if (it->role == GUI_ROLE_SWITCH) {
            uint32_t fill = it->checked ? theme->accent : (i == hovered ? theme->hover : theme->background);
            uint32_t ink = it->checked ? (light_mode ? 0xFFFFFF : 0x06120C) : theme->text;
            gui_rounded(&screen, it->x, iy, it->w, it->h, 10, fill);
            gui_text_bold(&screen, it->x + 14, iy + 12, it->name, ink, 1);
            gui_text(&screen, it->x + 14, iy + 34, it->checked ? "On" : "Off", ink, 1);
            if (i == focus) gui_frame(&screen, it->x - 3, iy - 3, it->w + 6, it->h + 6, 2, theme->accent2);
        } else if (it->role == GUI_ROLE_SLIDER) {
            int permille = ACTION_INDEX(it->action) == Q_VOLUME ? volume_level() * 10
                                                               : (speech_speed() - 50) * 1000 / 250;
            draw_slider(&screen, it, permille, i == focus, i == hovered, slide);
        } else {
            struct gui_item moved = *it;
            moved.y = iy;
            draw_window_item(&screen, &moved, i == focus, i == hovered);
        }
    }
}

void shell_draw_slider(struct gui_canvas *c, const struct gui_item *it, int permille, bool focused, bool hovered) {
    draw_slider(c, it, permille, focused, hovered, 0);
}

// ---- Notifications ("toasts") ----

#define TOAST_W 380
#define TOAST_H 76
#define TOAST_IN_MS 320
#define TOAST_STAY_MS 4200

static char toast_text[96];
static uint64_t toast_start = 0;
static bool toast_on = false;

void toast_show(const char *text) {
    text_copy(toast_text, text, sizeof(toast_text));
    toast_start = now_ms;
    toast_on = true;
    gui_line_start();
    gui_line_add("Notification: ");
    gui_line_add(text);
    gui_line_say();
    request_redraw();
}

bool toast_active(void) {
    if (toast_on && now_ms - toast_start > TOAST_IN_MS * 2 + TOAST_STAY_MS) {
        toast_on = false;
        request_redraw();
    }
    return toast_on;
}

void toast_draw(void) {
    if (!toast_active()) return;
    uint64_t t = now_ms - toast_start;
    int shown;  // 0 = hidden off the right edge, 1000 = fully in
    if (t < TOAST_IN_MS) shown = gui_ease_out((int)(t * 1000 / TOAST_IN_MS));
    else if (t < TOAST_IN_MS + TOAST_STAY_MS) shown = 1000;
    else shown = 1000 - gui_ease_out((int)((t - TOAST_IN_MS - TOAST_STAY_MS) * 1000 / TOAST_IN_MS));
    int x = screen.width - (TOAST_W + 16) * shown / 1000;
    int y = screen.height - TASKBAR_H - TOAST_H - 16;
    gui_shadow(&screen, x, y, TOAST_W, TOAST_H, 10);
    gui_rounded(&screen, x, y, TOAST_W, TOAST_H, 12, theme->panel);
    gui_fill(&screen, x, y + 12, 4, TOAST_H - 24, theme->accent);
    icon_draw(&screen, ICON_LOGO, x + 14, y + 14, 48);
    gui_text_bold(&screen, x + 74, y + 16, "AREOS", theme->accent, 1);
    gui_text_wrapped(&screen, x + 74, y + 38, TOAST_W - 90, toast_text, theme->text, 1);
}

void shell_init(void) {
    update_clock();
}
