// desktop.h - the parts of the AREOS desktop, and how they fit together.
//
//   main.c        start-up, the main loop, keyboard focus, announcements
//   setup.c       the setup screens shown first (welcome, name, look...)
//   wallpaper.c   painting the space wallpaper
//   icons.c       the little pictures (gear, calculator, rocket...)
//   shell.c       desktop icons, the taskbar, the AREOS launcher (start menu),
//                 quick settings, notifications
//   windows.c     opening, moving, stacking and animating windows
//   app_*.c       the built-in apps that live in windows
//
// Every clickable thing is a `struct gui_item` with a name and a role, so
// the keyboard, the mouse and the screen-reader text all agree on what's
// on screen.

#ifndef DESKTOP_H
#define DESKTOP_H

#include "areos.h"
#include "gui.h"

// ---- Shared state (defined in main.c) ----

extern struct gui_canvas screen;          // the whole frame, drawn every time
extern const struct gui_theme *theme;
extern bool detailed;                     // announce positions and hints?
extern bool dirty;                        // something changed: redraw
extern int mouse_x, mouse_y;
extern char user_name[33];
extern uint64_t now_ms;                   // time of this frame, for animations
extern bool light_mode;                   // light theme on?
extern int accent_choice;                 // 0 green, 1 blue, 2 purple, 3 orange, 4 pink
extern int mouse_size;                    // 0 normal, 1 large, 2 extra large
extern bool clock_24h;
extern bool shift_held;                   // Shift is held down right now
extern bool alt_held;                     // Alt is held down right now
// Where the mouse clicked inside a window item, in window content
// coordinates (-1 if it wasn't a click). Window apps read this in choose().
extern int click_x, click_y;

void request_redraw(void);

// ---- Icons ----

enum icon_kind {
    ICON_PROGRAM, ICON_SETTINGS, ICON_ABOUT, ICON_TERMINAL, ICON_CALCULATOR, ICON_NOTES,
    ICON_STARFIELD, ICON_HELLO, ICON_COUNTDOWN, ICON_OOPS, ICON_POWER, ICON_RESTART, ICON_LOGO,
    ICON_BROWSER, ICON_QUICK,
};

void icon_draw(struct gui_canvas *c, enum icon_kind kind, int x, int y, int size);
enum icon_kind icon_for_app(const char *label);

// ---- Wallpaper ----

enum wallpaper_kind { WALLPAPER_NEBULA, WALLPAPER_PLANET, WALLPAPER_PLAIN };

void wallpaper_init(int width, int height);
void wallpaper_build(enum wallpaper_kind kind, bool light);
void wallpaper_draw(struct gui_canvas *to);   // copy it onto a canvas

// ---- Things you can open ----

enum launch_type { LAUNCH_WINDOW, LAUNCH_APP, LAUNCH_COMMAND_LINE };

struct launchable {
    char label[40];
    enum icon_kind icon;
    enum launch_type type;
    int target;           // window kind, or app number
    char file[40];        // apps: "hello.arx"
    bool on_desktop;      // shown as a desktop icon (everything is in the Start menu)
};

#define MAX_LAUNCHABLES 32
extern struct launchable launchables[MAX_LAUNCHABLES];
extern int launchable_count;

void launch(int index);

// ---- Actions: what an item does when chosen ----
// action = kind * 1000 + index

enum action_kind {
    A_NONE = 0,
    A_LAUNCH,             // index = launchable
    A_START_BUTTON,
    A_TASKBAR_WINDOW,     // index = window kind
    A_RESTART,
    A_POWER_OFF,
    A_MINIMIZE,           // index = window kind
    A_MAXIMIZE,           // index = window kind
    A_CLOSE,              // index = window kind
    A_CONTENT,            // index = something inside a window (the window decides)
    A_QUICK_BUTTON,       // the quick settings button on the taskbar
    A_QUICK,              // index = something in quick settings
};

#define ACTION(kind, index) ((kind) * 1000 + (index))
#define ACTION_KIND(action) ((action) / 1000)
#define ACTION_INDEX(action) ((action) % 1000)

// ---- Keyboard focus ----
// The keyboard is always in one "zone"; F6 moves between them.

enum zone { ZONE_DESKTOP, ZONE_TASKBAR, ZONE_START, ZONE_WINDOW, ZONE_QUICK };

extern enum zone zone;
void set_zone(enum zone z, bool announce);
void announce_where(void);
void announce_item(struct gui_item *items, int count, int focus);

// ---- Windows ----

enum window_kind { WIN_SETTINGS, WIN_ABOUT, WIN_CALCULATOR, WIN_NOTES, WIN_BROWSER, WIN_COUNT };

#define TITLE_BAR_H 40
#define TITLE_ITEMS 3          // Minimize, Maximize and Close come last in a window's items

enum { SNAP_NONE, SNAP_MAXIMIZED, SNAP_LEFT, SNAP_RIGHT };

struct window {
    bool open;
    bool minimized;
    enum window_kind kind;
    const char *title;
    enum icon_kind icon;
    int x, y, w, h;
    int snap;                   // SNAP_NONE, SNAP_MAXIMIZED, SNAP_LEFT or SNAP_RIGHT
    int restore_x, restore_y, restore_w, restore_h;   // where it goes back to
    struct gui_canvas canvas;   // the window is drawn here, then onto the screen
    int focus;                  // which item inside has keyboard focus
    int anim;                   // which animation is playing (see windows.c)
    uint64_t anim_start;
};

