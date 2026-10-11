// app_settings.c - the Settings window.
//
// Like the Settings app on other systems, it's split into categories,
// listed down the left side as tabs: System, Personalization, Sound,
// Accessibility, Privacy and About. Up and Down move between categories
// (the page changes as you go), Tab moves into the page.

#include "desktop.h"

enum page { SYSTEM, PERSONALIZATION, SOUND, ACCESSIBILITY, PRIVACY, ABOUT, PAGE_COUNT };

static const char *page_names[PAGE_COUNT] = {
    "System", "Personalization", "Sound", "Accessibility", "Privacy", "About",
};

// What each page is for, said when you arrive on it.
static const char *page_descriptions[PAGE_COUNT] = {
    "System: your name, the clock, and running setup again.",
    "Personalization: dark or light mode, the wallpaper and the accent colour.",
    "Sound: how loud AREOS is.",
    "Accessibility: the screen reader, its voice and speed, how much AREOS describes, "
        "and the mouse pointer size.",
    "Privacy: AREOS collects nothing about you. No telemetry, no tracking, no accounts, no ads. "
        "Everything stays on this computer.",
    "About: AREOS version 0.1. It can run Doom. That's it. A hobby operating system built from scratch. "
        "The voices are Flite, from Carnegie Mellon University.",
};

static int page = SYSTEM;

// What each item does (the index passed to choose()).
enum {
    // 0-5: the category tabs
    NAME = 10, CLOCK_12, CLOCK_24, RUN_SETUP,
    DARK = 20, LIGHT, WALL_NEBULA, WALL_PLANET, WALL_PLAIN,
    ACCENT_FIRST = 25,                       // 25-29
    VOLUME = 30, TEST_SOUND,
    SPEECH = 40, VOICE_PICK = 41,
    SPEED = 45, DETAILED, BRIEF, POINTER_FIRST = 48,   // 48-50
};

static const char *accent_names[5] = { "Green", "Blue", "Purple", "Orange", "Pink" };
static const uint32_t accent_swatches[5] = { 0x3DFF9A, 0x4FC3F7, 0xB388FF, 0xFFAB40, 0xFF80AB };
#define VOICE_COUNT 10
static const char *voice_names[VOICE_COUNT] = {
    "Sally (US English)", "Rob (US English)", "Alan (Scottish English)", "Kal (classic robot)",
    "Ivy (higher Sally)", "Sam (deeper Sally)", "Finn (higher Rob)", "Dan (deeper Rob)",
    "Angus (deeper Alan)", "Kit (higher Kal)",
};
static const char *pointer_names[3] = { "Normal pointer", "Large pointer", "Extra large pointer" };

#define TAB_X 12
#define TAB_W 196
#define TAB_H 40
#define PAGE_X 236
#define RIGHT_X 564
#define ROW_W 300
#define ROW_H 34

static char volume_text[16], speed_text[16];

static int current_voice(void) {
    uint64_t v = areos_setting_get(AREOS_SETTING_SPEECH_VOICE);
    return v < VOICE_COUNT ? (int)v : 0;
}

static void add(struct gui_item *items, int *n, const char *name, enum gui_role role, int what,
                bool checked, int x, int y, int w, int h) {
    items[(*n)++] = (struct gui_item){ .name = name, .role = role, .action = ACTION(A_CONTENT, what),
        .checked = checked, .x = x, .y = y, .w = w, .h = h };
}

