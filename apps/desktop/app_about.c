// app_about.c - the About AREOS window.

#include "desktop.h"

static const char *about_text =
    "A hobby operating system built from scratch. Not Linux, not Windows, not macOS: "
    "its own thing. No ads, no bloat, no telemetry.";

static char uptime_text[48];

static void update_uptime(void) {
    uint64_t seconds = areos_time_ms() / 1000;
    text_copy(uptime_text, "Running for ", sizeof(uptime_text));
    if (seconds >= 3600) {
        text_append_number(uptime_text, (int64_t)(seconds / 3600), sizeof(uptime_text));
        text_append(uptime_text, " h ", sizeof(uptime_text));
    }
    text_append_number(uptime_text, (int64_t)((seconds / 60) % 60), sizeof(uptime_text));
    text_append(uptime_text, " min ", sizeof(uptime_text));
    text_append_number(uptime_text, (int64_t)(seconds % 60), sizeof(uptime_text));
    text_append(uptime_text, " s", sizeof(uptime_text));
}

static int build(struct gui_item *items, int max) {
    (void)max;
    items[0] = (struct gui_item){ .name = "OK", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_CLOSE, WIN_ABOUT), .x = 520 - 24 - 120, .y = 300, .w = 120, .h = 38 };
    return 1;
}

static void draw(struct gui_canvas *c, struct gui_item *items, int count, int focus) {
    update_uptime();
    icon_draw(c, ICON_LOGO, 24, 20, 104);
    gui_text_bold(c, 148, 30, "AREOS", theme->accent, 3);
    gui_text(c, 150, 84, "It can run Doom. That's it.", theme->accent2, 1);
    gui_text(c, 150, 104, "Version 0.1  -  pronounced \"Arios\"", theme->text_dim, 1);
    gui_text_wrapped(c, 24, 150, 472, about_text, theme->text, 1);
    gui_text(c, 24, 230, uptime_text, theme->text_dim, 1);
    for (int i = 0; i < count; i++) {
        draw_window_item(c, &items[i], i == focus, i == hovered_window_item);
    }
}

static bool key(struct areos_key_event *e, struct gui_item *items, int count, int *focus) {
    (void)e; (void)items; (void)count; (void)focus;
    return false;
}

static void choose(int index) {
    (void)index;
}

static void opened(void) {
    update_uptime();
    gui_line_start();
    gui_line_add("AREOS version 0.1. It can run Doom. That's it. ");
    gui_line_add(about_text);
    gui_line_add(" ");
    gui_line_add(uptime_text);
    gui_line_add(".");
    gui_line_say();
}

const struct window_app app_about = {
    .title = "About AREOS", .icon = ICON_ABOUT, .width = 520, .height = 360,
    .build = build, .draw = draw, .key = key, .choose = choose, .opened = opened,
};
