// app_settings.c - the Settings window.

#include "desktop.h"

enum { DARK, LIGHT, NEBULA, PLANET, PLAIN, DETAILED, BRIEF, SPEAK, QUIET,
       SPEED_NORMAL, SPEED_FAST, SPEED_VERY_FAST, RUN_SETUP };

#define ROW_W 380
#define ROW_H 34
#define LEFT 24
#define RIGHT 450

static int build(struct gui_item *items, int max) {
    (void)max;
    bool light = theme == &gui_theme_light;
    int n = 0;
    bool quiet = areos_setting_get(AREOS_SETTING_SPEECH_OFF) != 0;
    uint64_t speed = areos_setting_get(AREOS_SETTING_SPEECH_SPEED);
    struct { const char *name; int x, y; bool checked; } radios[] = {
        { "Dark mode",              LEFT,   44, !light },
        { "Light mode",             LEFT,   82, light },
        { "Nebula",                 LEFT,  160, wallpaper_choice == WALLPAPER_NEBULA },
        { "Red planet",             LEFT,  198, wallpaper_choice == WALLPAPER_PLANET },
        { "Plain",                  LEFT,  236, wallpaper_choice == WALLPAPER_PLAIN },
        { "Detailed descriptions",  RIGHT,  44, detailed },
        { "Brief descriptions",     RIGHT,  82, !detailed },
        { "Speak out loud",         RIGHT, 160, !quiet },
        { "Quiet",                  RIGHT, 198, quiet },
        { "Normal speed",           RIGHT, 276, speed == 0 },
        { "Fast",                   RIGHT, 314, speed == 1 },
        { "Very fast",              RIGHT, 352, speed == 2 },
    };
    for (int i = 0; i < 12; i++) {
        items[n++] = (struct gui_item){ .name = radios[i].name, .role = GUI_ROLE_RADIO,
            .action = ACTION(A_CONTENT, i), .checked = radios[i].checked,
            .x = radios[i].x, .y = radios[i].y, .w = ROW_W, .h = ROW_H };
    }
    items[n++] = (struct gui_item){ .name = "Run setup again", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_CONTENT, RUN_SETUP), .x = LEFT, .y = 410, .w = 200, .h = 38 };
    return n;
}

static void draw(struct gui_canvas *c, struct gui_item *items, int count, int focus) {
    gui_text_bold(c, LEFT, 16, "Look", theme->accent, 1);
    gui_text_bold(c, LEFT, 132, "Wallpaper", theme->accent, 1);
    gui_text_bold(c, RIGHT, 16, "Descriptions for screen readers", theme->accent, 1);
    gui_text_bold(c, RIGHT, 132, "Voice (Ctrl+Alt+S turns it on or off)", theme->accent, 1);
    gui_text_bold(c, RIGHT, 248, "Speaking speed", theme->accent, 1);
    for (int i = 0; i < count; i++) {
        draw_window_item(c, &items[i], i == focus, i == hovered_window_item);
    }
}

static bool key(struct areos_key_event *e, struct gui_item *items, int count, int *focus) {
    (void)e; (void)items; (void)count; (void)focus;
    return false;  // nothing special: the usual keys do everything
}

static void choose(int index) {
    switch (index) {
    case DARK:     apply_theme(false); break;
    case LIGHT:    apply_theme(true); break;
    case NEBULA:   apply_wallpaper(WALLPAPER_NEBULA); break;
    case PLANET:   apply_wallpaper(WALLPAPER_PLANET); break;
    case PLAIN:    apply_wallpaper(WALLPAPER_PLAIN); break;
    case DETAILED: apply_detailed(true); break;
    case BRIEF:    apply_detailed(false); break;
    case SPEAK:    areos_setting_set(AREOS_SETTING_SPEECH_OFF, 0); break;
    case QUIET:    areos_setting_set(AREOS_SETTING_SPEECH_OFF, 1); break;
    case SPEED_NORMAL:    areos_setting_set(AREOS_SETTING_SPEECH_SPEED, 0); break;
    case SPEED_FAST:      areos_setting_set(AREOS_SETTING_SPEECH_SPEED, 1); break;
    case SPEED_VERY_FAST: areos_setting_set(AREOS_SETTING_SPEECH_SPEED, 2); break;
    case RUN_SETUP:
        gui_announce("Starting setup again.");
        setup_begin();
        break;
    }
}

const struct window_app app_settings = {
    .title = "Settings", .icon = ICON_SETTINGS, .width = 880, .height = 470,
    .build = build, .draw = draw, .key = key, .choose = choose,
};
