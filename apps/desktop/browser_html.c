// browser_html.c - reading a web page's HTML and laying it out.
//
// HTML is text with "tags" in angle brackets: <h1>Title</h1>, <a href="...">
// for links, <p> for paragraphs, and so on. This reads it a character at a
// time, keeping track of which tags we're inside (bold? a link? a heading?),
// and places each word on the page, wrapping lines at the edge.
//
// It's a simple reader: no pictures, no colors or layouts from the page,
// no JavaScript. Just the words, headings, links and simple forms, which
// is also what a screen reader cares about most.

#include "browser.h"

// ---- Small helpers ----

bool text_starts_with(const char *text, const char *prefix) {
    for (int i = 0; prefix[i]; i++) {
        if (to_lower(text[i]) != to_lower(prefix[i])) return false;
    }
    return true;
}

static bool is_space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f'; }
static bool is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

const char *page_string(const struct page_document *d, int at) {
    if (at < 0 || at >= d->text_used) return "";
    return d->text + at;
}

bool page_document_init(struct page_document *d) {
    d->text = areos_more_memory(TEXT_STORE);
    d->boxes = areos_more_memory(sizeof(struct page_box) * MAX_BOXES);
    d->lines = areos_more_memory(sizeof(struct page_line) * MAX_LINES);
    d->focusables = areos_more_memory(sizeof(struct page_focusable) * MAX_FOCUSABLES);
    return d->text && d->boxes && d->lines && d->focusables;
}

// ---- Web addresses ----

bool url_split(const char *url, char *host, int host_max, int *port, char *path, int path_max, bool *secure) {
    *secure = text_starts_with(url, "https://");
    if (!*secure && !text_starts_with(url, "http://")) return false;
    const char *p = url + (*secure ? 8 : 7);
    int n = 0;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#' && n < host_max - 1) host[n++] = to_lower(*p++);
    host[n] = '\0';
    if (n == 0) return false;
    *port = *secure ? 443 : 80;
    if (*p == ':') {
        p++;
        int value = 0;
        while (*p >= '0' && *p <= '9') value = value * 10 + (*p++ - '0');
        if (value > 0 && value < 65536) *port = value;
    }
    n = 0;
    if (*p != '/') path[n++] = '/';
    while (*p && *p != '#' && n < path_max - 1) path[n++] = *p++;   // the part after # stays in the browser
    path[n] = '\0';
    return true;
}

