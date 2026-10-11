// setup.c - the setup screens you see the first time: welcome, your name,
// language, look, how much to describe, and "all set".

#include "desktop.h"

enum page { WELCOME, NAME, LANGUAGE, APPEARANCE, ACCESSIBILITY, DONE, PAGE_COUNT };

enum { ACT_NEXT = 1, ACT_BACK, ACT_NAME, ACT_ENGLISH, ACT_SPANISH, ACT_DARK, ACT_LIGHT,
       ACT_DETAILED, ACT_BRIEF, ACT_FINISH };

static bool active = false;
static enum page page = WELCOME;
static struct gui_item items[8];
static int count = 0;
static int focus = 0;
static int hovered = -1;
// (shift_held is shared with the rest of the desktop: see desktop.h)

#define CARD_W 860
#define CARD_H 600
#define BUTTON_W 240
#define BUTTON_H 52
#define CHOICE_H 50

static int card_x(void) { return (screen.width - CARD_W) / 2; }
static int card_y(void) { return (screen.height - CARD_H) / 2; }

static struct gui_item *add(const char *name, enum gui_role role, int action, int x, int y, int w, int h) {
    struct gui_item *it = &items[count++];
    *it = (struct gui_item){ .name = name, .role = role, .action = action, .x = x, .y = y, .w = w, .h = h };
    return it;
}

static void add_back_next(const char *next, int action, bool with_back) {
    int y = card_y() + CARD_H - BUTTON_H - 36;
    if (with_back) add("Back", GUI_ROLE_BUTTON, ACT_BACK, card_x() + 48, y, BUTTON_W, BUTTON_H);
    add(next, GUI_ROLE_BUTTON, action, card_x() + CARD_W - 48 - BUTTON_W, y, BUTTON_W, BUTTON_H);
}

static void add_choice(const char *name, int action, bool checked, int index) {
    add(name, GUI_ROLE_RADIO, action, card_x() + 48, card_y() + 310 + index * (CHOICE_H + 12),
        CARD_W - 96, CHOICE_H)->checked = checked;
}

static void build(void) {
    count = 0;
    bool light = light_mode;
    switch (page) {
    case WELCOME:
        add_back_next("Get started", ACT_NEXT, false);
        break;
    case NAME:
        add("Your name", GUI_ROLE_TEXT_FIELD, ACT_NAME, card_x() + 48, card_y() + 310, CARD_W - 96, 60)->value = user_name;
        add_back_next("Next", ACT_NEXT, true);
        break;
    case LANGUAGE:
        add_choice("English", ACT_ENGLISH, true, 0);
        add_choice("Spanish (coming soon)", ACT_SPANISH, false, 1);
        items[count - 1].disabled = true;
        add_back_next("Next", ACT_NEXT, true);
        break;
    case APPEARANCE:
        add_choice("Dark mode", ACT_DARK, !light, 0);
        add_choice("Light mode", ACT_LIGHT, light, 1);
        add_back_next("Next", ACT_NEXT, true);
        break;
    case ACCESSIBILITY:
        add_choice("Detailed: names, positions and hints", ACT_DETAILED, detailed, 0);
        add_choice("Brief: just names", ACT_BRIEF, !detailed, 1);
        add_back_next("Next", ACT_NEXT, true);
        break;
    case DONE:
    default:
        add_back_next("Start using AREOS", ACT_FINISH, true);
        break;
    }
    if (focus >= count) focus = count - 1;
    if (focus < 0) focus = 0;
}

static const char *title(void) {
    switch (page) {
    case WELCOME:       return "Welcome to AREOS";
    case NAME:          return "What should we call you?";
    case LANGUAGE:      return "Choose your language";
    case APPEARANCE:    return "Pick a look";
    case ACCESSIBILITY: return "How much should AREOS describe?";
    default:            return "You're all set";
    }
}

static const char *body(void) {
    switch (page) {
    case WELCOME:
        return "It can run Doom. That's it. Let's set a few things up first; it takes about a minute. "
               "Everything here is also described in text, for screen readers.";
    case NAME:
        return "This is just for greetings. Type your name, or leave it blank.";
    case LANGUAGE:
        return "More languages are on the way.";
    case APPEARANCE:
        return "We've defaulted to dark mode, because bright screens can be hard on the eyes. "
               "You can change this anytime in Settings.";
    case ACCESSIBILITY:
        return "AREOS reads the screen out loud, and describes it in text for screen readers "
               "like NVDA. Choose how much it says.";
    default:
        return "The fourth OS is ready. Press Start to go to your desktop.";
    }
}

