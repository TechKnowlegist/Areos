// app_calculator.c - the Calculator window.
//
// AREOS apps don't have floating point (decimal) maths yet, so numbers are
// stored as whole numbers counted in millionths: 2.5 is kept as 2500000.
// That gives six decimal places, which is plenty for a pocket calculator.
//
// Every key is spoken ("7", "plus", "equals 15"), like a talking calculator.

#include "desktop.h"

#define SCALE 1000000LL            // one = 1,000,000 millionths
#define LIMIT 999999999999LL       // biggest number shown (about a trillion)

static char entry[24];             // digits being typed
static bool entering = false;      // is the user in the middle of typing a number?
static int64_t total = 0;          // the running result, in millionths
static char pending = 0;           // operation waiting for its second number (+ - * /)
static bool error = false;
static char expression[48];        // small line above the display, like "12 +"

// ---- Numbers ----

static int64_t parse(const char *text) {
    int64_t whole = 0, fraction = 0, fraction_scale = SCALE;
    bool negative = false, after_point = false;
    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        if (c == '-') negative = true;
        else if (c == '.') after_point = true;
        else if (c >= '0' && c <= '9') {
            if (!after_point) whole = whole * 10 + (c - '0');
            else if (fraction_scale > 1) { fraction_scale /= 10; fraction += (c - '0') * fraction_scale; }
        }
    }
    int64_t value = whole * SCALE + fraction;
    return negative ? -value : value;
}

static void format(int64_t value, char *out, int size) {
    out[0] = '\0';
    if (value < 0) {
        text_append(out, "-", size);
        value = -value;
    }
    text_append_number(out, value / SCALE, size);
    int64_t fraction = value % SCALE;
    if (fraction != 0) {
        char digits[8] = "000000";
        for (int i = 5; i >= 0; i--) {
            digits[i] = (char)('0' + fraction % 10);
            fraction /= 10;
        }
        int last = 5;
        while (last > 0 && digits[last] == '0') last--;   // drop trailing zeros
        digits[last + 1] = '\0';
        text_append(out, ".", size);
        text_append(out, digits, size);
    }
}

static bool too_big(int64_t value) {
    return value > LIMIT * SCALE || value < -LIMIT * SCALE;
}

// a * b, both in millionths, without overflowing on the way.
static int64_t multiply(int64_t a, int64_t b) {
    int64_t whole = a / SCALE, part = a % SCALE;
    return whole * b + part * b / SCALE;
}

// a / b, both in millionths, a step at a time so nothing overflows.
static int64_t divide(int64_t a, int64_t b) {
    int64_t quotient = a / b;
    int64_t remainder = a % b;
    int64_t result = quotient * SCALE;
    // Work out the six decimal places one digit at a time, like long division.
    int64_t place = SCALE / 10;
    for (int i = 0; i < 6; i++) {
        remainder *= 10;
        result += (remainder / b) * place;
        remainder %= b;
        place /= 10;
    }
    return result;
}

static int64_t apply(int64_t a, char op, int64_t b) {
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return multiply(a, b);
    case '/':
        if (b == 0) { error = true; return 0; }
        return divide(a, b);
    }
    return b;
}

// ---- Speaking ----

static const char *spoken(char key) {
    switch (key) {
    case '+': return "plus";
    case '-': return "minus";
    case '*': return "times";
    case '/': return "divided by";
    case '=': return "equals";
    case '.': return "point";
    case '%': return "percent";
    case 'C': return "cleared";
    case 'B': return "backspace";
    case 'N': return "plus minus";
    }
    return "";
}

static void say_value(const char *prefix, int64_t value) {
    char number[40];
    format(value, number, sizeof(number));
    gui_line_start();
    gui_line_add(prefix);
    gui_line_add(number);
    gui_line_say();
}

// ---- Pressing keys ----

static void reset(void) {
    entry[0] = '\0';
    entering = false;
    total = 0;
    pending = 0;
    error = false;
    expression[0] = '\0';
}

static int64_t current_value(void) {
    return entering ? parse(entry) : total;
}

static void press(char key) {
    if (error && key != 'C') {
        reset();
    }
    if ((key >= '0' && key <= '9') || key == '.') {
        if (!entering) {
            entry[0] = '\0';
            entering = true;
        }
        int length = text_length(entry);
        bool has_point = false;
        for (int i = 0; i < length; i++) if (entry[i] == '.') has_point = true;
        if ((key == '.' && has_point) || length >= 15) return;
        if (key == '.' && length == 0) text_append(entry, "0", sizeof(entry));
        char digit[2] = { key, '\0' };
        text_append(entry, digit, sizeof(entry));
        gui_announce(key == '.' ? "point" : digit);
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        int64_t value = current_value();
        total = pending ? apply(total, pending, value) : value;
        pending = key;
        entering = false;
        format(total, expression, sizeof(expression));
        char op[4] = { ' ', key == '*' ? 'x' : key, '\0' };
        text_append(expression, op, sizeof(expression));
        gui_announce(spoken(key));
    } else if (key == '=') {
        if (pending) {
            total = apply(total, pending, current_value());
            pending = 0;
        } else {
            total = current_value();
        }
        entering = false;
        expression[0] = '\0';
        if (error) gui_announce("Can't divide by zero.");
        else if (too_big(total)) { error = true; gui_announce("That number is too big."); }
        else say_value("equals ", total);
    } else if (key == 'C') {
        reset();
        gui_announce(spoken(key));
    } else if (key == 'B') {
        int length = text_length(entry);
        if (entering && length > 0) {
            entry[length - 1] = '\0';
            say_value("", parse(entry));
        }
    } else if (key == '%') {
        int64_t value = divide(current_value(), 100 * SCALE);
        format(value, entry, sizeof(entry));
        entering = true;
        say_value("percent, ", value);
    } else if (key == 'N') {
        int64_t value = -current_value();
        format(value, entry, sizeof(entry));
        entering = true;
        say_value("", value);
    }
    request_redraw();
}