void url_resolve(const char *base, const char *link, char *out, int max) {
    while (is_space(*link)) link++;
    if (text_starts_with(link, "http://") || text_starts_with(link, "https://")
        || text_starts_with(link, "about:") || text_starts_with(link, "mailto:")
        || text_starts_with(link, "javascript:")) {
        text_copy(out, link, max);
        return;
    }
    if (link[0] == '/' && link[1] == '/') {             // "//host/path": same scheme
        text_copy(out, text_starts_with(base, "https://") ? "https:" : "http:", max);
        text_append(out, link, max);
        return;
    }
    // Find where the base's host ends ("http://host" + ...).
    char root[URL_MAX];
    text_copy(root, base, sizeof(root));
    int start = text_starts_with(root, "http://") ? 7 : (text_starts_with(root, "https://") ? 8 : 0);
    int host_end = start;
    while (root[host_end] && root[host_end] != '/' && root[host_end] != '?' && root[host_end] != '#') host_end++;
    if (link[0] == '#' || link[0] == '\0') {             // the same page
        text_copy(out, base, max);
        return;
    }
    if (link[0] == '?') {                                // same path, new question
        int q = host_end;
        while (root[q] && root[q] != '?' && root[q] != '#') q++;
        root[q] = '\0';
        text_copy(out, root, max);
        text_append(out, link, max);
        return;
    }
    char path[URL_MAX];
    if (link[0] == '/') {
        text_copy(path, link, sizeof(path));
    } else {
        // Relative: the base's folder, then the link.
        int end = host_end;
        while (root[end] && root[end] != '?' && root[end] != '#') end++;
        int last_slash = -1;
        for (int i = host_end; i < end; i++) if (root[i] == '/') last_slash = i;
        path[0] = '\0';
        if (last_slash < 0) {
            text_copy(path, "/", sizeof(path));
        } else {
            int n = last_slash - host_end + 1;
            if (n >= (int)sizeof(path)) n = sizeof(path) - 1;
            for (int i = 0; i < n; i++) path[i] = root[host_end + i];
            path[n] = '\0';
        }
        text_append(path, link, sizeof(path));
    }
    // Tidy "./" and "../" out of the path.
    char clean[URL_MAX];
    int c = 0;
    for (int i = 0; path[i] && c < (int)sizeof(clean) - 1;) {
        if (path[i] == '?' || path[i] == '#') {           // the rest is copied as it is
            while (path[i] && c < (int)sizeof(clean) - 1) clean[c++] = path[i++];
            break;
        }
        if (path[i] == '/' && path[i + 1] == '.' && (path[i + 2] == '/' || path[i + 2] == '\0')) {
            i += 2;
            continue;
        }
        if (path[i] == '/' && path[i + 1] == '.' && path[i + 2] == '.' && (path[i + 3] == '/' || path[i + 3] == '\0')) {
            while (c > 0 && clean[c - 1] != '/') c--;     // back up over the last folder
            if (c > 0) c--;
            while (c > 0 && clean[c - 1] != '/') c--;
            if (c > 0) c--;
            i += 3;
            continue;
        }
        clean[c++] = path[i++];
    }
    clean[c] = '\0';
    if (clean[0] != '/') {
        text_copy(path, "/", sizeof(path));
        text_append(path, clean, sizeof(path));
        text_copy(clean, path, sizeof(clean));
    }
    root[host_end] = '\0';
    text_copy(out, root, max);
    text_append(out, clean, max);
}

