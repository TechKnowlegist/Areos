// windows.c - opening, moving, stacking and animating windows.
//
// Each window first draws itself into its own canvas (its own little
// picture). Then the windows are copied onto the screen from back to
// front: that's called "compositing". Because each window is a separate
// picture, it can be shrunk, grown or faded while it's copied, which is
// how the open, close and minimize animations work.

#include "desktop.h"

struct window windows[WIN_COUNT];

// Back-to-front order of open windows (the last one is in front).
static enum window_kind order[WIN_COUNT];
static int order_count = 0;

int hovered_window_item = -1;
int content_w = 0, content_h = 0;

void window_context(struct window *w) {
    content_w = w->w;
    content_h = w->h - TITLE_BAR_H;
}

enum animation { ANIM_NONE, ANIM_OPEN, ANIM_CLOSE, ANIM_MINIMIZE, ANIM_RESTORE };
#define ANIM_MS 200

const struct window_app *window_app_for(enum window_kind kind) {
    switch (kind) {
    case WIN_SETTINGS:   return &app_settings;
    case WIN_ABOUT:      return &app_about;
    case WIN_CALCULATOR: return &app_calculator;
    case WIN_NOTES:      return &app_notes;
    case WIN_BROWSER:    return &app_browser;
    default:             return &app_about;
    }
}

static const struct window_app *app_for(enum window_kind kind) {
    return window_app_for(kind);
}

void windows_init(void) {
    for (int k = 0; k < WIN_COUNT; k++) {
        const struct window_app *app = app_for((enum window_kind)k);
        struct window *w = &windows[k];
        w->kind = (enum window_kind)k;
        w->title = app->title;
        w->icon = app->icon;
        w->w = app->width;
        w->h = app->height + TITLE_BAR_H;
        w->canvas.width = w->w;
        w->canvas.height = w->h;
        // Windows that can be maximized get a canvas big enough for the whole screen.
        size_t pixels = app->resizable ? (size_t)screen.width * screen.height : (size_t)w->w * w->h;
        w->canvas.pixels = areos_more_memory(pixels * 4);
        // Start each kind of window in a slightly different place, like real
        // desktops do, so they don't all stack exactly on top of each other.
        w->x = (screen.width - w->w) / 2 + (k - 1) * 40;
        w->y = (screen.height - TASKBAR_H - w->h) / 2 + (k - 1) * 30;
        if (w->x + w->w > screen.width) w->x = screen.width - w->w;
        if (w->x < 0) w->x = 0;
        if (w->y + w->h > screen.height - TASKBAR_H) w->y = screen.height - TASKBAR_H - w->h;
        if (w->y < 10) w->y = 10;
    }
}

static void remove_from_order(enum window_kind kind) {
    int j = 0;
    for (int i = 0; i < order_count; i++) {
        if (order[i] != kind) order[j++] = order[i];
    }
    order_count = j;
}

static void bring_to_front(enum window_kind kind) {
    remove_from_order(kind);
    order[order_count++] = kind;
}

int window_order(enum window_kind out[WIN_COUNT]) {
    for (int i = 0; i < order_count; i++) out[i] = order[i];
    return order_count;
}

struct window *window_top(void) {
    for (int i = order_count - 1; i >= 0; i--) {
        struct window *w = &windows[order[i]];
        if (w->open && !w->minimized && w->anim != ANIM_CLOSE && w->anim != ANIM_MINIMIZE) return w;
    }
    return NULL;
}

static void start_animation(struct window *w, int anim) {
    w->anim = anim;
    w->anim_start = now_ms;
    request_redraw();
}

void window_open(enum window_kind kind) {
    struct window *w = &windows[kind];
    if (w->canvas.pixels == NULL) {
        return;
    }
    if (w->open) {
        window_focus(kind);   // already open: just bring it forward
        return;
    }
    w->open = true;
    w->minimized = false;
    w->focus = 0;
    bring_to_front(kind);
    start_animation(w, ANIM_OPEN);
    const struct window_app *app = app_for(kind);
    if (app->opened != NULL) app->opened();
}

void window_close(enum window_kind kind) {
    struct window *w = &windows[kind];
    if (!w->open) return;
    start_animation(w, ANIM_CLOSE);   // it really closes when the animation ends
}

void window_minimize(enum window_kind kind) {
    struct window *w = &windows[kind];
    if (!w->open || w->minimized) return;
    // When it comes back, start on its first item, not on the Minimize button.
    struct gui_item items[48];
    int count = window_items(w, items, 48);
    if (w->focus >= count - TITLE_ITEMS) w->focus = 0;
    start_animation(w, ANIM_MINIMIZE);
}

