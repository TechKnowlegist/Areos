// app_settings.c - the Settings window.

#include "desktop.h"

enum { DARK, LIGHT, NEBULA, PLANET, PLAIN, DETAILED, BRIEF, RUN_SETUP };

#define ROW_W 512
#define ROW_H 34

static int build(struct gui_item *items, int max) {
    (void)max;
    bool light = theme == &gui_theme_light;
    int n = 0;
    struct { const char *name; int y; bool checked; } radios[] = {
        { "Dark mode",            44, !light },
        { "Light mode",           82, light },
        { "Nebula",              160, wallpaper_choice == WALLPAPER_NEBULA },
        { "Red planet",          198, wallpaper_choice == WALLPAPER_PLANET },
        { "Plain",               236, wallpaper_choice == WALLPAPER_PLAIN },
        { "Detailed descriptions", 314, detailed },
        { "Brief descriptions",  352, !detailed },
    };
    for (int i = 0; i < 7; i++) {
        items[n++] = (struct gui_item){ .name = radios[i].name, .role = GUI_ROLE_RADIO,
            .action = ACTION(A_CONTENT, i), .checked = radios[i].checked,
            .x = 24, .y = radios[i].y, .w = ROW_W, .h = ROW_H };
    }
    items[n++] = (struct gui_item){ .name = "Run setup again", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_CONTENT, RUN_SETUP), .x = 24, .y = 410, .w = 200, .h = 38 };
    return n;
}

static void draw(struct gui_canvas *c, struct gui_item *items, int count, int focus) {
    gui_text_bold(c, 24, 16, "Look", theme->accent, 1);
    gui_text_bold(c, 24, 132, "Wallpaper", theme->accent, 1);
    gui_text_bold(c, 24, 286, "Descriptions for screen readers", theme->accent, 1);
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
    case RUN_SETUP:
        gui_announce("Starting setup again.");
        setup_begin();
        break;
    }
}

const struct window_app app_settings = {
    .title = "Settings", .icon = ICON_SETTINGS, .width = 560, .height = 470,
    .build = build, .draw = draw, .key = key, .choose = choose,
};