void url_append_escaped(char *out, const char *text, int max) {
    const char *hex = "0123456789ABCDEF";
    int n = text_length(out);
    for (int i = 0; text[i] && n < max - 4; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (is_alnum((char)ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') out[n++] = (char)ch;
        else if (ch == ' ') out[n++] = '+';
        else { out[n++] = '%'; out[n++] = hex[ch >> 4]; out[n++] = hex[ch & 15]; }
    }
    out[n] = '\0';
}

// ---- Characters ----

// Named characters like &amp; -> &.
static const struct { const char *name; const char *text; } entities[] = {
    { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" },
    { "nbsp", " " }, { "copy", "(c)" }, { "reg", "(R)" }, { "trade", "(TM)" },
    { "mdash", "-" }, { "ndash", "-" }, { "hellip", "..." }, { "bull", "*" }, { "middot", "-" },
    { "lsquo", "'" }, { "rsquo", "'" }, { "ldquo", "\"" }, { "rdquo", "\"" }, { "laquo", "<<" },
    { "raquo", ">>" }, { "times", "x" }, { "deg", " degrees" }, { "euro", "EUR" }, { "pound", "GBP" },
    { "rarr", "->" }, { "larr", "<-" }, { "eacute", "e" }, { "egrave", "e" }, { "aacute", "a" },
    { "agrave", "a" }, { "uuml", "u" }, { "ouml", "o" }, { "auml", "a" }, { "ntilde", "n" },
    { "ccedil", "c" }, { "szlig", "ss" }, { "iacute", "i" }, { "oacute", "o" }, { "uacute", "u" },
};

// A character number (from &#...; or UTF-8) as plain text our font can show.
static void code_point_text(uint32_t cp, char *out) {
    out[0] = '\0';
    if (cp == 9 || cp == 10 || cp == 13) { out[0] = ' '; out[1] = '\0'; return; }
    if (cp >= 32 && cp < 127) { out[0] = (char)cp; out[1] = '\0'; return; }
    switch (cp) {
    case 0xA0: case 0x2002: case 0x2003: case 0x2009: text_copy(out, " ", 4); return;
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: text_copy(out, "'", 4); return;
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: text_copy(out, "\"", 4); return;
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: text_copy(out, "-", 4); return;
    case 0x2026: text_copy(out, "...", 4); return;
    case 0x2022: case 0x00B7: text_copy(out, "*", 4); return;
    case 0x00A9: text_copy(out, "(c)", 4); return;
    case 0x00AB: text_copy(out, "<<", 4); return;
    case 0x00BB: text_copy(out, ">>", 4); return;
    case 0x2192: text_copy(out, "->", 4); return;
    case 0x2190: text_copy(out, "<-", 4); return;
    }
    if (cp >= 0xC0 && cp <= 0xFF) {   // accented letters: the plain letter
        const char *plain = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
        out[0] = plain[cp - 0xC0];
        out[1] = '\0';
    }
    // Anything else (other alphabets, emoji) is left out: the font can't draw it.
}

// ---- The layout ----

static struct page_document *doc;
static int page_width;
static int cursor_x, cursor_y, current_line, indent;
static bool pending_space, last_was_gap;
static int heading_level, bold_depth, italic_depth, pre_depth, list_depth;
static int current_focusable = -1;   // inside a link or button
static int current_form = -1;
static char focus_label[160];
static int focus_label_length;

static int store(const char *text, int length) {
    if (doc->text_used + length + 1 > TEXT_STORE) return -1;
    int at = doc->text_used;
    for (int i = 0; i < length; i++) doc->text[at + i] = text[i];
    doc->text[at + length] = '\0';
    doc->text_used += length + 1;
    return at;
}

static void finish_line(void) {
    if (current_line >= 0) {
        cursor_y += doc->lines[current_line].h;
        current_line = -1;
    }
    cursor_x = indent;
    pending_space = false;
}

static void line_break(void) {
    finish_line();
}

static void paragraph_break(void) {
    finish_line();
    if (!last_was_gap && doc->line_count > 0) {
        cursor_y += 10;
        last_was_gap = true;
    }
}

static bool open_line(int height) {
    if (current_line < 0) {
        if (doc->line_count >= MAX_LINES) return false;
        current_line = doc->line_count++;
        struct page_line *l = &doc->lines[current_line];
        l->y = cursor_y;
        l->h = height;
        l->first_box = doc->box_count;
        l->box_count = 0;
        l->heading = heading_level;
    } else if (doc->lines[current_line].h < height) {
        doc->lines[current_line].h = height;
    }
    return true;
}

static uint16_t current_style(void) {
    uint16_t style = 0;
    if (bold_depth > 0 || heading_level > 0) style |= STYLE_BOLD;
    if (italic_depth > 0) style |= STYLE_ITALIC;
    if (heading_level > 0) style |= STYLE_HEADING;
    if (pre_depth > 0) style |= STYLE_PRE;
    if (current_focusable >= 0) {
        enum focus_kind k = doc->focusables[current_focusable].kind;
        style |= k == FOCUS_LINK ? STYLE_LINK : STYLE_BUTTON;
    }
    return style;
}

// Put one word (no spaces inside, unless it's preformatted text) on the page.
static void place_word(const char *word, int length, uint16_t style, int focusable) {
    if (length <= 0) return;
    int scale = heading_level == 1 ? 2 : 1;
    int char_w = 8 * scale;
    // Words wider than the page are split.
    int max_chars = (page_width - indent) / char_w;
    if (max_chars < 1) max_chars = 1;
    while (length > max_chars) {
        place_word(word, max_chars, style, focusable);
        pending_space = false;
        word += max_chars;
        length -= max_chars;
    }
    int width = length * char_w;
    int space = (pending_space && cursor_x > indent) ? char_w : 0;
    if (current_line >= 0 && cursor_x + space + width > page_width && cursor_x > indent) {
        finish_line();
        space = 0;
    }
    if (!open_line(16 * scale + 6)) return;
    struct page_line *line = &doc->lines[current_line];

    // Join it onto the box before, if that's the same style (fewer, longer boxes).
    if (line->box_count > 0) {
        struct page_box *last = &doc->boxes[doc->box_count - 1];
        if (last->style == style && last->focusable == focusable && last->scale == scale
            && last->text + last->length + 1 == doc->text_used) {
            doc->text_used--;   // drop the end-of-text marker, then add on
            if (space) store(" ", 1), doc->text_used--;
            store(word, length);
            last->length += (space ? 1 : 0) + length;
            last->w += space + width;
            cursor_x += space + width;
            pending_space = false;
            last_was_gap = false;
            return;
        }
    }
    if (doc->box_count >= MAX_BOXES) return;
    int at = store(word, length);
    if (at < 0) return;
    struct page_box *b = &doc->boxes[doc->box_count++];
    b->x = cursor_x + space;
    b->y = line->y;
    b->w = width;
    b->h = 16 * scale;
    b->text = at;
    b->length = length;
    b->style = style;
    b->scale = (uint8_t)scale;
    b->focusable = (int16_t)focusable;
    b->line = current_line;
    line->box_count++;
    if (focusable >= 0 && doc->focusables[focusable].line < 0) doc->focusables[focusable].line = current_line;
    cursor_x += space + width;
    pending_space = false;
    last_was_gap = false;
}

static void add_text(const char *text, int length) {
    uint16_t style = current_style();
    if (pre_depth > 0) {
        // Preformatted: keep spaces and line breaks exactly.
        int start = 0;
        for (int i = 0; i <= length; i++) {
            if (i == length || text[i] == '\n') {
                if (i > start) place_word(text + start, i - start, style, current_focusable);
                if (i < length) line_break();
                start = i + 1;
            }
        }
        return;
    }
    int i = 0;
    while (i < length) {
        if (is_space(text[i])) {
            pending_space = true;
            i++;
            continue;
        }
        int start = i;
        while (i < length && !is_space(text[i])) i++;
        place_word(text + start, i - start, style, current_focusable);
        if (current_focusable >= 0 && focus_label_length + (i - start) + 2 < (int)sizeof(focus_label)) {
            if (focus_label_length > 0) focus_label[focus_label_length++] = ' ';
            for (int k = start; k < i; k++) focus_label[focus_label_length++] = text[k];
        }
    }
}

static int current_aid = 0;   // data-aid of the tag being read: how scripts name controls

static int new_focusable(enum focus_kind kind) {
    if (doc->focusable_count >= MAX_FOCUSABLES) return -1;
    int f = doc->focusable_count++;
    struct page_focusable *p = &doc->focusables[f];
    p->kind = kind;
    p->target = -1;
    p->name = -1;
    p->label = -1;
    p->form = current_form;
    p->line = -1;
    p->aid = current_aid;
    p->secret = false;
    p->value[0] = '\0';
    return f;
}

// ---- Reading tags ----

#define ATTR_MAX 8
struct tag {
    char name[16];
    bool closing;
    int attr_count;
    char attr_name[ATTR_MAX][16];
    char attr_value[ATTR_MAX][URL_MAX];
};

static const char *attr(const struct tag *t, const char *name) {
    for (int i = 0; i < t->attr_count; i++) {
        if (text_equal(t->attr_name[i], name)) return t->attr_value[i];
    }
    return NULL;
}

// Decode &...; and UTF-8 into plain text. Returns how many input bytes were used.
static int decode_char(const char *s, int length, char *out) {
    unsigned char c = (unsigned char)s[0];
    out[0] = '\0';
    if (c == '&') {
        int end = 1;
        while (end < length && end < 12 && s[end] != ';' && !is_space(s[end]) && s[end] != '&') end++;
        if (end < length && s[end] == ';') {
            char name[12];
            int n = end - 1;
            for (int i = 0; i < n; i++) name[i] = s[1 + i];
            name[n] = '\0';
            if (name[0] == '#') {
                uint32_t cp = 0;
                if (name[1] == 'x' || name[1] == 'X') {
                    for (int i = 2; name[i]; i++) {
                        char h = to_lower(name[i]);
                        cp = cp * 16 + (uint32_t)(h >= 'a' ? h - 'a' + 10 : h - '0');
                    }
                } else {
                    for (int i = 1; name[i]; i++) cp = cp * 10 + (uint32_t)(name[i] - '0');
                }
                code_point_text(cp, out);
                return end + 1;
            }
            for (unsigned k = 0; k < sizeof(entities) / sizeof(entities[0]); k++) {
                if (text_equal(entities[k].name, name)) {
                    text_copy(out, entities[k].text, 8);
                    return end + 1;
                }
            }
        }
        out[0] = '&';
        out[1] = '\0';
        return 1;
    }
    if (c < 0x80) {
        out[0] = (char)c;
        out[1] = '\0';
        return 1;
    }
    // UTF-8: 2, 3 or 4 bytes.
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    if (extra == 0 || extra >= length) return 1;   // not valid UTF-8: skip it
    for (int i = 1; i <= extra; i++) cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
    code_point_text(cp, out);
    return extra + 1;
}

// Tags whose insides aren't shown. (<noscript> is shown: AREOS has no JavaScript,
// which is exactly when a page wants it read.)
static bool is_hidden_tag(const char *name) {
    return text_equal(name, "script") || text_equal(name, "style") || text_equal(name, "head")
        || text_equal(name, "svg") || text_equal(name, "template")
        || text_equal(name, "iframe") || text_equal(name, "object") || text_equal(name, "select")
        || text_equal(name, "textarea") || text_equal(name, "canvas") || text_equal(name, "video")
        || text_equal(name, "audio");
}

static bool is_block_tag(const char *n) {
    const char *blocks[] = { "div", "section", "article", "header", "footer", "nav", "main", "aside",
        "table", "tr", "ul", "ol", "dl", "dt", "dd", "center", "figure", "figcaption", "address",
        "fieldset", "details", "summary", "form", "tbody", "thead", "caption", "legend", "menu" };
    for (unsigned i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        if (text_equal(n, blocks[i])) return true;
    }
    return false;
}

static void handle_tag(const struct tag *t) {
    const char *n = t->name;
    current_aid = 0;
    if (!t->closing) {
        const char *aid_text = attr(t, "data-aid");
        for (int i = 0; aid_text && aid_text[i] >= '0' && aid_text[i] <= '9'; i++) current_aid = current_aid * 10 + (aid_text[i] - '0');
    }
    if (n[0] == 'h' && n[1] >= '1' && n[1] <= '6' && n[2] == '\0') {
        paragraph_break();
        if (!t->closing) {
            heading_level = n[1] - '0';
            doc->heading_count++;
        } else {
            heading_level = 0;
        }
        return;
    }
    if (text_equal(n, "p") || text_equal(n, "blockquote")) {
        paragraph_break();
        if (text_equal(n, "blockquote")) indent += t->closing ? -24 : 24;
        if (indent < 0) indent = 0;
        cursor_x = indent;
        return;
    }
    if (text_equal(n, "br")) { line_break(); return; }
    if (text_equal(n, "hr")) {
        paragraph_break();
        place_word("----------", 10, 0, -1);
        paragraph_break();
        return;
    }
    if (text_equal(n, "pre")) {
        paragraph_break();
        pre_depth += t->closing ? -1 : 1;
        if (pre_depth < 0) pre_depth = 0;
        return;
    }
    if (text_equal(n, "ul") || text_equal(n, "ol")) {
        line_break();
        list_depth += t->closing ? -1 : 1;
        if (list_depth < 0) list_depth = 0;
        indent = list_depth * 24;
        if (indent > page_width / 3) indent = page_width / 3;
        cursor_x = indent;
        if (t->closing) paragraph_break();
        return;
    }
    if (text_equal(n, "li")) {
        if (!t->closing) {
            line_break();
            place_word("*", 1, 0, -1);
            pending_space = true;
        }
        return;
    }
    if (text_equal(n, "td") || text_equal(n, "th")) {
        if (!t->closing && cursor_x > indent) { place_word("|", 1, 0, -1); pending_space = true; }
        return;
    }
    if (text_equal(n, "b") || text_equal(n, "strong")) { bold_depth += t->closing ? -1 : 1; if (bold_depth < 0) bold_depth = 0; return; }
    if (text_equal(n, "i") || text_equal(n, "em")) { italic_depth += t->closing ? -1 : 1; if (italic_depth < 0) italic_depth = 0; return; }

    if (text_equal(n, "a")) {
        if (!t->closing) {
            const char *href = attr(t, "href");
            if (href != NULL && href[0] != '\0' && current_focusable < 0) {
                current_focusable = new_focusable(FOCUS_LINK);
                if (current_focusable >= 0) {
                    doc->focusables[current_focusable].target = store(href, text_length(href));
                    doc->link_count++;
                }
                focus_label_length = 0;
            }
        } else if (current_focusable >= 0 && doc->focusables[current_focusable].kind == FOCUS_LINK) {
            struct page_focusable *f = &doc->focusables[current_focusable];
            if (focus_label_length == 0) {
                // A link with no words (a picture): show where it goes.
                const char *target = page_string(doc, f->target);
                place_word("[link]", 6, STYLE_LINK, current_focusable);
                f->label = store(target, text_length(target));
            } else {
                f->label = store(focus_label, focus_label_length);
            }
            current_focusable = -1;
        }
        return;
    }
    if (text_equal(n, "img") && !t->closing) {
        const char *alt = attr(t, "alt");
        if (alt != NULL && alt[0] != '\0') {
            char text[140] = "[";
            text_append(text, alt, 130);
            text_append(text, "]", sizeof(text));
            add_text(text, text_length(text));
        }
        return;
    }
    if (text_equal(n, "form")) {
        if (!t->closing && doc->form_count < MAX_FORMS) {
            current_form = doc->form_count++;
            const char *action = attr(t, "action");
            doc->forms[current_form].action = action ? store(action, text_length(action)) : -1;
            doc->forms[current_form].hidden_first = doc->hidden_count;
            doc->forms[current_form].hidden_count = 0;
        } else if (t->closing) {
            current_form = -1;
        }
        paragraph_break();
        return;
    }
    if (text_equal(n, "input") && !t->closing) {
        const char *type = attr(t, "type");
        const char *name = attr(t, "name");
        const char *value = attr(t, "value");
        if (type == NULL) type = "text";
        if (text_equal(type, "hidden")) {
            if (current_form >= 0 && name && doc->hidden_count < MAX_HIDDENS) {
                struct page_hidden *h = &doc->hiddens[doc->hidden_count++];
                h->name = store(name, text_length(name));
                h->value = store(value ? value : "", value ? text_length(value) : 0);
                h->form = current_form;
                doc->forms[current_form].hidden_count++;
            }
            return;
        }
        if (text_equal(type, "submit") || text_equal(type, "button") || text_equal(type, "image")) {
            int f = new_focusable(FOCUS_BUTTON);
            if (f < 0) return;
            const char *label = value ? value : "Submit";
            doc->focusables[f].label = store(label, text_length(label));
            doc->focusables[f].name = name ? store(name, text_length(name)) : -1;
            char shown[80] = "[ ";
            text_append(shown, label, 70);
            text_append(shown, " ]", sizeof(shown));
            pending_space = true;
            place_word(shown, text_length(shown), STYLE_BUTTON, f);
            pending_space = true;
            return;
        }
        if (text_equal(type, "text") || text_equal(type, "search") || text_equal(type, "email")
            || text_equal(type, "url") || text_equal(type, "number") || text_equal(type, "tel")
            || text_equal(type, "password")) {
            int f = new_focusable(FOCUS_FIELD);
            if (f < 0) return;
            doc->focusables[f].secret = text_equal(type, "password");
            doc->field_count++;
            const char *label = attr(t, "placeholder");
            if (label == NULL) label = attr(t, "aria-label");
            if (label == NULL) label = attr(t, "title");
            if (label == NULL) label = name ? name : "text";
            doc->focusables[f].label = store(label, text_length(label));
            doc->focusables[f].name = name ? store(name, text_length(name)) : -1;
            if (value) text_copy(doc->focusables[f].value, value, FIELD_MAX);
            // The box is drawn as a field 28 characters wide.
            pending_space = true;
            place_word("____________________________", 28, STYLE_FIELD, f);
            pending_space = true;
        }
        return;
    }
    if (text_equal(n, "button")) {
        if (!t->closing && current_focusable < 0) {
            current_focusable = new_focusable(FOCUS_BUTTON);
            if (current_focusable >= 0) {
                const char *name = attr(t, "name");
                doc->focusables[current_focusable].name = name ? store(name, text_length(name)) : -1;
            }
            focus_label_length = 0;
            pending_space = true;
            if (current_focusable >= 0) place_word("[", 1, STYLE_BUTTON, current_focusable);
        } else if (t->closing && current_focusable >= 0 && doc->focusables[current_focusable].kind == FOCUS_BUTTON) {
            struct page_focusable *f = &doc->focusables[current_focusable];
            if (focus_label_length == 0) {
                place_word("button", 6, STYLE_BUTTON, current_focusable);
                f->label = store("button", 6);
            } else {
                f->label = store(focus_label, focus_label_length);
            }
            place_word("]", 1, STYLE_BUTTON, current_focusable);
            current_focusable = -1;
            pending_space = true;
        }
        return;
    }
    if (is_block_tag(n)) {
        line_break();
        return;
    }
}

// Read a tag starting just after '<'. Returns where it ends (after '>').
static int read_tag(const char *s, int length, int i, struct tag *t) {
    t->closing = false;
    t->attr_count = 0;
    t->name[0] = '\0';
    if (i < length && s[i] == '/') { t->closing = true; i++; }
    int n = 0;
    while (i < length && (is_alnum(s[i]) || s[i] == '-')) {
        if (n < 15) t->name[n++] = to_lower(s[i]);
        i++;
    }
    t->name[n] = '\0';
    // Attributes: name, name=value, name="value", name='value'.
    while (i < length && s[i] != '>') {
        if (is_space(s[i]) || s[i] == '/') { i++; continue; }
        char aname[16];
        int an = 0;
        while (i < length && !is_space(s[i]) && s[i] != '=' && s[i] != '>' && s[i] != '/') {
            if (an < 15) aname[an++] = to_lower(s[i]);
            i++;
        }
        aname[an] = '\0';
        while (i < length && is_space(s[i])) i++;
        char *value = NULL;
        int slot = -1;
        if (t->attr_count < ATTR_MAX && an > 0) {
            slot = t->attr_count++;
            text_copy(t->attr_name[slot], aname, 16);
            value = t->attr_value[slot];
            value[0] = '\0';
        }
        if (i < length && s[i] == '=') {
            i++;
            while (i < length && is_space(s[i])) i++;
            char quote = (i < length && (s[i] == '"' || s[i] == '\'')) ? s[i++] : 0;
            int vn = 0;
            while (i < length && (quote ? s[i] != quote : (!is_space(s[i]) && s[i] != '>'))) {
                // Decode &amp; and friends inside values (common in links).
                char decoded[8];
                int used = decode_char(s + i, length - i, decoded);
                for (int k = 0; decoded[k] && value && vn < URL_MAX - 1; k++) value[vn++] = decoded[k];
                i += used;
            }
            if (value) value[vn] = '\0';
            if (quote && i < length) i++;
        }
        (void)slot;
    }
    return i < length ? i + 1 : length;
}

void page_parse(struct page_document *d, const char *html, int length, int width, bool plain) {
    doc = d;
    page_width = width;
    d->text_used = 0;
    d->box_count = d->line_count = d->focusable_count = d->form_count = d->hidden_count = 0;
    d->link_count = d->heading_count = d->field_count = 0;
    d->title[0] = '\0';
    cursor_x = cursor_y = indent = 0;
    current_line = -1;
    pending_space = false;
    last_was_gap = true;
    heading_level = bold_depth = italic_depth = pre_depth = list_depth = 0;
    current_focusable = -1;
    current_form = -1;

    if (plain) {
        pre_depth = 1;
        add_text(html, length);
        finish_line();
        d->height = cursor_y;
        return;
    }

    char text[512];
    int text_n = 0;
    int hidden_depth = 0;
    char hidden_name[16] = "";
    bool in_title = false;
    struct tag t;

    for (int i = 0; i < length;) {
        char c = html[i];
        if (c == '<') {
            // A comment, or <!DOCTYPE ...>.
            if (i + 3 < length && html[i + 1] == '!' && html[i + 2] == '-' && html[i + 3] == '-') {
                int j = i + 4;
                while (j + 2 < length && !(html[j] == '-' && html[j + 1] == '-' && html[j + 2] == '>')) j++;
                i = j + 3;
                continue;
            }
            if (i + 1 < length && (html[i + 1] == '!' || html[i + 1] == '?')) {
                while (i < length && html[i] != '>') i++;
                i++;
                continue;
            }
            if (i + 1 < length && !(is_alnum(html[i + 1]) || html[i + 1] == '/')) {
                // A lone "<" in the text.
                if (text_n < (int)sizeof(text) - 1) text[text_n++] = '<';
                i++;
                continue;
            }
            // Text so far goes out before the tag takes effect.
            if (text_n > 0 && hidden_depth == 0 && !in_title) add_text(text, text_n);
            if (text_n > 0 && in_title) {
                int tl = text_length(d->title);
                for (int k = 0; k < text_n && tl < (int)sizeof(d->title) - 1; k++) {
                    char ch = is_space(text[k]) ? ' ' : text[k];
                    if (ch == ' ' && (tl == 0 || d->title[tl - 1] == ' ')) continue;
                    d->title[tl++] = ch;
                }
                d->title[tl] = '\0';
            }
            text_n = 0;
            i = read_tag(html, length, i + 1, &t);
            if (text_equal(t.name, "title")) {
                in_title = !t.closing;
                continue;
            }
            if (text_equal(t.name, "body") && !t.closing) {
                hidden_depth = 0;   // whatever was left open in the head is over
                continue;
            }
            if (hidden_depth > 0) {
                if (t.closing && text_equal(t.name, hidden_name)) hidden_depth = 0;
                continue;
            }
            if (!t.closing && is_hidden_tag(t.name)) {
                hidden_depth = 1;
                text_copy(hidden_name, t.name, sizeof(hidden_name));
                continue;
            }
            handle_tag(&t);
            continue;
        }
        if (in_title || hidden_depth == 0) {
            char decoded[8];
            int used = decode_char(html + i, length - i, decoded);
            for (int k = 0; decoded[k]; k++) {
                if (text_n >= (int)sizeof(text) - 1) {
                    if (hidden_depth == 0 && !in_title) add_text(text, text_n);
                    text_n = 0;
                }
                text[text_n++] = decoded[k];
            }
            i += used;
        } else {
            i++;
        }
    }
    if (text_n > 0 && hidden_depth == 0) add_text(text, text_n);
    finish_line();
    d->height = cursor_y + 10;
}