void window_focus(enum window_kind kind) {
    struct window *w = &windows[kind];
    if (!w->open) return;
    bring_to_front(kind);
    if (w->minimized) {
        w->minimized = false;
        start_animation(w, ANIM_RESTORE);
    }
    request_redraw();
}

bool windows_animating(void) {
    for (int k = 0; k < WIN_COUNT; k++) {
        if (windows[k].open && windows[k].anim != ANIM_NONE) return true;
    }
    return false;
}

void windows_update(void) {
    for (int k = 0; k < WIN_COUNT; k++) {
        struct window *w = &windows[k];
        if (!w->open || w->anim == ANIM_NONE) continue;
        if (now_ms - w->anim_start < ANIM_MS) continue;
        // The animation just finished.
        if (w->anim == ANIM_CLOSE) {
            w->open = false;
            remove_from_order(w->kind);
        } else if (w->anim == ANIM_MINIMIZE) {
            w->minimized = true;
        }
        w->anim = ANIM_NONE;
        request_redraw();
    }
}

// ---- Items inside a window ----

#define TITLE_BUTTON_W 46

int window_items(struct window *w, struct gui_item *items, int max) {
    const struct window_app *app = app_for(w->kind);
    window_context(w);
    int count = app->build(items, max - TITLE_ITEMS);
    // Content items are built in content coordinates: shift them below the title bar.
    for (int i = 0; i < count; i++) {
        items[i].y += TITLE_BAR_H;
    }
    // The title bar buttons come last, so Tab reaches the content first.
    struct gui_item *minimize = &items[count++];
    *minimize = (struct gui_item){ .name = "Minimize", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_MINIMIZE, w->kind), .x = w->w - 3 * TITLE_BUTTON_W, .y = 0,
        .w = TITLE_BUTTON_W, .h = TITLE_BAR_H };
    struct gui_item *maximize = &items[count++];
    *maximize = (struct gui_item){ .name = w->snap == SNAP_NONE ? "Maximize" : "Restore", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_MAXIMIZE, w->kind), .x = w->w - 2 * TITLE_BUTTON_W, .y = 0,
        .w = TITLE_BUTTON_W, .h = TITLE_BAR_H, .disabled = !app->resizable };
    struct gui_item *close = &items[count++];
    *close = (struct gui_item){ .name = "Close", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_CLOSE, w->kind), .x = w->w - TITLE_BUTTON_W, .y = 0,
        .w = TITLE_BUTTON_W, .h = TITLE_BAR_H };
    return count;
}

struct window *window_at(int x, int y) {
    for (int i = order_count - 1; i >= 0; i--) {
        struct window *w = &windows[order[i]];
        if (!w->open || w->minimized || w->anim == ANIM_CLOSE || w->anim == ANIM_MINIMIZE) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return w;
    }
    return NULL;
}

bool window_title_at(struct window *w, int x, int y) {
    int local_x = x - w->x, local_y = y - w->y;
    return local_y >= 0 && local_y < TITLE_BAR_H && local_x >= 0 && local_x < w->w - TITLE_ITEMS * TITLE_BUTTON_W;
}

void window_moved(struct window *w) {
    // Keep at least the title bar on screen, so it can always be dragged back.
    if (w->x < -w->w + 120) w->x = -w->w + 120;
    if (w->x > screen.width - 120) w->x = screen.width - 120;
    if (w->y < 0) w->y = 0;
    if (w->y > screen.height - TASKBAR_H - TITLE_BAR_H) w->y = screen.height - TASKBAR_H - TITLE_BAR_H;
    request_redraw();
}

// ---- Window management ----

static void set_bounds(struct window *w, int x, int y, int width, int height) {
    w->x = x;
    w->y = y;
    w->w = width;
    w->h = height;
    w->canvas.width = width;
    w->canvas.height = height;
    request_redraw();
}

void window_set_snap(struct window *w, int snap) {
    const struct window_app *app = app_for(w->kind);
    if (!app->resizable) return;
    int work_h = screen.height - TASKBAR_H;
    if (snap != SNAP_NONE && w->snap == SNAP_NONE) {
        w->restore_x = w->x; w->restore_y = w->y; w->restore_w = w->w; w->restore_h = w->h;
    }
    w->snap = snap;
    switch (snap) {
    case SNAP_MAXIMIZED: set_bounds(w, 0, 0, screen.width, work_h); break;
    case SNAP_LEFT:      set_bounds(w, 0, 0, screen.width / 2, work_h); break;
    case SNAP_RIGHT:     set_bounds(w, screen.width / 2, 0, screen.width - screen.width / 2, work_h); break;
    default:
        set_bounds(w, w->restore_x, w->restore_y, w->restore_w, w->restore_h);
        window_moved(w);
        break;
    }
}

void window_toggle_maximize(struct window *w) {
    window_set_snap(w, w->snap == SNAP_MAXIMIZED ? SNAP_NONE : SNAP_MAXIMIZED);
}