static int build(struct gui_item *items, int max) {
    (void)max;
    int n = 0;
    for (int p = 0; p < PAGE_COUNT; p++) {
        add(items, &n, page_names[p], GUI_ROLE_TAB, p, p == page, TAB_X, 16 + p * (TAB_H + 6), TAB_W, TAB_H);
    }
    switch (page) {
    case SYSTEM:
        add(items, &n, "Your name", GUI_ROLE_TEXT_FIELD, NAME, false, PAGE_X, 100, 360, 36);
        items[n - 1].value = user_name;
        add(items, &n, "12-hour clock (3:45 PM)", GUI_ROLE_RADIO, CLOCK_12, !clock_24h, PAGE_X, 196, ROW_W, ROW_H);
        add(items, &n, "24-hour clock (15:45)", GUI_ROLE_RADIO, CLOCK_24, clock_24h, PAGE_X, 234, ROW_W, ROW_H);
        add(items, &n, "Run setup again", GUI_ROLE_BUTTON, RUN_SETUP, false, PAGE_X, 312, 200, 38);
        break;
    case PERSONALIZATION:
        add(items, &n, "Dark mode", GUI_ROLE_RADIO, DARK, !light_mode, PAGE_X, 96, ROW_W, ROW_H);
        add(items, &n, "Light mode", GUI_ROLE_RADIO, LIGHT, light_mode, PAGE_X, 134, ROW_W, ROW_H);
        add(items, &n, "Nebula", GUI_ROLE_RADIO, WALL_NEBULA, wallpaper_choice == WALLPAPER_NEBULA, PAGE_X, 212, ROW_W, ROW_H);
        add(items, &n, "Red planet", GUI_ROLE_RADIO, WALL_PLANET, wallpaper_choice == WALLPAPER_PLANET, PAGE_X, 250, ROW_W, ROW_H);
        add(items, &n, "Plain", GUI_ROLE_RADIO, WALL_PLAIN, wallpaper_choice == WALLPAPER_PLAIN, PAGE_X, 288, ROW_W, ROW_H);
        for (int a = 0; a < 5; a++) {
            add(items, &n, accent_names[a], GUI_ROLE_RADIO, ACCENT_FIRST + a, accent_choice == a,
                RIGHT_X, 96 + a * 38, ROW_W - 20, ROW_H);
        }
        break;
    case SOUND:
        volume_text[0] = '\0';
        text_append_number(volume_text, volume_level(), sizeof(volume_text));
        text_append(volume_text, "%", sizeof(volume_text));
        add(items, &n, "Volume", GUI_ROLE_SLIDER, VOLUME, false, PAGE_X, 120, 420, 30);
        items[n - 1].value = volume_text;
        add(items, &n, "Play a test sound", GUI_ROLE_BUTTON, TEST_SOUND, false, PAGE_X, 180, 220, 38);
        break;
    case ACCESSIBILITY: {
        add(items, &n, "Screen reader", GUI_ROLE_SWITCH, SPEECH, speech_on(), PAGE_X, 96, ROW_W, 40);
        int voice = current_voice();
        add(items, &n, "Voice", GUI_ROLE_SLIDER, VOICE_PICK, false, PAGE_X, 188, ROW_W, 30);
        items[n - 1].value = voice_names[voice];
        speed_text[0] = '\0';
        text_append_number(speed_text, speech_speed(), sizeof(speed_text));
        text_append(speed_text, "%", sizeof(speed_text));
        add(items, &n, "Speech speed", GUI_ROLE_SLIDER, SPEED, false, PAGE_X, 262, ROW_W, 30);
        items[n - 1].value = speed_text;
        add(items, &n, "Detailed descriptions", GUI_ROLE_RADIO, DETAILED, detailed, RIGHT_X, 96, ROW_W - 20, ROW_H);
        add(items, &n, "Brief descriptions", GUI_ROLE_RADIO, BRIEF, !detailed, RIGHT_X, 134, ROW_W - 20, ROW_H);
        for (int p = 0; p < 3; p++) {
            add(items, &n, pointer_names[p], GUI_ROLE_RADIO, POINTER_FIRST + p, mouse_size == p,
                RIGHT_X, 212 + p * 38, ROW_W - 20, ROW_H);
        }
        break;
    }
    default:
        break;   // Privacy and About are just words
    }
    return n;
}

static void heading(struct gui_canvas *c, int x, int y, const char *text) {
    gui_text_bold(c, x, y, text, theme->accent, 1);
}