void setup_announce(void) {
    gui_line_start();
    gui_line_add("Setup, step ");
    gui_line_add_number((int)page + 1);
    gui_line_add(" of ");
    gui_line_add_number(PAGE_COUNT);
    gui_line_add(": ");
    const char *t = title();
    gui_line_add(t);
    char last = t[text_length(t) - 1];
    gui_line_add((last == '?' || last == '!' || last == '.') ? " " : ". ");
    gui_line_add(body());
    gui_line_say();
    if (page == WELCOME && detailed) gui_announce("Tip: press F1 any time to hear the keys.");
    gui_announce_item(&items[focus], focus + 1, count, detailed);
}

static void go_to(enum page p) {
    page = p;
    focus = 0;
    hovered = -1;
    build();
    for (int i = 0; i < count; i++) {   // start on the main choice, not "Back"
        if (items[i].action != ACT_BACK) { focus = i; break; }
    }
    request_redraw();
    setup_announce();
}

bool setup_active(void) {
    return active;
}

void setup_begin(void) {
    active = true;
    // First words a screen reader user hears: how to turn the voice off.
    if (speech_on()) {
        areos_speech_stop();
        gui_announce("AREOS is running with a screen reader. Press Control plus Alt plus S to turn it off.");
    }
    go_to(WELCOME);
}

static bool quiet = false;   // the page is about to change: don't describe the old choice

static void choose(int index) {
    struct gui_item *it = &items[index];
    focus = index;
    if (it->disabled) {
        gui_line_start();
        gui_line_add(it->name);
        gui_line_add(" isn't available yet.");
        gui_line_say();
        return;
    }
    switch (it->action) {
    case ACT_NEXT: case ACT_NAME: go_to((enum page)(page + 1)); return;
    case ACT_BACK: go_to((enum page)(page - 1)); return;
    case ACT_DARK: apply_theme(false); break;
    case ACT_LIGHT: apply_theme(true); break;
    case ACT_DETAILED: apply_detailed(true); break;
    case ACT_BRIEF: apply_detailed(false); break;
    case ACT_FINISH:
        save_name();
        areos_setting_set(AREOS_SETTING_SETUP_DONE, 1);
        active = false;
        desktop_enter(true);
        return;
    default: break;
    }
    build();
    request_redraw();
    if (!quiet) gui_announce_item(&items[focus], focus + 1, count, detailed);
}

void setup_key(struct areos_key_event *e) {
    if (e->key == AREOS_KEY_SHIFT) { shift_held = e->pressed; return; }
    if (!e->pressed) return;
    build();

    // Typing your name.
    if (items[focus].role == GUI_ROLE_TEXT_FIELD) {
        int length = text_length(user_name);
        if (e->key == AREOS_KEY_BACKSPACE) {
            if (length > 0) { user_name[length - 1] = '\0'; gui_echo_backspace(); request_redraw(); }
            return;
        }
        char c = e->character;
        if (c >= 32 && c <= 126) {
            if (length < 32) {
                user_name[length] = c;
                user_name[length + 1] = '\0';
                gui_echo_char(c);
                request_redraw();
            }
            return;
        }
    }

    switch (e->key) {
    case AREOS_KEY_TAB:
        focus = (focus + (shift_held ? count - 1 : 1)) % count;
        request_redraw();
        gui_announce_item(&items[focus], focus + 1, count, detailed);
        break;
    case AREOS_KEY_DOWN: case AREOS_KEY_RIGHT:
        focus = (focus + 1) % count;
        request_redraw();
        gui_announce_item(&items[focus], focus + 1, count, detailed);
        break;
    case AREOS_KEY_UP: case AREOS_KEY_LEFT:
        focus = (focus + count - 1) % count;
        request_redraw();
        gui_announce_item(&items[focus], focus + 1, count, detailed);
        break;
    case AREOS_KEY_ENTER:
        // On a choice, Enter picks it and moves on: the defaults are just Enter, Enter, Enter.
        if (items[focus].role == GUI_ROLE_RADIO && !items[focus].disabled) {
            quiet = true;
            choose(focus);
            quiet = false;
            go_to((enum page)(page + 1));
        } else {
            choose(focus);
        }
        break;
    case AREOS_KEY_SPACE:
        choose(focus);
        break;
    case AREOS_KEY_ESCAPE:
        if (page > WELCOME) go_to((enum page)(page - 1));
        break;
    case AREOS_KEY_F1:
        gui_announce("Keys: Tab or the arrow keys move. Space chooses. Enter chooses and moves to the next page. "
                     "Escape goes back. F2 says where you are.");
        break;
    case AREOS_KEY_F1 + 1:
        setup_announce();
        break;
    default:
        break;
    }
}