// Like Windows' Win+arrow keys.
void window_snap_key(struct window *w, int direction) {
    // direction: 0 left, 1 right, 2 up, 3 down
    switch (direction) {
    case 0: window_set_snap(w, w->snap == SNAP_RIGHT ? SNAP_NONE : SNAP_LEFT); break;
    case 1: window_set_snap(w, w->snap == SNAP_LEFT ? SNAP_NONE : SNAP_RIGHT); break;
    case 2: window_set_snap(w, SNAP_MAXIMIZED); break;
    default:
        if (w->snap != SNAP_NONE) window_set_snap(w, SNAP_NONE);
        else window_minimize(w->kind);
        break;
    }
}

const char *window_snap_words(struct window *w) {
    switch (w->snap) {
    case SNAP_MAXIMIZED: return "maximized, filling the screen above the taskbar";
    case SNAP_LEFT:      return "snapped to the left half of the screen";
    case SNAP_RIGHT:     return "snapped to the right half of the screen";
    default:             return "restored to its normal size";
    }
}

// Dragging a maximized or snapped window by its title bar pulls it back
// to its normal size, under the pointer.
void window_unsnap_for_drag(struct window *w, int mouse_x) {
    if (w->snap == SNAP_NONE) return;
    int fraction = (mouse_x - w->x) * 1000 / (w->w > 0 ? w->w : 1);
    int width = w->restore_w;
    w->snap = SNAP_NONE;
    set_bounds(w, mouse_x - width * fraction / 1000, w->y, width, w->restore_h);
    window_moved(w);
}

// ---- Drawing ----

void draw_window_item(struct gui_canvas *c, const struct gui_item *item, bool focused, bool hovered) {
    uint32_t text_color = item->disabled ? theme->text_dim : theme->text;
    int text_y = item->y + (item->h - gui_text_height(1)) / 2;
    if (item->role == GUI_ROLE_RADIO) {
        if (focused || hovered) {
            gui_rounded(c, item->x, item->y, item->w, item->h, 8, focused ? theme->selected : theme->hover);
        }
        int dot_x = item->x + 20, dot_y = item->y + item->h / 2;
        gui_ring(c, dot_x, dot_y, 9, 2, item->checked ? theme->accent : theme->text_dim);
        if (item->checked) gui_circle(c, dot_x, dot_y, 4, theme->accent);
        gui_text(c, item->x + 40, text_y, item->name, text_color, 1);
        if (focused) gui_frame(c, item->x, item->y, item->w, item->h, 2, theme->accent);
        return;
    }
    // A normal button.
    gui_rounded(c, item->x, item->y, item->w, item->h, 8,
                focused ? theme->selected : (hovered ? theme->hover : theme->background));
    if (focused) {
        gui_frame(c, item->x, item->y, item->w, item->h, 2, theme->accent);
    } else {
        gui_frame(c, item->x + 4, item->y, item->w - 8, 1, 1, theme->panel_border);
        gui_frame(c, item->x + 4, item->y + item->h - 1, item->w - 8, 1, 1, theme->panel_border);
    }
    gui_text_centered(c, item->x + item->w / 2, text_y, item->name, text_color, 1);
}

static void draw_title_bar(struct window *w, bool active, struct gui_item *items, int count) {
    struct gui_canvas *c = &w->canvas;
    gui_fill(c, 0, 0, w->w, TITLE_BAR_H, active ? theme->selected : theme->panel);
    gui_fill(c, 0, TITLE_BAR_H - 1, w->w, 1, theme->panel_border);
    icon_draw(c, w->icon, 10, 9, 22);
    gui_text_bold(c, 42, 12, w->title, active ? theme->text : theme->text_dim, 1);

    // Minimize, maximize and close buttons (the last three items).
    for (int i = count - TITLE_ITEMS; i < count; i++) {
        struct gui_item *b = &items[i];
        bool hover = hovered_window_item == i;
        bool focused = active && zone == ZONE_WINDOW && w->focus == i;
        bool is_close = ACTION_KIND(b->action) == A_CLOSE;
        if (hover || focused) {
            gui_fill(c, b->x, b->y, b->w, b->h, is_close ? 0xC42B1C : theme->hover);
        }
        uint32_t mark = (hover && is_close) ? 0xFFFFFF : theme->text;
        int mx = b->x + b->w / 2, my = b->h / 2;
        if (is_close) {
            gui_line(c, mx - 6, my - 6, mx + 6, my + 6, 2, mark);
            gui_line(c, mx + 6, my - 6, mx - 6, my + 6, 2, mark);
        } else if (ACTION_KIND(b->action) == A_MAXIMIZE) {
            uint32_t m = b->disabled ? theme->text_dim : mark;
            if (w->snap == SNAP_NONE) {
                gui_frame(c, mx - 6, my - 6, 12, 12, 2, m);
            } else {   // two overlapping squares: "restore"
                gui_frame(c, mx - 6, my - 3, 9, 9, 2, m);
                gui_fill(c, mx - 3, my - 7, 10, 2, m);
                gui_fill(c, mx + 5, my - 7, 2, 9, m);
            }
        } else {
            gui_fill(c, mx - 7, my, 14, 2, mark);
        }
        if (focused) gui_frame(c, b->x + 2, b->y + 2, b->w - 4, b->h - 4, 2, theme->accent);
    }
}