static void draw(struct gui_canvas *c, struct gui_item *items, int count, int focus) {
    // The sidebar.
    gui_fill(c, 0, 0, PAGE_X - 16, c->height, theme->background);
    gui_fill(c, PAGE_X - 16, 0, 1, c->height, theme->panel_border);
    for (int i = 0; i < count; i++) {
        struct gui_item *it = &items[i];
        bool focused = i == focus, hovered = i == hovered_window_item;
        if (it->role == GUI_ROLE_TAB) {
            if (it->checked || focused || hovered) {
                gui_rounded(c, it->x, it->y, it->w, it->h, 8, it->checked ? theme->selected : theme->hover);
            }
            if (it->checked) gui_rounded(c, it->x, it->y + 10, 4, it->h - 20, 2, theme->accent);
            gui_text(c, it->x + 16, it->y + (it->h - 16) / 2, it->name, theme->text, 1);
            if (focused) gui_frame(c, it->x, it->y, it->w, it->h, 2, theme->accent);
        } else if (it->role == GUI_ROLE_SLIDER) {
            int what = ACTION_INDEX(it->action);
            int permille = what == VOLUME ? volume_level() * 10
                         : what == VOICE_PICK ? current_voice() * 1000 / (VOICE_COUNT - 1)
                         : (speech_speed() - 50) * 1000 / 250;
            shell_draw_slider(c, it, permille, focused, hovered);
        } else if (it->role == GUI_ROLE_SWITCH) {
            if (focused || hovered) gui_rounded(c, it->x, it->y, it->w, it->h, 8, focused ? theme->selected : theme->hover);
            gui_text(c, it->x + 12, it->y + (it->h - 16) / 2, it->name, theme->text, 1);
            // The switch itself: a pill with a knob, on the right.
            int sx = it->x + it->w - 64, sy = it->y + (it->h - 22) / 2;
            gui_rounded(c, sx, sy, 48, 22, 11, it->checked ? theme->accent : theme->panel_border);
            gui_circle(c, it->checked ? sx + 37 : sx + 11, sy + 11, 8, it->checked ? theme->panel : theme->text);
            gui_text(c, sx - 34, it->y + (it->h - 16) / 2, it->checked ? "On" : "Off", theme->text_dim, 1);
            if (focused) gui_frame(c, it->x, it->y, it->w, it->h, 2, theme->accent);
        } else if (it->role == GUI_ROLE_TEXT_FIELD) {
            gui_rounded(c, it->x, it->y, it->w, it->h, 6, theme->background);
            gui_frame(c, it->x, it->y, it->w, it->h, focused ? 2 : 1, focused ? theme->accent : theme->panel_border);
            const char *v = it->value && it->value[0] ? it->value : "Type your name";
            gui_text(c, it->x + 10, it->y + 10, v, it->value && it->value[0] ? theme->text : theme->text_dim, 1);
            if (focused) {
                int cx = it->x + 10 + gui_text_width(it->value ? it->value : "", 1);
                gui_fill(c, cx + 1, it->y + 9, 2, 18, theme->accent);
            }
        } else {
            draw_window_item(c, it, focused, hovered);
            // Accent radios get a swatch of their colour.
            int what = ACTION_INDEX(it->action);
            if (what >= ACCENT_FIRST && what < ACCENT_FIRST + 5) {
                gui_rounded(c, it->x + it->w - 36, it->y + 7, 24, it->h - 14, 5, accent_swatches[what - ACCENT_FIRST]);
            }
        }
    }

    // Page title and the words for each page.
    gui_text_bold(c, PAGE_X, 18, page_names[page], theme->text, 2);
    switch (page) {
    case SYSTEM:
        heading(c, PAGE_X, 76, "Your name (for greetings)");
        heading(c, PAGE_X, 170, "Clock");
        heading(c, PAGE_X, 286, "Setup");
        break;
    case PERSONALIZATION:
        heading(c, PAGE_X, 70, "Theme");
        heading(c, PAGE_X, 186, "Wallpaper");
        heading(c, RIGHT_X, 70, "Accent colour");
        break;
    case SOUND:
        heading(c, PAGE_X, 70, "Main volume");
        gui_text_wrapped(c, PAGE_X, 240, 560,
                         "AREOS plays sound through an AC'97 sound card. The screen reader's voice "
                         "uses this volume too.", theme->text_dim, 1);
        break;
    case ACCESSIBILITY:
        heading(c, PAGE_X, 70, "Built-in screen reader (Ctrl+Alt+S)");
        gui_text(c, PAGE_X, 336, "Voice: Left and Right change it. 10 voices in all.", theme->text_dim, 1);
        heading(c, RIGHT_X, 70, "Descriptions");
        heading(c, RIGHT_X, 186, "Mouse pointer size");
        break;
    default:
        gui_text_wrapped(c, PAGE_X, 76, c->width - PAGE_X - 24, page_descriptions[page], theme->text, 1);
        if (page == ABOUT) {
            char uptime[64] = "Running for ";
            uint64_t seconds = areos_time_ms() / 1000;
            text_append_number(uptime, (int64_t)(seconds / 60), sizeof(uptime));
            text_append(uptime, " min ", sizeof(uptime));
            text_append_number(uptime, (int64_t)(seconds % 60), sizeof(uptime));
            text_append(uptime, " s.", sizeof(uptime));
            gui_text(c, PAGE_X, 180, uptime, theme->text_dim, 1);
            icon_draw(c, ICON_LOGO, c->width - 140, c->height - 150, 110);
        } else {
            gui_text_wrapped(c, PAGE_X, 160, c->width - PAGE_X - 24,
                             "Your settings are kept in memory until AREOS turns off.",
                             theme->text_dim, 1);
        }
        break;
    }
}

static void show_page(int p, bool announce) {
    page = p;
    request_redraw();
    if (announce) gui_announce(page_descriptions[p]);
}

void settings_show_page(int p) {
    page = p;
    open_window(WIN_SETTINGS);
}

// Typing into the name field.
static bool type_name(struct areos_key_event *e) {
    int length = text_length(user_name);
    if (e->key == AREOS_KEY_BACKSPACE) {
        if (length > 0) {
            user_name[length - 1] = '\0';
            gui_echo_backspace();
            save_name();
            request_redraw();
        }
        return true;
    }
    if (e->key == AREOS_KEY_ENTER) {
        gui_line_start();
        gui_line_add(user_name[0] ? "Name saved: " : "Name cleared.");
        if (user_name[0]) gui_line_add(user_name);
        gui_line_say();
        return true;
    }
    char ch = e->character;
    if (ch >= 32 && ch <= 126) {
        if (length < 32) {
            user_name[length] = ch;
            user_name[length + 1] = '\0';
            gui_echo_char(ch);
            save_name();
            request_redraw();
        }
        return true;
    }
    return false;
}