static int item_at(int x, int y) {
    for (int i = 0; i < count; i++) {
        if (x >= items[i].x && x < items[i].x + items[i].w && y >= items[i].y && y < items[i].y + items[i].h) return i;
    }
    return -1;
}

void setup_mouse_move(void) {
    build();
    int under = item_at(mouse_x, mouse_y);
    if (under != hovered) {
        hovered = under;
        if (under >= 0) {
            gui_line_start();
            gui_line_add("Mouse: ");
            gui_line_add(items[under].name);
            gui_line_say();
        }
        request_redraw();
    }
}

void setup_click(void) {
    build();
    int under = item_at(mouse_x, mouse_y);
    if (under < 0) return;
    if (items[under].role == GUI_ROLE_TEXT_FIELD) {
        focus = under;
        gui_announce_item(&items[focus], focus + 1, count, detailed);
        request_redraw();
    } else {
        choose(under);
    }
}

void setup_draw(void) {
    build();
    wallpaper_draw(&screen);
    int x = card_x(), y = card_y();
    gui_shadow(&screen, x, y, CARD_W, CARD_H, 18);
    gui_rounded_alpha(&screen, x, y, CARD_W, CARD_H, 16, theme->panel, 240);

    // "Step 3 of 6" and a progress bar.
    char step[16] = "Step ";
    text_append_number(step, (int)page + 1, sizeof(step));
    text_append(step, " of 6", sizeof(step));
    gui_text(&screen, x + 48, y + 38, step, theme->text_dim, 2);
    int bar_x = x + 48 + gui_text_width(step, 2) + 32, bar_w = CARD_W - 96 - gui_text_width(step, 2) - 32;
    gui_rounded(&screen, bar_x, y + 48, bar_w, 8, 4, theme->panel_border);
    gui_rounded(&screen, bar_x, y + 48, bar_w * ((int)page + 1) / PAGE_COUNT, 8, 4, theme->accent);

    if (page == WELCOME) icon_draw(&screen, ICON_LOGO, x + CARD_W - 48 - 120, y + 90, 120);
    gui_text_bold(&screen, x + 48, y + 96, title(), theme->accent, 3);
    gui_text_wrapped(&screen, x + 48, y + 170, page == WELCOME ? CARD_W - 260 : CARD_W - 96, body(), theme->text, 2);

    if (page == DONE && user_name[0] != '\0') {
        char greeting[64] = "Welcome aboard, ";
        text_append(greeting, user_name, sizeof(greeting));
        text_append(greeting, ".", sizeof(greeting));
        gui_text_bold(&screen, x + 48, y + 330, greeting, theme->accent2, 3);
    }

    for (int i = 0; i < count; i++) {
        struct gui_item *it = &items[i];
        bool f = i == focus, h = i == hovered;
        if (it->role == GUI_ROLE_TEXT_FIELD) {
            gui_rounded(&screen, it->x, it->y, it->w, it->h, 10, theme->background);
            gui_frame(&screen, it->x, it->y, it->w, it->h, f ? 3 : 1, f ? theme->accent : theme->panel_border);
            int ty = it->y + (it->h - 32) / 2;
            if (user_name[0] == '\0' && !f) gui_text(&screen, it->x + 20, ty, "Your name", theme->text_dim, 2);
            gui_text(&screen, it->x + 20, ty, user_name, theme->text, 2);
            if (f) gui_fill(&screen, it->x + 22 + gui_text_width(user_name, 2), ty + 4, 4, 24, theme->accent);
        } else if (it->role == GUI_ROLE_RADIO) {
            gui_rounded(&screen, it->x, it->y, it->w, it->h, 10, f ? theme->selected : (h ? theme->hover : theme->background));
            if (f) gui_frame(&screen, it->x, it->y, it->w, it->h, 3, theme->accent);
            int cx = it->x + 30, cy = it->y + it->h / 2;
            gui_ring(&screen, cx, cy, 12, 3, it->checked ? theme->accent : theme->text_dim);
            if (it->checked) gui_circle(&screen, cx, cy, 5, theme->accent);
            gui_text(&screen, it->x + 60, it->y + (it->h - 32) / 2, it->name,
                     it->disabled ? theme->text_dim : theme->text, 2);
        } else {
            bool primary = it->action != ACT_BACK;
            uint32_t fill = primary ? theme->accent : (h ? theme->hover : theme->background);
            gui_rounded(&screen, it->x, it->y, it->w, it->h, 12, fill);
            if (f) gui_frame(&screen, it->x - 3, it->y - 3, it->w + 6, it->h + 6, 3, theme->accent2);
            gui_text_centered(&screen, it->x + it->w / 2, it->y + (it->h - 32) / 2, it->name,
                              primary ? 0x0A0F14 : theme->text, 2);
        }
    }
}