static void render_window(struct window *w, bool active) {
    const struct window_app *app = app_for(w->kind);
    struct gui_item items[48];
    int count = window_items(w, items, 48);
    if (w->focus >= count) w->focus = count - 1;

    gui_fill(&w->canvas, 0, 0, w->w, w->h, theme->panel);
    // Draw the content in its own coordinates (below the title bar).
    struct gui_canvas content = w->canvas;
    content.pixels += (size_t)TITLE_BAR_H * w->w;
    content.height -= TITLE_BAR_H;
    int own = count - TITLE_ITEMS;
    for (int i = 0; i < own; i++) items[i].y -= TITLE_BAR_H;
    int content_focus = (active && zone == ZONE_WINDOW && w->focus < own) ? w->focus : -1;
    app->draw(&content, items, own, content_focus);
    for (int i = 0; i < own; i++) items[i].y += TITLE_BAR_H;

    draw_title_bar(w, active, items, count);
    gui_frame(&w->canvas, 0, 0, w->w, w->h, 1, active ? theme->accent2 : theme->panel_border);
}

// Where the window's taskbar button is, for the minimize animation.
static void taskbar_spot(enum window_kind kind, int *x, int *y) {
    struct gui_item items[16];
    int count = taskbar_items(items, 16);
    for (int i = 0; i < count; i++) {
        if (items[i].action == ACTION(A_TASKBAR_WINDOW, (int)kind)) {
            *x = items[i].x + items[i].w / 2;
            *y = items[i].y + items[i].h / 2;
            return;
        }
    }
    *x = screen.width / 2;
    *y = screen.height - TASKBAR_H / 2;
}

void windows_draw(void) {
    struct window *top = window_top();
    for (int i = 0; i < order_count; i++) {
        struct window *w = &windows[order[i]];
        if (!w->open || (w->minimized && w->anim == ANIM_NONE)) continue;

        hovered_window_item = -1;
        if (w == window_at(mouse_x, mouse_y)) {
            struct gui_item items[48];
            int count = window_items(w, items, 48);
            for (int j = 0; j < count; j++) {
                int lx = mouse_x - w->x, ly = mouse_y - w->y;
                if (lx >= items[j].x && lx < items[j].x + items[j].w &&
                    ly >= items[j].y && ly < items[j].y + items[j].h) {
                    hovered_window_item = j;
                    break;
                }
            }
        }
        render_window(w, w == top);

        if (w->anim == ANIM_NONE) {
            gui_shadow(&screen, w->x, w->y, w->w, w->h, 14);
            gui_blit(&screen, w->x, w->y, &w->canvas);
            continue;
        }

        // Animating: work out how far along it is, then where and how solid.
        int progress = (int)((now_ms - w->anim_start) * 1000 / ANIM_MS);
        if (progress > 1000) progress = 1000;
        int eased = gui_ease_out(progress);
        int x = w->x, y = w->y, width = w->w, height = w->h, alpha = 255;

        if (w->anim == ANIM_OPEN || w->anim == ANIM_CLOSE) {
            // Grow from 88% size and fade in (closing runs it backwards).
            int amount = w->anim == ANIM_OPEN ? eased : 1000 - eased;
            int scale = 880 + amount * 120 / 1000;
            width = w->w * scale / 1000;
            height = w->h * scale / 1000;
            x = w->x + (w->w - width) / 2;
            y = w->y + (w->h - height) / 2;
            alpha = amount * 255 / 1000;
        } else {
            // Shrink into (or grow out of) the taskbar button.
            int amount = w->anim == ANIM_MINIMIZE ? eased : 1000 - eased;
            int tx, ty;
            taskbar_spot(w->kind, &tx, &ty);
            width = w->w - (w->w - 60) * amount / 1000;
            height = w->h - (w->h - 40) * amount / 1000;
            x = w->x + (tx - width / 2 - w->x) * amount / 1000;
            y = w->y + (ty - height / 2 - w->y) * amount / 1000;
            alpha = 255 - amount * 200 / 1000;
        }
        gui_blit_scaled(&screen, x, y, width, height, &w->canvas, alpha);
    }
    hovered_window_item = -1;
}