extern struct window windows[WIN_COUNT];

void windows_init(void);
void window_open(enum window_kind kind);
void window_close(enum window_kind kind);
void window_minimize(enum window_kind kind);
void window_focus(enum window_kind kind);           // bring to front (and restore)
struct window *window_top(void);                    // the front window that's visible, or NULL
int window_order(enum window_kind order[WIN_COUNT]);  // open windows, back to front
bool windows_animating(void);
void windows_update(void);                          // advance animations
void windows_draw(void);                            // composite onto the screen

// Items inside a window, in window coordinates (content + title buttons).
int window_items(struct window *w, struct gui_item *items, int max);
// Which window (front-most) is at a screen point, or NULL.
struct window *window_at(int x, int y);
bool window_title_at(struct window *w, int x, int y);   // on its title bar (for dragging)
void window_moved(struct window *w);

// Window management: bigger, smaller, half the screen.
void window_set_snap(struct window *w, int snap);   // SNAP_*; SNAP_NONE restores
void window_toggle_maximize(struct window *w);
void window_snap_key(struct window *w, int direction);   // Windows-style: Left, Right, Up, Down
const char *window_snap_words(struct window *w);        // for announcing
void window_unsnap_for_drag(struct window *w, int mouse_x);
// The size of the content area of the window being built, drawn or updated.
extern int content_w, content_h;
void window_context(struct window *w);

// What each kind of window contains. Each app_*.c fills in one of these.
struct window_app {
    const char *title;
    enum icon_kind icon;
    int width, height;                     // content size (below the title bar)
    bool resizable;                        // can be maximized and snapped (layout follows content_w/content_h)
    int (*build)(struct gui_item *items, int max);              // items, content coordinates
    void (*draw)(struct gui_canvas *c, struct gui_item *items, int count, int focus);
    bool (*key)(struct areos_key_event *e, struct gui_item *items, int count, int *focus);
    void (*choose)(int content_index);     // an item with A_CONTENT was chosen
    void (*opened)(void);                  // just opened (optional)
    void (*tick)(void);                    // called every frame while open (optional)
};

extern const struct window_app app_settings, app_about, app_calculator, app_notes, app_browser;
const struct window_app *window_app_for(enum window_kind kind);

// Where in a slider the mouse clicked, 0 (left end) to 1000 (right end),
// or -1 if it wasn't a click. Window apps read this in choose().
extern int slider_click;
// Set while a slider is moved with the keyboard: -1 / +1 (or -5 / +5 for
// Page Down / Page Up). Window apps read this in choose().
extern int slider_step;

// Handy for apps: open a window and move keyboard focus into it.
void open_window(enum window_kind kind);
void settings_show_page(int page);   // Settings window, on one of its pages

// Shared drawing for buttons and radio buttons inside windows.
void draw_window_item(struct gui_canvas *c, const struct gui_item *item, bool focused, bool hovered);
extern int hovered_window_item;           // item under the mouse in the window being drawn

// ---- Desktop shell: icons, taskbar, Start menu, notifications ----

#define TASKBAR_H 52

void shell_init(void);
int desktop_items(struct gui_item *items, int max);
int taskbar_items(struct gui_item *items, int max);
int start_items(struct gui_item *items, int max);
void shell_draw_desktop_icons(int focus, int hovered);
void shell_draw_taskbar(int focus, int hovered);
void shell_draw_start(int focus, int hovered);
void start_open(void);
void start_close(void);
bool start_is_open(void);
bool start_animating(void);
bool start_contains(int x, int y);
void quick_open(void);
void quick_close(void);
bool quick_is_open(void);
bool quick_animating(void);
bool quick_contains(int x, int y);
int quick_items(struct gui_item *items, int max);
void shell_draw_quick(int focus, int hovered);
void quick_choose(int index);
// Draw a slider (used by quick settings and Settings). permille: 0-1000.
void shell_draw_slider(struct gui_canvas *c, const struct gui_item *item, int permille, bool focused, bool hovered);
bool quick_key(struct areos_key_event *e, int focus);   // sliders: Left/Right
void toast_show(const char *text);
bool toast_active(void);
void toast_draw(void);

// Go to the desktop (after setup, or straight away if setup was done before).
void desktop_enter(bool after_setup);

// ---- Setup screens ----

bool setup_active(void);
void setup_begin(void);
void setup_key(struct areos_key_event *e);
void setup_mouse_move(void);
void setup_click(void);
void setup_draw(void);
void setup_announce(void);

// ---- Settings shared with the setup screens ----

void apply_theme(bool light);
void apply_accent(int choice);
void apply_mouse_size(int size);
void apply_clock_24h(bool on);
void apply_speech(bool on);            // the built-in screen reader
void apply_speech_speed(int percent);
void apply_volume(int percent);
int speech_speed(void);                // 50-300
int volume_level(void);                // 0-100
bool speech_on(void);
void apply_wallpaper(int kind);
void apply_detailed(bool on);
extern int wallpaper_choice;
void save_name(void);

// ---- Small text helpers ----

int text_length(const char *s);
void text_copy(char *to, const char *from, int max);
void text_append(char *to, const char *from, int max);
void text_append_number(char *to, int64_t number, int max);
bool text_equal(const char *a, const char *b);
char to_lower(char c);

#endif