static bool key(struct areos_key_event *e, struct gui_item *items, int count, int *focus) {
    if (*focus < 0 || *focus >= count) return false;
    struct gui_item *it = &items[*focus];
    // Up and Down on the categories switch pages as you go, like a tab list.
    if (it->role == GUI_ROLE_TAB && (e->key == AREOS_KEY_UP || e->key == AREOS_KEY_DOWN)) {
        int p = ACTION_INDEX(it->action) + (e->key == AREOS_KEY_DOWN ? 1 : -1);
        if (p < 0) p = PAGE_COUNT - 1;
        if (p >= PAGE_COUNT) p = 0;
        show_page(p, false);
        *focus = p;
        struct gui_item fresh[48];
        int n = build(fresh, 48);
        gui_announce_item(&fresh[p], p + 1, n, detailed);
        gui_announce(page_descriptions[p]);
        return true;
    }
    // Tab from the categories goes straight into the page (not through
    // every other category), and Shift+Tab from the page comes back.
    if (e->key == AREOS_KEY_TAB && !shift_held && it->role == GUI_ROLE_TAB) {
        *focus = PAGE_COUNT < count ? PAGE_COUNT : 0;
        gui_announce_item(&items[*focus], *focus + 1, count, detailed);
        request_redraw();
        return true;
    }
    if (e->key == AREOS_KEY_TAB && shift_held && *focus == PAGE_COUNT) {
        *focus = page;
        gui_announce_item(&items[*focus], *focus + 1, count, detailed);
        request_redraw();
        return true;
    }
    if (it->role == GUI_ROLE_TEXT_FIELD && ACTION_INDEX(it->action) == NAME) {
        return type_name(e);
    }
    return false;
}

static int slider_value(int current, int low, int high, int step_size) {
    int value = current;
    if (slider_click >= 0) value = low + slider_click * (high - low) / 1000;
    else value = current + slider_step * step_size;
    if (value < low) value = low;
    if (value > high) value = high;
    return value;
}

static void choose(int what) {
    if (what >= 0 && what < PAGE_COUNT) {
        show_page(what, true);
        return;
    }
    switch (what) {
    case NAME:        gui_announce("Type your name, then press Enter."); break;
    case CLOCK_12:    apply_clock_24h(false); break;
    case CLOCK_24:    apply_clock_24h(true); break;
    case RUN_SETUP:
        gui_announce("Starting setup again.");
        setup_begin();
        break;
    case DARK:        apply_theme(false); break;
    case LIGHT:       apply_theme(true); break;
    case WALL_NEBULA: apply_wallpaper(WALLPAPER_NEBULA); break;
    case WALL_PLANET: apply_wallpaper(WALLPAPER_PLANET); break;
    case WALL_PLAIN:  apply_wallpaper(WALLPAPER_PLAIN); break;
    case VOLUME:      apply_volume(slider_value(volume_level(), 0, 100, 5)); break;
    case TEST_SOUND:  areos_speech_say("This is how loud AREOS is."); break;
    case SPEECH:      apply_speech(!speech_on()); break;
    case VOICE_PICK: {
        int v = slider_value(current_voice(), 0, VOICE_COUNT - 1, 1);
        if (slider_click >= 0) v = (slider_click * (VOICE_COUNT - 1) + 500) / 1000;
        areos_setting_set(AREOS_SETTING_SPEECH_VOICE, (uint64_t)v);
        areos_speech_stop();
        char hello[64];
        hello[0] = '\0';
        text_append(hello, voice_names[v], sizeof(hello));
        areos_speech_say(hello);
        break;
    }
    case SPEED:       apply_speech_speed(slider_value(speech_speed(), 50, 300, 10)); break;
    case DETAILED:    apply_detailed(true); break;
    case BRIEF:       apply_detailed(false); break;
    default:
        if (what >= ACCENT_FIRST && what < ACCENT_FIRST + 5) apply_accent(what - ACCENT_FIRST);
        else if (what >= POINTER_FIRST && what < POINTER_FIRST + 3) {
            apply_mouse_size(what - POINTER_FIRST);
        }
        break;
    }
    request_redraw();
}

static void opened(void) {
    show_page(page, false);
    windows[WIN_SETTINGS].focus = page;   // start on the current category's tab
}

const struct window_app app_settings = {
    .title = "Settings", .icon = ICON_SETTINGS, .width = 900, .height = 470, .resizable = true,
    .build = build, .draw = draw, .key = key, .choose = choose, .opened = opened,
};