// ---- The window ----

// Button layout: what's printed on it, the key it presses, and its spoken name.
static const struct { const char *label; char key; const char *name; } buttons[20] = {
    { "C", 'C', "Clear" }, { "<-", 'B', "Backspace" }, { "%", '%', "Percent" }, { "/", '/', "Divide" },
    { "7", '7', "7" },     { "8", '8', "8" },          { "9", '9', "9" },       { "x", '*', "Times" },
    { "4", '4', "4" },     { "5", '5', "5" },          { "6", '6', "6" },       { "-", '-', "Minus" },
    { "1", '1', "1" },     { "2", '2', "2" },          { "3", '3', "3" },       { "+", '+', "Plus" },
    { "+/-", 'N', "Plus minus" }, { "0", '0', "0" },   { ".", '.', "Point" },   { "=", '=', "Equals" },
};

#define CELL_W 72
#define CELL_H 54
#define GAP 8
#define GRID_X 20
#define GRID_Y 120

static int build(struct gui_item *items, int max) {
    (void)max;
    for (int i = 0; i < 20; i++) {
        items[i] = (struct gui_item){ .name = buttons[i].name, .role = GUI_ROLE_BUTTON,
            .action = ACTION(A_CONTENT, i),
            .x = GRID_X + (i % 4) * (CELL_W + GAP), .y = GRID_Y + (i / 4) * (CELL_H + GAP),
            .w = CELL_W, .h = CELL_H };
    }
    return 20;
}

static void draw(struct gui_canvas *c, struct gui_item *items, int count, int focus) {
    // The display.
    gui_rounded(c, GRID_X, 16, 4 * CELL_W + 3 * GAP, 88, 10, theme->background);
    gui_text(c, GRID_X + 4 * CELL_W + 3 * GAP - 14 - gui_text_width(expression, 1), 26,
             expression, theme->text_dim, 1);
    char shown[40];
    if (error) text_copy(shown, "Error", sizeof(shown));
    else if (entering) text_copy(shown, entry, sizeof(shown));
    else format(total, shown, sizeof(shown));
    int scale = text_length(shown) > 11 ? 2 : 3;
    gui_text(c, GRID_X + 4 * CELL_W + 3 * GAP - 14 - gui_text_width(shown, scale), 92 - gui_text_height(scale),
             shown, theme->text, scale);

    // The buttons: numbers plain, operations tinted, equals in green.
    for (int i = 0; i < count; i++) {
        struct gui_item *it = &items[i];
        char k = buttons[i].key;
        bool is_op = k == '/' || k == '*' || k == '-' || k == '+' || k == 'C' || k == 'B' || k == '%' || k == 'N';
        uint32_t fill = k == '=' ? theme->accent : (is_op ? theme->hover : theme->background);
        if (i == hovered_window_item && k != '=') fill = theme->selected;
        gui_rounded(c, it->x, it->y, it->w, it->h, 10, fill);
        uint32_t text_color = k == '=' ? 0x0A0F14 : (is_op ? theme->accent2 : theme->text);
        gui_text_centered(c, it->x + it->w / 2, it->y + (it->h - 32) / 2, buttons[i].label, text_color, 2);
        if (i == focus) gui_frame(c, it->x, it->y, it->w, it->h, 3, theme->accent2);
    }
}

static bool key(struct areos_key_event *e, struct gui_item *items, int count, int *focus) {
    (void)items; (void)count; (void)focus;
    char c = e->character;
    if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '=') {
        press(c);
        return true;
    }
    if (c == 'x' || c == 'X') { press('*'); return true; }
    if (c == 'c' || c == 'C' || e->key == AREOS_KEY_DELETE) { press('C'); return true; }
    if (e->key == AREOS_KEY_BACKSPACE) { press('B'); return true; }
    if (e->key == AREOS_KEY_ENTER) { press('='); return true; }   // Enter means equals, like most calculators
    return false;
}

static void choose(int index) {
    if (index >= 0 && index < 20) press(buttons[index].key);
}

static void opened(void) {
    gui_announce("Type numbers and + - * / then Enter for equals. C clears.");
}

const struct window_app app_calculator = {
    .title = "Calculator", .icon = ICON_CALCULATOR, .width = 4 * CELL_W + 3 * GAP + 2 * GRID_X, .height = GRID_Y + 5 * (CELL_H + GAP) + 12,
    .build = build, .draw = draw, .key = key, .choose = choose, .opened = opened,
};
