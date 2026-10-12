// app_browser.c - the Browser window.
//
// What it can do: open web pages over http and https, show their words,
// headings and links, follow links, fill in simple search boxes, keep
// bookmarks and go back and forward. Pages with JavaScript are run by the
// JavaScript engine (jshost.c) and shown as text again whenever they change.
// Pages are drawn by the web engine (web_*.c) with their own colors, fonts, layout and pictures; the
// words are also kept as lines for the keyboard and the screen reader. Alt+T switches to a plain text view.
// What it can't do yet: cookies, sending forms with POST, web fonts, video.
//
// Keys, when the page has focus (like a screen reader's browse mode):
//   Down / Up        read the next / previous line
//   Tab / Shift+Tab  next / previous link, text box or button
//   Enter            follow the link, or send the form
//   H / Shift+H      next / previous heading;  K: next link
//   Space, Page Down / Page Up   scroll a screen;  Home / End: top / bottom
//   Alt+D            the address bar;  Alt+Left or Backspace: back
//   Alt+J            what the page's scripts have written to their console
//   Alt+T            switch between the styled page and plain text

#include "browser.h"
#include "browser_tls.h"
#include "jshost.h"
#include "web.h"

#define TOOLBAR_H 56
#define PAGE_X 16
#define START_W 1000        // the window's size when it opens
#define START_H 620
// The page area follows the window's size (it can be maximized or snapped).
#define PAGE_W (content_w - 2 * PAGE_X - 12)
#define PAGE_H (content_h - TOOLBAR_H)
#define CONTENT_W content_w

enum { BACK, FORWARD, RELOAD, HOME, ADDRESS, GO, PAGE, ITEM_COUNT };

static struct page_document page;
static bool page_ready_memory = false;

// ---- The styled view (the web engine) ----
static struct page *wp = NULL;            // the page as the web engine has it
static bool want_styled = true;           // Alt+T switches to the plain text view
static bool rich = false;                 // the page on screen is drawn by the engine
static char *html_keep = NULL;            // a copy of the page's HTML (for plain text view and resizing)
static struct resource *res_cur = NULL;   // the style sheet or picture being fetched for the page
static char res_url[2100];
static int res_redirects = 0;
static int res_fetched = 0;               // pictures fetched for this page (there is a limit)
static uint64_t style_wait_until = 0;     // the page is held back until its style sheets arrive (or this time)
static bool announce_wait = false;        // the page's announcement waits for that
static int announce_code = 200;
static uint64_t relayout_at = 0;          // something arrived: lay the page out again at this time
static char known_host[128];              // the last website looked up (to save looking it up again)
static uint32_t known_ip = 0;
static unsigned hash_last = 0;
static int hash_len_last = -1;
#define IMAGE_LIMIT 80

static char address[URL_MAX];        // what's in the address bar
static bool address_fresh = false;   // the first key typed replaces it
static char current_url[URL_MAX];    // the page being shown
static char history[16][URL_MAX];
static int history_count = 0;
static char forward_list[16][URL_MAX];
static int forward_count = 0;

// Bookmarks (kept while the desktop is running).
#define BOOKMARK_MAX 24
static char bookmark_url[BOOKMARK_MAX][URL_MAX];
static char bookmark_name[BOOKMARK_MAX][64];
static int bookmark_count = 0;

// The page's source, kept so it can be laid out again when the window changes size.
static const char *source = NULL;
static int source_length = 0;
static bool source_plain = false;
static int laid_out_width = 0;

// Find in page, and reading aloud.
static bool find_active = false;
static char find_text[64];
static bool reading = false;
static int reading_line = 0;
static uint64_t reading_next_ms = 0;

static int scroll_y = 0;
static int read_line = 0;            // the line Down/Up reads
static int page_focus = -1;          // the link/field/button Tab is on
static int last_window_focus = -1;

static char summary[96];
static char page_name[140];

// ---- Fetching ----

enum fetch_state { IDLE, LOOKUP, CONNECT, RECEIVE, DONE };
static enum fetch_state state = IDLE;
static int state_frames = 0;
static char fetch_host[128], fetch_path[URL_MAX];
static int fetch_port = 80;
static bool fetch_secure = false;    // https: wrap the connection in TLS
static char fetch_request[URL_MAX + 300];
static int fetch_request_length;
static uint32_t fetch_ip;
static int connection = -1;
static char *raw;                    // the server's whole answer
static int raw_length;
#define RAW_MAX (2 * 1024 * 1024)
static int redirects = 0;
static char status_text[160];

// ---- Pages run by JavaScript ----

static bool js_on = false;           // the page is alive: its scripts are running
static bool js_dirty = false;        // the page changed since it was last shown
static bool js_shown = false;        // it has been shown at least once
static bool js_new_view = false;     // the next showing is a different "page" (scroll to the top, say so)
static bool js_can_back = false, js_can_forward = false;
static uint64_t js_next_pump = 0, js_render_at = 0;

static bool loading(void);
static void html_add_escaped(char *out, const char *text, int max);
static void read_next_line(void);
static void relayout(void);
static void announce_page(int code);
static void rich_layout(void);
static void relayout_keep(void);
static void end_style_wait(void);
static void stop_fetch(void);
static bool page_secure = false;
static void show_html(const char *html, const char *url, bool announce);
static void navigate(const char *url, bool add_to_history);
static void js_refresh_history_state(void);
static void relayout(void);

static void set_status(const char *text) {
    text_copy(status_text, text, sizeof(status_text));
    request_redraw();
}

// ---- Built-in pages ----

static const char *home_html =
    "<title>AREOS Browser</title>"
    "<h1>AREOS Browser</h1>"
    "<p>Type an address in the bar above, or search the web with FrogFind.</p>"
    "<form action=\"http://frogfind.com/\" method=\"get\">"
    "<input type=\"text\" name=\"q\" placeholder=\"Search with FrogFind\"> <input type=\"submit\" value=\"Search\">"
    "</form>"
    "<h2>Places to try</h2>"
    "<ul>"
    "<li><a href=\"http://frogfind.com/\">FrogFind</a>: a search engine made for old and simple browsers.</li>"
    "<li><a href=\"http://68k.news/\">68k.news</a>: today's headlines, in plain text.</li>"
    "<li><a href=\"http://info.cern.ch/hypertext/WWW/TheProject.html\">The very first website</a>, from 1991.</li>"
    "<li><a href=\"http://example.com/\">example.com</a>: a tiny test page.</li>"
    "<li><a href=\"about:bookmarks\">Your bookmarks</a> and <a href=\"about:history\">your history</a></li>"
    "<li><a href=\"http://textfiles.com/\">textfiles.com</a>: old computer text files.</li>"
    "</ul>"
    "<h2>Good to know</h2>"
    "<p>This browser opens http and secure https pages and draws them with their own colors, fonts, layout "
    "and pictures (PNG, JPEG, GIF and BMP). It runs JavaScript, so many modern sites work. It can't show "
    "video, web fonts or WebP pictures yet. Alt+T switches to a plain text view of the page. "
    "If a page misbehaves, Alt+J shows what its scripts reported.</p>"
    "<p>On a page: Down and Up read line by line, Tab moves between links, Enter follows a link, "
    "H jumps to the next heading, R reads the page aloud from here, and / finds words. "
    "B bookmarks the page. Alt+D goes to the address bar, Alt+B shows bookmarks, Alt+H shows history. "
    "Backspace or Alt+Left goes back, Alt+Right goes forward.</p>"
    "<p>Privacy: AREOS doesn't keep cookies or send anything about you. Websites you visit can see "
    "your internet address, like with any browser.</p>";

static void show_message(const char *title, const char *message, const char *url) {
    static char html[2048];
    text_copy(html, "<title>", sizeof(html));
    text_append(html, title, sizeof(html));
    text_append(html, "</title><h1>", sizeof(html));
    text_append(html, title, sizeof(html));
    text_append(html, "</h1><p>", sizeof(html));
    text_append(html, message, sizeof(html));
    text_append(html, "</p><ul><li><a href=\"about:home\">Go to the home page</a></li></ul>", sizeof(html));
    show_html(html, url, true);
}

// ---- Showing a page ----

static void update_summary(void) {
    summary[0] = '\0';
    text_append_number(summary, page.link_count, sizeof(summary));
    text_append(summary, page.link_count == 1 ? " link, " : " links, ", sizeof(summary));
    text_append_number(summary, page.heading_count, sizeof(summary));
    text_append(summary, page.heading_count == 1 ? " heading" : " headings", sizeof(summary));
    if (page.field_count > 0) {
        text_append(summary, ", ", sizeof(summary));
        text_append_number(summary, page.field_count, sizeof(summary));
        text_append(summary, page.field_count == 1 ? " text box" : " text boxes", sizeof(summary));
    }
    text_copy(page_name, page.title[0] ? page.title : current_url, sizeof(page_name));
}


// Is this summary's <details> open?
static bool summary_open(const struct page_focusable *f) {
    const struct node *n = f->node;
    return n && n->parent && node_has_attr(n->parent, "open");
}

// What kind of control a button is, and its state, in words ("check box, checked").
static void control_words(const struct page_focusable *f, char *out, int max) {
    switch (f->sub) {
    case SUB_CHECK: text_append(out, f->checked ? "check box, checked" : "check box, not checked", max); break;
    case SUB_RADIO: text_append(out, f->checked ? "radio button, selected" : "radio button, not selected", max); break;
    case SUB_SELECT: text_append(out, "menu", max); break;
    case SUB_SUMMARY: text_append(out, summary_open(f) ? "button, expanded" : "button, collapsed", max); break;
    case SUB_FILE: text_append(out, "file upload button, not supported yet", max); break;
    default: text_append(out, "button", max); break;
    }
}

// What a line says, for the screen reader: words, with links and
// headings marked.
static void describe_line(int line, char *out, int max) {
    out[0] = '\0';
    if (line < 0 || line >= page.line_count) return;
    struct page_line *l = &page.lines[line];
    if (l->heading > 0) {
        text_append(out, "Heading level ", max);
        text_append_number(out, l->heading, max);
        text_append(out, ": ", max);
    }
    int last_focusable = -2;
    for (int b = l->first_box; b < l->first_box + l->box_count; b++) {
        struct page_box *box = &page.boxes[b];
        if (text_length(out) > 0 && out[text_length(out) - 1] != ' ') text_append(out, " ", max);
        if (box->focusable >= 0) {
            struct page_focusable *f = &page.focusables[box->focusable];
            if (box->focusable != last_focusable) {
                if (f->kind == FOCUS_LINK) text_append(out, "link ", max);
                if (f->kind == FOCUS_FIELD) {
                    text_append(out, f->secret ? "password edit text " : "edit text ", max);
                    text_append(out, page_string(&page, f->label), max);
                    text_append(out, ": ", max);
                    text_append(out, f->secret ? (f->value[0] ? "hidden" : "blank") : (f->value[0] ? f->value : "blank"), max);
                    last_focusable = box->focusable;
                    continue;
                }
                if (f->kind == FOCUS_BUTTON) {
                    control_words(f, out, max);
                    text_append(out, " ", max);
                    text_append(out, page_string(&page, f->label), max);
                    if (f->sub == SUB_SELECT && f->value[0]) { text_append(out, ": ", max); text_append(out, f->value, max); }
                    last_focusable = box->focusable;
                    continue;
                }
            } else if (f->kind != FOCUS_LINK) {
                continue;
            }
            last_focusable = box->focusable;
        } else {
            last_focusable = -2;
        }
        int n = text_length(out);
        for (int k = 0; k < box->length && n < max - 1; k++) out[n++] = page.text[box->text + k];
        out[n] = '\0';
    }
    if (out[0] == '\0') text_copy(out, "blank", max);
}

static void scroll_to_line(int line) {
    if (line < 0 || line >= page.line_count) return;
    struct page_line *l = &page.lines[line];
    if (l->y < scroll_y + 8) scroll_y = l->y - 8;
    if (l->y + l->h > scroll_y + PAGE_H - 8) scroll_y = l->y + l->h - PAGE_H + 8;
    if (scroll_y < 0) scroll_y = 0;
    request_redraw();
}

static void say_line(int line) {
    char text[600];
    describe_line(line, text, sizeof(text));
    areos_speech_stop();
    gui_announce(text);
}

static void say_focusable(int f) {
    if (f < 0 || f >= page.focusable_count) return;
    struct page_focusable *p = &page.focusables[f];
    gui_line_start();
    gui_line_add(page_string(&page, p->label));
    if (p->kind == FOCUS_LINK) gui_line_add(", link.");
    if (p->kind == FOCUS_BUTTON) {
        char words[100] = ", ";
        control_words(p, words, sizeof(words));
        text_append(words, ".", sizeof(words));
        gui_line_add(words);
        if (p->sub == SUB_SELECT && p->value[0]) { gui_line_add(" "); gui_line_add(p->value); gui_line_add("."); }
    }
    if (p->kind == FOCUS_FIELD) {
        gui_line_add(p->secret ? ", password edit text, " : ", edit text, ");
        gui_line_add(p->secret ? (p->value[0] ? "hidden" : "blank") : (p->value[0] ? p->value : "blank"));
        gui_line_add(".");
    }
    gui_line_say();
}

static void js_stop(void) {
    if (js_on) jsh_page_stop();
    js_on = false;
    js_dirty = false;
    js_can_back = js_can_forward = false;
}


// ---- Showing a page with the web engine ----

// Keep a copy of the page's HTML (the network buffer is reused for pictures).
static void keep_source(const char *html, int length) {
    free(html_keep);
    html_keep = malloc((size_t)length + 1);
    if (!html_keep) { source = NULL; source_length = 0; return; }
    memcpy(html_keep, html, (size_t)length);
    html_keep[length] = '\0';
    source = html_keep;
    source_length = length;
}

// Plain text as a page: its characters, kept as they are.
static char *wrap_plain(const char *text, int length, int *out_length) {
    char *o = malloc((size_t)length * 5 + 200);
    if (!o) return NULL;
    const char *head = "<!DOCTYPE html><meta charset=\"utf-8\"><pre style=\"white-space:pre-wrap;margin:16px;font-size:15px\">";
    int n = text_length(head);
    memcpy(o, head, (size_t)n);
    for (int i = 0; i < length; i++) {
        char c = text[i];
        const char *r = c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : NULL;
        if (r) { int k = text_length(r); memcpy(o + n, r, (size_t)k); n += k; }
        else o[n++] = c;
    }
    const char *tail = "</pre>";
    memcpy(o + n, tail, 6);
    n += 6;
    o[n] = '\0';
    *out_length = n;
    return o;
}

static void reset_view_state(void) {
    style_wait_until = 0;
    announce_wait = false;
    relayout_at = 0;
    res_fetched = 0;
    hash_len_last = -1;
}

// Make the web engine's page from this HTML (the old page goes first). False: no memory.
static bool rich_load(const char *html, int length, const char *url, bool scripting) {
    char saved_url[2100];
    bool had_resource = res_cur != NULL;
    if (had_resource) { text_copy(saved_url, res_url, sizeof(saved_url)); res_cur = NULL; }
    if (wp) { web_page_free(wp); wp = NULL; }
    rich = false;
    web_cache_trim();
    wp = web_page_new();
    if (!wp) { if (had_resource) stop_fetch(); return false; }
    web_page_load(wp, html, length, url, scripting);
    if (!wp->root) { web_page_free(wp); wp = NULL; if (had_resource) stop_fetch(); return false; }
    // a picture that was on its way may still be wanted by the new page
    if (had_resource) {
        bool found = false;
        rich_layout();
        for (struct resource *r = wp->resources; r; r = r->next) {
            if (r->state == RS_WANTED && text_equal(r->url, saved_url)) {
                r->state = RS_REQUESTED;
                if (r->image) r->image->state = IMG_LOADING;
                res_cur = r;
                found = true;
                break;
            }
        }
        if (!found) stop_fetch();
    }
    return true;
}

static void rich_layout(void) {
    web_page_layout(wp, content_w, PAGE_H, !light_mode);
    web_doc_fill(&page, wp);
    laid_out_width = content_w;
    rich = true;
}

// Lay the page out again (a picture or style sheet arrived), keeping the place on the page.
static void relayout_keep(void) {
    if (!rich || !wp) return;
    rich_layout();
    if (scroll_y > page.height - PAGE_H) scroll_y = page.height - PAGE_H;
    if (scroll_y < 0) scroll_y = 0;
    if (read_line >= page.line_count) read_line = page.line_count - 1;
    if (read_line < 0) read_line = 0;
    if (page_focus >= page.focusable_count) page_focus = -1;
    request_redraw();
}

// Read the page into `page` (and the engine) in the view that's wanted. `plain`: it's a text file.
static void load_view(const char *html, int length, bool plain, bool scripting) {
    reset_view_state();
    if (want_styled) {
        const char *use = html;
        int use_length = length;
        char *wrapped = NULL;
        if (plain) { wrapped = wrap_plain(html, length, &use_length); use = wrapped ? wrapped : html; }
        bool ok = rich_load(use, use_length, current_url, scripting);
        free(wrapped);
        if (ok) {
            if (!rich) rich_layout();
            return;
        }
    }
    if (wp) { web_page_free(wp); wp = NULL; }
    rich = false;
    laid_out_width = PAGE_W;
    page_parse(&page, html, length, PAGE_W, plain);
}

// A new page is in: say what it is, once its styles are in (or have taken too long).
static void view_ready(int code) {
    if (rich && wp && !web_styles_ready(wp)) {
        style_wait_until = now_ms + 8000;
        announce_wait = true;
        announce_code = code;
        set_status("Loading the page's styles...");
        return;
    }
    announce_page(code);
}

static void end_style_wait(void) {
    style_wait_until = 0;
    status_text[0] = '\0';
    if (rich && wp) {
        rich_layout();
        update_summary();
        if (scroll_y > page.height - PAGE_H) scroll_y = page.height - PAGE_H;
        if (scroll_y < 0) scroll_y = 0;
    }
    if (announce_wait) {
        announce_wait = false;
        read_line = 0;
        page_focus = -1;
        announce_page(announce_code);
    }
    request_redraw();
}

// ---- Fetching a page's style sheets and pictures ----

static void resource_done(struct resource *r) {
    if (!wp) return;
    if (r->kind != RES_IMAGE && style_wait_until && web_styles_ready(wp)) { end_style_wait(); return; }
    if (web_pending_count(wp, true) == 0) relayout_at = now_ms;
    else if (relayout_at == 0) relayout_at = now_ms + (style_wait_until ? 0 : 600);
}

static void skip_resource(struct resource *r) {
    r->state = RS_FAILED;
    if (r->image) r->image->state = IMG_FAILED;
}

static bool start_resource(struct resource *r) {
    if (!url_split(r->url, fetch_host, sizeof(fetch_host), &fetch_port, fetch_path, sizeof(fetch_path), &fetch_secure)
        || areos_net_address() == 0) {
        skip_resource(r);
        return false;
    }
    stop_fetch();
    res_cur = r;
    text_copy(res_url, r->url, sizeof(res_url));
    res_redirects = 0;
    if (r->kind == RES_IMAGE) res_fetched++;
    raw_length = 0;
    state_frames = 0;
    if (known_ip != 0 && text_equal(known_host, fetch_host)) { fetch_ip = known_ip; state = CONNECT; }
    else state = LOOKUP;
    return true;
}

static void pump_resources(void) {
    if (!rich || !wp || res_cur) return;
    if (state != IDLE && state != DONE) return;
    for (;;) {
        struct resource *r = web_next_resource(wp);
        if (!r) return;
        if (r->kind == RES_IMAGE && res_fetched >= IMAGE_LIMIT) { skip_resource(r); continue; }
        if (start_resource(r)) return;
    }
}

// The scripts want the network for a moment: put the page's file back in the queue.
void browser_pause_resources(void) {
    if (!res_cur) return;
    if (connection >= 0) areos_tcp_close(connection);
    connection = -1;
    if (res_cur->state == RS_REQUESTED) {
        res_cur->state = RS_WANTED;
        if (res_cur->image) res_cur->image->state = IMG_WAITING;
        if (res_cur->kind == RES_IMAGE && res_fetched > 0) res_fetched--;
    }
    res_cur = NULL;
    state = IDLE;
}

static bool resource_failed(void) {
    if (!res_cur) return false;
    struct resource *r = res_cur;
    stop_fetch();
    if (wp) { web_resource_arrived(wp, r, NULL, 0, NULL); resource_done(r); }
    return true;
}

static void show_html(const char *html, const char *url, bool announce) {
    if (!page_ready_memory) return;
    js_stop();
    text_copy(current_url, url, sizeof(current_url));
    if (!text_starts_with(url, "about:")) text_copy(address, url, sizeof(address));
    else address[0] = '\0';
    stop_fetch();
    source = html;
    source_length = text_length(html);
    source_plain = false;
    load_view(html, source_length, false, false);
    scroll_y = 0;
    read_line = 0;
    page_focus = -1;
    reading = false;
    update_summary();
    status_text[0] = '\0';
    windows[WIN_BROWSER].focus = PAGE;
    request_redraw();
    if (announce) {
        areos_speech_stop();
        gui_line_start();
        gui_line_add(page_name);
        gui_line_add(". Web page, ");
        gui_line_add(summary);
        gui_line_add(".");
        gui_line_say();
        if (detailed) gui_announce("Down arrow reads it line by line, Tab moves between links, H jumps to headings.");
    }
}

static void announce_page(int code) {
    areos_speech_stop();
    gui_line_start();
    gui_line_add(page_name);
    if (page_secure) gui_line_add(". Secure connection");
    if (code != 200) {
        gui_line_add(". The website says: error ");
        gui_line_add_number(code);
    }
    gui_line_add(". Web page, ");
    gui_line_add(summary);
    gui_line_add(".");
    gui_line_say();
    if (page.line_count > 0) say_line(0);
}

// ---- Fetching pages ----

static void stop_fetch(void) {
    if (connection >= 0) areos_tcp_close(connection);
    connection = -1;
    state = IDLE;
    res_cur = NULL;
}

static void begin_fetch(const char *url) {
    stop_fetch();
    js_stop();
    source = NULL;   // the new page will overwrite the old one's text
    if (!url_split(url, fetch_host, sizeof(fetch_host), &fetch_port, fetch_path, sizeof(fetch_path), &fetch_secure)) {
        show_message("Can't open that address", "AREOS couldn't understand that web address.", url);
        return;
    }
    if (areos_net_address() == 0) {
        show_message("No network", "AREOS isn't connected to a network. (In QEMU, it needs a network card: "
                     "make run-bios adds one.)", url);
        return;
    }
    text_copy(current_url, url, sizeof(current_url));
    text_copy(address, url, sizeof(address));
    raw_length = 0;
    state = LOOKUP;
    state_frames = 0;
    char text[200];
    text_copy(text, fetch_secure ? "Loading securely from " : "Loading ", sizeof(text));
    text_append(text, fetch_host, sizeof(text));
    text_append(text, "...", sizeof(text));
    set_status(text);
    areos_speech_stop();
    gui_announce(text);
}

// Turn "Transfer-Encoding: chunked" bodies back into one piece, in place.
static int unchunk(char *body, int length) {
    int in = 0, out = 0;
    while (in < length) {
        int size = 0, digits = 0;
        while (in < length) {
            char c = to_lower(body[in]);
            if (c >= '0' && c <= '9') size = size * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') size = size * 16 + (c - 'a' + 10);
            else break;
            in++;
            digits++;
        }
        while (in < length && body[in] != '\n') in++;   // rest of the size line
        in++;
        if (digits == 0 || size == 0) break;
        if (in + size > length) size = length - in;
        for (int k = 0; k < size; k++) body[out + k] = body[in + k];
        out += size;
        in += size + 2;   // skip the \r\n after the piece
    }
    return out;
}

// Find a header's value ("Location: ..."), copying it into out.
static bool header_value(const char *headers, int length, const char *name, char *out, int max) {
    int name_length = text_length(name);
    for (int i = 0; i < length; i++) {
        if ((i == 0 || headers[i - 1] == '\n') && text_starts_with(headers + i, name)
            && headers[i + name_length] == ':') {
            int j = i + name_length + 1;
            while (j < length && headers[j] == ' ') j++;
            int n = 0;
            while (j < length && headers[j] != '\r' && headers[j] != '\n' && n < max - 1) out[n++] = headers[j++];
            out[n] = '\0';
            return true;
        }
    }
    return false;
}

static bool has_script(const char *html, int length) {
    for (int i = 0; i + 7 < length; i++) {
        if (html[i] == '<' && to_lower(html[i + 1]) == 's' && to_lower(html[i + 2]) == 'c'
            && to_lower(html[i + 3]) == 'r' && to_lower(html[i + 4]) == 'i' && to_lower(html[i + 5]) == 'p'
            && to_lower(html[i + 6]) == 't') return true;
    }
    return false;
}

static void finish_resource(void);

static void finish_fetch(void) {
    if (res_cur) { finish_resource(); return; }
    page_secure = fetch_secure;
    state = DONE;
    if (connection >= 0) areos_tcp_close(connection);
    connection = -1;
    // Split the answer into headers and the page itself.
    int body = -1;
    for (int i = 0; i + 3 < raw_length; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') { body = i + 4; break; }
    }
    if (body < 0 || raw_length < 12 || !text_starts_with(raw, "HTTP/")) {
        show_message("The page didn't load", "The website's answer couldn't be understood, or it didn't answer.", current_url);
        return;
    }
    int code = 0;
    for (int i = 9; i < 12; i++) code = code * 10 + (raw[i] - '0');
    char value[URL_MAX];
    if (code >= 300 && code < 400 && header_value(raw, body, "location", value, sizeof(value))) {
        if (++redirects > 5) {
            show_message("Too many redirects", "The website kept sending AREOS somewhere else.", current_url);
            return;
        }
        char next[URL_MAX];
        url_resolve(current_url, value, next, sizeof(next));
        navigate(next, false);
        return;
    }
    redirects = 0;
    int length = raw_length - body;
    char *content = raw + body;
    if (header_value(raw, body, "transfer-encoding", value, sizeof(value)) && text_starts_with(value, "chunked")) {
        length = unchunk(content, length);
    }
    content[length] = '\0';
    bool plain = false;
    if (header_value(raw, body, "content-type", value, sizeof(value))) {
        if (text_starts_with(value, "text/plain")) plain = true;
        else if (!text_starts_with(value, "text/html") && !text_starts_with(value, "application/xhtml")) {
            char message[300] = "This address is a file of type ";
            text_append(message, value, sizeof(message));
            text_append(message, ". AREOS can only show web pages and text so far.", sizeof(message));
            show_message("Can't show this file", message, current_url);
            return;
        }
    }
    text_copy(address, current_url, sizeof(address));
    keep_source(content, length);
    source_plain = plain;
    bool run_scripts = !plain && has_script(content, length);
    load_view(source ? source : content, length, plain, false);
    bool scripted = false;
    if (run_scripts) {
        // Run the page's scripts. Until they have run, the page shows as the server sent it.
        js_stop();
        if (jsh_page_start(current_url, content, length)) {
            js_on = true;
            js_dirty = true;
            js_shown = false;
            js_new_view = false;
            js_next_pump = 0;
            js_render_at = 0;
            scripted = true;
        }
    }
    if (!scripted && !plain && page.box_count < (rich ? 6 : 25) && has_script(content, length)) {
        // Many modern sites are an empty page that JavaScript fills in. AREOS has no
        // JavaScript yet, so say so instead of showing a blank window.
        char message[1000];
        text_copy(message, "This page is built with JavaScript, which AREOS can't run yet, so there is "
                           "almost nothing here for the browser to show.", sizeof(message));
        if (page.title[0]) {
            text_append(message, " The page's title is: ", sizeof(message));
            html_add_escaped(message, page.title, sizeof(message));
            text_append(message, ".", sizeof(message));
        }
        if (page.box_count > 0) {
            // Whatever text the page does have (for example its <noscript> message).
            text_append(message, " What the page does show: ", sizeof(message));
            char shown[240];
            int n = 0;
            for (int b = 0; b < page.box_count && n < 220; b++) {
                if (n > 0) shown[n++] = ' ';
                for (int k = 0; k < page.boxes[b].length && n < 220; k++) shown[n++] = page.text[page.boxes[b].text + k];
            }
            shown[n] = '\0';
            html_add_escaped(message, shown, sizeof(message));
        }
        text_append(message, " Many sites also have a simpler version of their pages, or a text-only link.", sizeof(message));
        show_message("This page needs JavaScript", message, current_url);
        return;
    }
    reading = false;
    scroll_y = 0;
    read_line = 0;
    page_focus = -1;
    update_summary();
    status_text[0] = '\0';
    windows[WIN_BROWSER].focus = PAGE;
    request_redraw();
    if (scripted) {
        set_status("Running the page's scripts...");
        js_refresh_history_state();
        return;
    }
    view_ready(code);
}

// A style sheet or picture has come in.
static void finish_resource(void) {
    if (connection >= 0) areos_tcp_close(connection);
    connection = -1;
    state = IDLE;
    struct resource *r = res_cur;
    int body = -1;
    for (int i = 0; i + 3 < raw_length; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') { body = i + 4; break; }
    }
    int code = 0;
    if (body >= 0 && raw_length >= 12 && text_starts_with(raw, "HTTP/")) {
        for (int i = 9; i < 12; i++) code = code * 10 + (raw[i] - '0');
    }
    char value[URL_MAX];
    if (code >= 300 && code < 400 && header_value(raw, body, "location", value, sizeof(value)) && res_redirects++ < 4) {
        char next[URL_MAX * 2];
        url_resolve(res_url, value, next, sizeof(next));
        if (url_split(next, fetch_host, sizeof(fetch_host), &fetch_port, fetch_path, sizeof(fetch_path), &fetch_secure)) {
            text_copy(res_url, next, sizeof(res_url));
            raw_length = 0;
            state_frames = 0;
            state = (known_ip != 0 && text_equal(known_host, fetch_host)) ? CONNECT : LOOKUP;
            return;
        }
    }
    char *data = NULL;
    int length = 0;
    if (code == 200 && body >= 0) {
        data = raw + body;
        length = raw_length - body;
        if (header_value(raw, body, "transfer-encoding", value, sizeof(value)) && text_starts_with(value, "chunked"))
            length = unchunk(data, length);
        data[length] = '\0';
    }
    res_cur = NULL;
    if (wp) {
        web_resource_arrived(wp, r, data, length, NULL);
        resource_done(r);
    }
}


// ---- Running a page's scripts ----

static void js_refresh_history_state(void) {
    js_can_back = js_on && jsh_history_can_go(-1);
    js_can_forward = js_on && jsh_history_can_go(1);
}

static bool same_before_hash(const char *a, const char *b) {
    for (int i = 0;; i++) {
        bool a_end = a[i] == '\0' || a[i] == '#', b_end = b[i] == '\0' || b[i] == '#';
        if (a_end || b_end) return a_end && b_end;
        if (a[i] != b[i]) return false;
    }
}

// Show the page as it is now: ask the engine for it as simple HTML and lay it out,
// keeping the place on the page and the control Tab was on.
static void js_show_render(void) {
    if (!js_on || !page_ready_memory) return;
    int length;
    const char *html = want_styled ? jsh_render_full(&length) : jsh_render(&length);
    if (length <= 0 && !js_on) return;
    int focus_aid = (page_focus >= 0 && page_focus < page.focusable_count) ? page.focusables[page_focus].aid : 0;
    bool first = !js_shown;
    bool new_view = first || js_new_view;
    int old_scroll = scroll_y;
    // The same page as last time: nothing to do (unless the window changed size).
    unsigned hash = 2166136261u;
    for (int i = 0; i < length; i++) hash = (hash ^ (unsigned char)html[i]) * 16777619u;
    if (!new_view && rich && wp && length == hash_len_last && hash == hash_last) {
        js_dirty = false;
        js_render_at = 0;
        if (laid_out_width != content_w) relayout_keep();
        return;
    }
    source = html;
    source_length = length;
    source_plain = false;
    load_view(html, length, false, true);
    hash_last = hash;
    hash_len_last = length;
    js_dirty = false;
    js_render_at = 0;
    js_shown = true;
    js_new_view = false;
    update_summary();
    if (new_view) {
        scroll_y = 0;
        read_line = 0;
        page_focus = -1;
        reading = false;
        status_text[0] = '\0';
        windows[WIN_BROWSER].focus = PAGE;
        view_ready(200);
    } else {
        page_focus = -1;
        if (focus_aid > 0) {
            for (int f = 0; f < page.focusable_count; f++) {
                if (page.focusables[f].aid == focus_aid) { page_focus = f; break; }
            }
        }
        scroll_y = old_scroll;
        if (scroll_y > page.height - PAGE_H) scroll_y = page.height - PAGE_H;
        if (scroll_y < 0) scroll_y = 0;
        if (read_line >= page.line_count) read_line = page.line_count - 1;
        if (read_line < 0) read_line = 0;
        status_text[0] = '\0';
    }
    request_redraw();
}

static void js_tick(void) {
    if (!js_on) return;
    if (now_ms >= js_next_pump) {
        int flags = jsh_pump();
        js_next_pump = now_ms + ((flags & JSH_LOADING) ? 0 : 16);
        if (flags & JSH_FATAL) {
            // The engine gave up (it ran out of memory, say).
            js_stop();
            if (!js_shown) {
                show_message("This page's scripts stopped", "The scripts on this page used too much memory or time, so "
                             "AREOS stopped them. Press Alt+J to see what they reported.", current_url);
            } else {
                gui_announce("The page's scripts stopped working, so the page can't change any more.");
                set_status("The page's scripts stopped.");
            }
            return;
        }
        const char *said;
        while ((said = jsh_announcement()) != NULL) gui_announce(said);
        if (flags & JSH_TITLE) { jsh_title(); flags |= JSH_DIRTY; }
        if (flags & JSH_URL) {
            int mode = 0;
            const char *url = jsh_url(&mode);
            if (mode == 0 && !same_before_hash(url, current_url)) js_new_view = true;
            if (mode == 2) js_new_view = true;
            text_copy(current_url, url, sizeof(current_url));
            text_copy(address, url, sizeof(address));
            js_refresh_history_state();
            flags |= JSH_DIRTY;
        }
        if (flags & JSH_DIRTY) {
            js_dirty = true;
            if (flags & JSH_LOADING) { if (js_render_at == 0) js_render_at = now_ms + 1500; }
            else if (js_new_view || js_render_at == 0 || js_render_at > now_ms + 120) js_render_at = now_ms + 120;
        }
        if (!(flags & JSH_LOADING) && js_dirty && js_render_at > now_ms + 120) js_render_at = now_ms + 120;
        if (jsh_wants_reload()) { redirects = 0; navigate(current_url, false); return; }
        if (flags & JSH_NAV) {
            char url[URL_MAX];
            text_copy(url, jsh_nav_url(), sizeof(url));
            bool replace = jsh_nav_replace();
            bool post = text_equal(jsh_nav_method(), "POST");
            if (post) {
                gui_announce("This page tried to send a form with POST, which AREOS can't do yet.");
            } else {
                redirects = 0;
                navigate(url, !replace);
                return;
            }
        }
    }
    if (js_on && js_dirty && js_render_at != 0 && now_ms >= js_render_at) js_show_render();
}

static void tick(void) {
    if (reading && now_ms >= reading_next_ms) read_next_line();
    js_tick();
    // The window was resized (maximized, snapped...): lay the page out again.
    if (page_ready_memory && (source != NULL || rich) && !loading() && laid_out_width != (rich ? content_w : PAGE_W)) relayout();
    if (style_wait_until && now_ms >= style_wait_until) end_style_wait();
    if (relayout_at && now_ms >= relayout_at && !style_wait_until) {
        relayout_at = 0;
        if (wp && rich && wp->needs_layout) relayout_keep();
    }
    if ((state == IDLE || state == DONE) && rich && wp && !res_cur) pump_resources();
    if (state == IDLE || state == DONE) return;
    // Let a frame be drawn (showing what's happening) before anything that waits.
    if (state_frames++ < 1) return;
    if (state == LOOKUP) {
        fetch_ip = areos_net_lookup(fetch_host);
        if (fetch_ip == 0) {
            state = DONE;
            if (resource_failed()) return;
            char message[200] = "AREOS couldn't find the website ";
            text_append(message, fetch_host, sizeof(message));
            text_append(message, ". Check the address, and that your computer is online.", sizeof(message));
            show_message("Website not found", message, current_url);
            return;
        }
        text_copy(known_host, fetch_host, sizeof(known_host));
        known_ip = fetch_ip;
        state = CONNECT;
        state_frames = 0;
        if (!res_cur) set_status("Connecting...");
        return;
    }
    if (state == CONNECT) {
        connection = areos_tcp_connect(fetch_ip, (uint16_t)fetch_port);
        if (connection < 0) {
            state = DONE;
            if (resource_failed()) return;
            show_message("Couldn't connect", "The website didn't answer. It may be down, or "
                         "on a different port.", current_url);
            return;
        }
        text_copy(fetch_request, "GET ", sizeof(fetch_request));
        text_append(fetch_request, fetch_path, sizeof(fetch_request));
        text_append(fetch_request, " HTTP/1.0\r\nHost: ", sizeof(fetch_request));
        text_append(fetch_request, fetch_host, sizeof(fetch_request));
        text_append(fetch_request, "\r\nUser-Agent: AREOS-Browser/2 (hobby operating system)\r\n", sizeof(fetch_request));
        if (res_cur && res_cur->kind == RES_IMAGE)
            text_append(fetch_request, "Accept: image/png,image/jpeg,image/gif,image/bmp,image/*;q=0.5,*/*;q=0.1\r\n", sizeof(fetch_request));
        else if (res_cur)
            text_append(fetch_request, "Accept: text/css,*/*;q=0.1\r\n", sizeof(fetch_request));
        else
            text_append(fetch_request, "Accept: text/html, text/plain\r\n", sizeof(fetch_request));
        text_append(fetch_request, "Connection: close\r\n\r\n", sizeof(fetch_request));
        fetch_request_length = text_length(fetch_request);
        if (fetch_secure) {
            tls_start(connection, fetch_host);
            state = RECEIVE;
            if (!res_cur) set_status("Setting up a secure connection...");
            return;
        }
        int sent = 0;
        while (sent < fetch_request_length) {
            int n = areos_tcp_send(connection, fetch_request + sent, fetch_request_length - sent);
            if (n < 0) break;
            sent += n;
            if (n == 0) areos_sleep_ms(5);
        }
        state = RECEIVE;
        if (!res_cur) set_status("Receiving the page...");
        return;
    }
    if (state == RECEIVE && fetch_secure) {
        enum tls_status status = tls_step(fetch_request, fetch_request_length, raw, &raw_length, RAW_MAX);
        if (status == TLS_FAILED) {
            state = DONE;
            if (resource_failed()) return;
            stop_fetch();
            show_message("Couldn't make a secure connection", tls_error(), current_url);
            return;
        }
        if (status == TLS_DONE) { finish_fetch(); return; }
        if (res_cur) return;
        char text[80];
        text_copy(text, raw_length > 0 ? "Receiving the page securely... " : "Setting up a secure connection...", sizeof(text));
        if (raw_length > 0) {
            text_append_number(text, raw_length / 1024, sizeof(text));
            text_append(text, " KB", sizeof(text));
        }
        set_status(text);
        return;
    }
    if (state == RECEIVE) {
        for (int rounds = 0; rounds < 64; rounds++) {
            int room = RAW_MAX - 1 - raw_length;
            if (room <= 0) { finish_fetch(); return; }
            int n = areos_tcp_receive(connection, raw + raw_length, room > 65536 ? 65536 : room);
            if (n < 0) { finish_fetch(); return; }
            if (n == 0) break;
            raw_length += n;
        }
        if (res_cur) return;
        char text[64] = "Receiving the page... ";
        text_append_number(text, raw_length / 1024, sizeof(text));
        text_append(text, " KB", sizeof(text));
        set_status(text);
    }
}

// ---- Bookmarks and history pages ----

static void html_add_escaped(char *out, const char *text, int max) {
    int n = text_length(out);
    for (int i = 0; text[i] != '\0' && n < max - 8; i++) {
        const char *r = NULL;
        if (text[i] == '&') r = "&amp;";
        else if (text[i] == '<') r = "&lt;";
        else if (text[i] == '>') r = "&gt;";
        else if (text[i] == '"') r = "&quot;";
        if (r) { for (int k = 0; r[k]; k++) out[n++] = r[k]; }
        else out[n++] = text[i];
    }
    out[n] = '\0';
}

static void show_console(void) {
    static char html[24000];
    text_copy(html, "<title>Page console</title><h1>Page console</h1><p>What the last page's scripts wrote, "
                    "including errors. Newest last.</p><pre>", sizeof(html));
    const char *c = jsh_console();
    int n = text_length(c);
    if (n > 16000) c += n - 16000;
    html_add_escaped(html, c[0] ? c : "(nothing)", sizeof(html));
    text_append(html, "</pre><ul><li><a href=\"about:home\">Go to the home page</a></li></ul>", sizeof(html));
    show_html(html, "about:console", true);
}

static void show_bookmarks(void) {
    static char html[BOOKMARK_MAX * (URL_MAX + 160) + 1024];
    text_copy(html, "<title>Bookmarks</title><h1>Bookmarks</h1>", sizeof(html));
    if (bookmark_count == 0) {
        text_append(html, "<p>You haven't saved any bookmarks yet. On a page, press B to bookmark it.</p>", sizeof(html));
    } else {
        text_append(html, "<p>Bookmarks last until AREOS is turned off. Each has a Remove link after it.</p><ul>", sizeof(html));
        for (int i = 0; i < bookmark_count; i++) {
            text_append(html, "<li><a href=\"", sizeof(html));
            html_add_escaped(html, bookmark_url[i], sizeof(html));
            text_append(html, "\">", sizeof(html));
            html_add_escaped(html, bookmark_name[i][0] ? bookmark_name[i] : bookmark_url[i], sizeof(html));
            text_append(html, "</a> <a href=\"about:remove?", sizeof(html));
            text_append_number(html, i, sizeof(html));
            text_append(html, "\">Remove</a></li>", sizeof(html));
        }
        text_append(html, "</ul>", sizeof(html));
    }
    text_append(html, "<p><a href=\"about:home\">Home page</a></p>", sizeof(html));
    show_html(html, "about:bookmarks", true);
}

static void show_history(void) {
    static char html[16 * (URL_MAX + 100) + 1024];
    text_copy(html, "<title>History</title><h1>History</h1>", sizeof(html));
    if (history_count == 0) {
        text_append(html, "<p>Nothing yet. Pages you leave appear here, most recent first.</p>", sizeof(html));
    } else {
        text_append(html, "<ul>", sizeof(html));
        for (int i = history_count - 1; i >= 0; i--) {
            text_append(html, "<li><a href=\"", sizeof(html));
            html_add_escaped(html, history[i], sizeof(html));
            text_append(html, "\">", sizeof(html));
            html_add_escaped(html, history[i], sizeof(html));
            text_append(html, "</a></li>", sizeof(html));
        }
        text_append(html, "</ul>", sizeof(html));
    }
    text_append(html, "<p><a href=\"about:home\">Home page</a></p>", sizeof(html));
    show_html(html, "about:history", true);
}

static void add_bookmark(void) {
    if (current_url[0] == '\0' || text_starts_with(current_url, "about:")) {
        gui_announce("This page can't be bookmarked.");
        return;
    }
    for (int i = 0; i < bookmark_count; i++) {
        if (text_equal(bookmark_url[i], current_url)) {
            gui_announce("Already in your bookmarks.");
            return;
        }
    }
    if (bookmark_count == BOOKMARK_MAX) {
        gui_announce("The bookmarks list is full. Remove one first.");
        return;
    }
    text_copy(bookmark_url[bookmark_count], current_url, URL_MAX);
    text_copy(bookmark_name[bookmark_count], page_name, 64);
    bookmark_count++;
    gui_line_start();
    gui_line_add("Bookmarked: ");
    gui_line_add(page_name);
    gui_line_add(". Alt+B shows your bookmarks.");
    gui_line_say();
}

static void remove_bookmark(int index) {
    if (index < 0 || index >= bookmark_count) return;
    for (int i = index + 1; i < bookmark_count; i++) {
        text_copy(bookmark_url[i - 1], bookmark_url[i], URL_MAX);
        text_copy(bookmark_name[i - 1], bookmark_name[i], 64);
    }
    bookmark_count--;
    gui_announce("Bookmark removed.");
}

static void push_history(const char *url) {
    if (history_count == 16) {
        for (int i = 1; i < 16; i++) text_copy(history[i - 1], history[i], URL_MAX);
        history_count--;
    }
    text_copy(history[history_count++], url, URL_MAX);
}

static void push_forward(const char *url) {
    if (forward_count == 16) {
        for (int i = 1; i < 16; i++) text_copy(forward_list[i - 1], forward_list[i], URL_MAX);
        forward_count--;
    }
    text_copy(forward_list[forward_count++], url, URL_MAX);
}

static void navigate(const char *url, bool add_to_history) {
    if (add_to_history && current_url[0] && !text_equal(current_url, url)) {
        push_history(current_url);
        forward_count = 0;   // going somewhere new forgets "forward"
    }
    if (text_starts_with(url, "about:remove?")) {
        int n = 0;
        for (const char *d = url + 13; *d >= '0' && *d <= '9'; d++) n = n * 10 + (*d - '0');
        remove_bookmark(n);
        stop_fetch();
        show_bookmarks();
        return;
    }
    if (text_starts_with(url, "about:bookmarks")) { stop_fetch(); show_bookmarks(); return; }
    if (text_starts_with(url, "about:history")) { stop_fetch(); show_history(); return; }
    if (text_starts_with(url, "about:console")) { stop_fetch(); show_console(); return; }
    if (text_starts_with(url, "about:home") || url[0] == '\0') {
        stop_fetch();
        show_html(home_html, "about:home", true);
        return;
    }
    if (text_starts_with(url, "mailto:") || text_starts_with(url, "javascript:")) {
        gui_announce("AREOS can't open that kind of link yet.");
        return;
    }
    begin_fetch(url);
}

// What's typed in the address bar: an address, or something to search for.
static void go_to_address(void) {
    char url[URL_MAX];
    bool has_space = false, has_dot = false;
    for (int i = 0; address[i]; i++) {
        if (address[i] == ' ') has_space = true;
        if (address[i] == '.') has_dot = true;
    }
    if (address[0] == '\0') {
        navigate("about:home", true);
        return;
    }
    if (text_starts_with(address, "http://") || text_starts_with(address, "https://") || text_starts_with(address, "about:")) {
        text_copy(url, address, sizeof(url));
    } else if (has_dot && !has_space) {
        text_copy(url, "http://", sizeof(url));
        text_append(url, address, sizeof(url));
    } else {
        text_copy(url, "http://frogfind.com/?q=", sizeof(url));
        url_append_escaped(url, address, sizeof(url));
    }
    redirects = 0;
    navigate(url, true);
}

static void go_back(void) {
    if (js_on && js_can_back && jsh_history_go(-1)) { js_next_pump = 0; js_refresh_history_state(); return; }
    if (history_count == 0) {
        gui_announce("There's no page to go back to.");
        return;
    }
    char url[URL_MAX];
    text_copy(url, history[--history_count], sizeof(url));
    if (current_url[0]) push_forward(current_url);
    redirects = 0;
    navigate(url, false);
}

static void go_forward(void) {
    if (js_on && js_can_forward && jsh_history_go(1)) { js_next_pump = 0; js_refresh_history_state(); return; }
    if (forward_count == 0) {
        gui_announce("There's no page to go forward to.");
        return;
    }
    char url[URL_MAX];
    text_copy(url, forward_list[--forward_count], sizeof(url));
    if (current_url[0]) push_history(current_url);
    redirects = 0;
    navigate(url, false);
}

// Send a form: its address, then ?name=value&name=value.
static void submit_form(int form, int button) {
    char url[URL_MAX * 2];
    const char *action = (form >= 0 && page.forms[form].action >= 0) ? page_string(&page, page.forms[form].action) : "";
    if (action[0] == '\0') {
        text_copy(url, current_url, sizeof(url));
        for (int i = 0; url[i]; i++) if (url[i] == '?') { url[i] = '\0'; break; }
    } else {
        url_resolve(current_url, action, url, sizeof(url));
    }
    text_append(url, "?", sizeof(url));
    bool first = true;
    for (int f = 0; f < page.focusable_count; f++) {
        struct page_focusable *p = &page.focusables[f];
        if (p->form != form || p->name < 0) continue;
        if (p->kind == FOCUS_LINK) continue;
        const char *sent = p->kind == FOCUS_FIELD ? p->value : page_string(&page, p->label);
        char chosen[FIELD_MAX];
        if (p->kind == FOCUS_BUTTON) {
            if (p->sub == SUB_CHECK || p->sub == SUB_RADIO) {
                if (!p->checked) continue;
                sent = p->value;
            } else if (p->sub == SUB_SELECT) {
                web_doc_select_value(&page, f, chosen, sizeof(chosen));
                sent = chosen;
            } else if (f != button) continue;
        }
        if (!first) text_append(url, "&", sizeof(url));
        first = false;
        url_append_escaped(url, page_string(&page, p->name), sizeof(url));
        text_append(url, "=", sizeof(url));
        url_append_escaped(url, sent, sizeof(url));
    }
    for (int h = 0; h < page.hidden_count; h++) {
        if (page.hiddens[h].form != form) continue;
        if (!first) text_append(url, "&", sizeof(url));
        first = false;
        url_append_escaped(url, page_string(&page, page.hiddens[h].name), sizeof(url));
        text_append(url, "=", sizeof(url));
        url_append_escaped(url, page_string(&page, page.hiddens[h].value), sizeof(url));
    }
    char resolved[URL_MAX];
    text_copy(resolved, url, sizeof(resolved));
    redirects = 0;
    navigate(resolved, true);
}

static void activate(int f) {
    if (f < 0 || f >= page.focusable_count) return;
    struct page_focusable *p = &page.focusables[f];
    if (js_on && p->aid > 0) {
        // The page's scripts know this control: let them handle it.
        if (p->kind == FOCUS_FIELD) jsh_key(p->aid, "Enter");
        else jsh_click(p->aid);
        js_next_pump = 0;
        return;
    }
    if (p->kind == FOCUS_LINK) {
        char url[URL_MAX];
        url_resolve(current_url, page_string(&page, p->target), url, sizeof(url));
        redirects = 0;
        navigate(url, true);
    } else if (p->kind == FOCUS_FIELD) {
        submit_form(p->form, -1);
    } else if (p->sub == SUB_CHECK || p->sub == SUB_RADIO || p->sub == SUB_SUMMARY) {
        if (rich && wp && web_doc_toggle(&page, wp, f)) {
            if (p->sub == SUB_SUMMARY) { wp->needs_layout = true; relayout_keep(); }
            request_redraw();
            say_focusable(f);
        }
    } else if (p->sub == SUB_SELECT) {
        if (rich && wp) {
            web_doc_cycle_select(&page, wp, f);
            request_redraw();
            gui_line_start();
            gui_line_add("Selected ");
            gui_line_add(p->value[0] ? p->value : "nothing");
            gui_line_add(".");
            gui_line_say();
        }
    } else if (p->sub == SUB_FILE) {
        gui_announce("Choosing a file to upload isn't supported yet.");
    } else if (!rich || p->submits) {
        submit_form(p->form, f);
    } else {
        gui_announce("This button does something only a script can do.");
    }
}

// ---- The window ----

static bool loading(void) { return !res_cur && (state == LOOKUP || state == CONNECT || state == RECEIVE); }

static int build(struct gui_item *items, int max) {
    (void)max;
    int address_w = content_w - 340 - 108;
    if (address_w < 120) address_w = 120;
    items[BACK] = (struct gui_item){ .name = "Back", .role = GUI_ROLE_BUTTON, .action = ACTION(A_CONTENT, BACK),
        .x = 12, .y = 10, .w = 64, .h = 36, .disabled = history_count == 0 && !js_can_back };
    items[FORWARD] = (struct gui_item){ .name = "Forward", .role = GUI_ROLE_BUTTON, .action = ACTION(A_CONTENT, FORWARD),
        .x = 82, .y = 10, .w = 90, .h = 36, .disabled = forward_count == 0 && !js_can_forward };
    items[RELOAD] = (struct gui_item){ .name = loading() ? "Stop" : "Reload", .role = GUI_ROLE_BUTTON,
        .action = ACTION(A_CONTENT, RELOAD), .x = 178, .y = 10, .w = 80, .h = 36 };
    items[HOME] = (struct gui_item){ .name = "Home", .role = GUI_ROLE_BUTTON, .action = ACTION(A_CONTENT, HOME),
        .x = 264, .y = 10, .w = 70, .h = 36 };
    items[ADDRESS] = (struct gui_item){ .name = "Address", .role = GUI_ROLE_TEXT_FIELD, .action = ACTION(A_CONTENT, ADDRESS),
        .x = 340, .y = 10, .w = address_w, .h = 36, .value = address };
    items[GO] = (struct gui_item){ .name = "Go", .role = GUI_ROLE_BUTTON, .action = ACTION(A_CONTENT, GO),
        .x = 340 + address_w + 6, .y = 10, .w = 96, .h = 36 };
    items[PAGE] = (struct gui_item){ .name = page_name[0] ? page_name : "Web page", .role = GUI_ROLE_DOCUMENT,
        .action = ACTION(A_CONTENT, PAGE), .x = 0, .y = TOOLBAR_H, .w = CONTENT_W, .h = PAGE_H,
        .state = status_text[0] ? status_text : summary };
    return ITEM_COUNT;
}

static void draw_page(struct gui_canvas *c, bool page_has_focus) {
    struct gui_canvas view = *c;
    view.pixels += (size_t)TOOLBAR_H * c->width;
    view.height = PAGE_H;
    gui_fill(&view, 0, 0, view.width, view.height, light_mode ? 0xFFFFFF : 0x0D1419);
    int top = scroll_y;
    if (rich && wp && !style_wait_until) {
        wp->focus_cur = page_has_focus ? page_focus : -1;
        web_page_paint(wp, &view, scroll_y);
        // The line being read: a frame around it.
        if (page_has_focus && read_line >= 0 && read_line < page.line_count && page.lines[read_line].box_count > 0) {
            struct page_line *l = &page.lines[read_line];
            int x0 = 1 << 30, x1 = 0;
            for (int b = l->first_box; b < l->first_box + l->box_count; b++) {
                if (page.boxes[b].x < x0) x0 = page.boxes[b].x;
                if (page.boxes[b].x + page.boxes[b].w > x1) x1 = page.boxes[b].x + page.boxes[b].w;
            }
            int ry = l->y - top - 2;
            if (ry + l->h + 4 > 0 && ry < view.height && x1 > x0) {
                gui_frame(&view, x0 - 3, ry, x1 - x0 + 6, l->h + 4, 1, 0xFFFFFF);
                gui_frame(&view, x0 - 4, ry - 1, x1 - x0 + 8, l->h + 6, 1, theme->accent2);
            }
        }
    } else if (rich && wp) {
        // waiting for the page's styles
    } else {
    // The line being read: a soft highlight.
    if (page_has_focus && read_line >= 0 && read_line < page.line_count) {
        struct page_line *l = &page.lines[read_line];
        gui_fill(&view, 0, l->y - top - 2, view.width - 12, l->h + 2, gui_blend(light_mode ? 0xFFFFFF : 0x0D1419, theme->accent2, 34));
    }
    for (int i = 0; i < page.line_count; i++) {
        struct page_line *l = &page.lines[i];
        if (l->y + l->h < top) continue;
        if (l->y > top + PAGE_H) break;
        for (int b = l->first_box; b < l->first_box + l->box_count; b++) {
            struct page_box *box = &page.boxes[b];
            int x = PAGE_X + box->x, y = box->y - top + 2;
            char word[256];
            int n = box->length < 255 ? box->length : 255;
            for (int k = 0; k < n; k++) word[k] = page.text[box->text + k];
            word[n] = '\0';
            bool focused = page_has_focus && box->focusable >= 0 && box->focusable == page_focus;
            if (box->style & STYLE_FIELD) {
                struct page_focusable *f = &page.focusables[box->focusable];
                gui_rounded(&view, x, y - 4, box->w, box->h + 8, 4, light_mode ? 0xF4F7F9 : 0x1A252D);
                gui_frame(&view, x, y - 4, box->w, box->h + 8, focused ? 2 : 1, focused ? theme->accent : theme->panel_border);
                char masked[FIELD_MAX];
                const char *typed = f->value;
                if (f->secret) {
                    int n = text_length(f->value);
                    for (int k = 0; k < n; k++) masked[k] = '*';
                    masked[n] = '\0';
                    typed = masked;
                }
                const char *shown = f->value[0] ? typed : page_string(&page, f->label);
                gui_text(&view, x + 6, y, shown, f->value[0] ? theme->text : theme->text_dim, 1);
                if (focused) gui_fill(&view, x + 7 + gui_text_width(typed, 1), y, 2, 16, theme->accent);
                continue;
            }
            uint32_t color = theme->text;
            if (box->style & STYLE_LINK) color = light_mode ? 0x1554A8 : 0x6CB8FF;
            if (box->style & STYLE_HEADING) color = theme->accent;
            if (box->style & STYLE_BUTTON) {
                gui_rounded(&view, x - 2, y - 4, box->w + 4, box->h + 8, 6, focused ? theme->selected : theme->hover);
            }
            if (box->style & STYLE_BOLD) gui_text_bold(&view, x, y, word, color, box->scale);
            else gui_text(&view, x, y, word, color, box->scale);
            if (box->style & STYLE_LINK) gui_fill(&view, x, y + 16 * box->scale, box->w, 1, color);
            if (focused) gui_frame(&view, x - 3, y - 3, box->w + 6, box->h + 6, 2, theme->accent);
        }
    }
    }
    // The scroll bar.
    if (page.height > PAGE_H) {
        int bar_h = PAGE_H * PAGE_H / page.height;
        if (bar_h < 30) bar_h = 30;
        int bar_y = (PAGE_H - bar_h) * scroll_y / (page.height - PAGE_H > 0 ? page.height - PAGE_H : 1);
        gui_rounded(&view, view.width - 9, bar_y, 6, bar_h, 3, theme->panel_border);
    }
    if (status_text[0]) {
        int w = gui_text_width(status_text, 1) + 24;
        gui_rounded(&view, 12, PAGE_H - 40, w, 30, 8, theme->panel);
        gui_frame(&view, 12, PAGE_H - 40, w, 30, 1, theme->accent2);
        gui_text(&view, 24, PAGE_H - 33, status_text, theme->text, 1);
    }
    if (find_active) {
        char bar[96] = "Find: ";
        text_append(bar, find_text, sizeof(bar));
        int w = 360;
        gui_rounded(&view, 12, 8, w, 34, 8, theme->panel);
        gui_frame(&view, 12, 8, w, 34, 2, theme->accent);
        gui_text(&view, 24, 16, bar, theme->text, 1);
        gui_fill(&view, 25 + gui_text_width(bar, 1), 15, 2, 18, theme->accent);
    } else if (reading) {
        gui_rounded(&view, 12, 8, 330, 30, 8, theme->panel);
        gui_text(&view, 24, 15, "Reading aloud. Press any key to stop.", theme->text, 1);
    }
    if (page_has_focus) gui_frame(&view, 0, 0, view.width, view.height, 2, theme->accent);
}

static void draw(struct gui_canvas *c, struct gui_item *items, int count, int focus) {
    gui_fill(c, 0, 0, c->width, TOOLBAR_H, theme->panel);
    for (int i = 0; i < count; i++) {
        struct gui_item *it = &items[i];
        bool focused = i == focus, hovered = i == hovered_window_item;
        if (i == PAGE) continue;
        if (it->role == GUI_ROLE_TEXT_FIELD) {
            gui_rounded(c, it->x, it->y, it->w, it->h, 18, theme->background);
            gui_frame(c, it->x + 8, it->y, it->w - 16, 1, 1, focused ? theme->accent : theme->panel_border);
            gui_frame(c, it->x + 8, it->y + it->h - 1, it->w - 16, 1, 1, focused ? theme->accent : theme->panel_border);
            const char *shown = address[0] ? address : "Type an address or search";
            // Show the end of a long address.
            int max_chars = (it->w - 32) / 8;
            int length = text_length(shown);
            if (address[0] && length > max_chars) shown += length - max_chars;
            else if (length > max_chars) { static char cut[64]; text_copy(cut, shown, max_chars > 0 && max_chars < 63 ? max_chars : 63); shown = cut; }
            gui_text(c, it->x + 16, it->y + 10, shown, address[0] ? theme->text : theme->text_dim, 1);
            if (focused) {
                if (address_fresh && address[0]) gui_frame(c, it->x + 2, it->y + 2, it->w - 4, it->h - 4, 2, theme->accent);
                else gui_fill(c, it->x + 17 + gui_text_width(shown, 1), it->y + 9, 2, 18, theme->accent);
            }
        } else {
            draw_window_item(c, it, focused, hovered);
        }
    }
    gui_fill(c, 0, TOOLBAR_H - 1, c->width, 1, theme->panel_border);
    draw_page(c, focus == PAGE);
}

// Typing in the address bar or a text box on the page.
static bool no_echo = false;   // typing in a password box: say nothing

static bool type_into(char *text, int max, struct areos_key_event *e, bool *fresh) {
    int length = text_length(text);
    if (e->key == AREOS_KEY_BACKSPACE) {
        if (fresh && *fresh) { text[0] = '\0'; *fresh = false; gui_announce("Cleared."); request_redraw(); return true; }
        if (length > 0) {
            text[length - 1] = '\0';
            gui_echo_backspace();
            request_redraw();
        }
        return true;
    }
    char ch = e->character;
    if (ch >= 32 && ch <= 126) {
        if (fresh && *fresh) { text[0] = '\0'; length = 0; *fresh = false; }
        if (length < max - 1) {
            text[length] = ch;
            text[length + 1] = '\0';
            if (!no_echo) gui_echo_char(ch);
            request_redraw();
        }
        return true;
    }
    return false;
}

static int next_focusable(int from, int step) {
    int f = from + step;
    if (f < 0 || f >= page.focusable_count) return -1;
    return f;
}

static void move_to_focusable(int f) {
    page_focus = f;
    if (f >= 0) {
        read_line = page.focusables[f].line;
        scroll_to_line(read_line);
        areos_speech_stop();
        say_focusable(f);
    }
    request_redraw();
}

static void move_to_line(int line) {
    if (line < 0) { gui_announce("Top of the page."); line = 0; }
    if (line >= page.line_count) { gui_announce("End of the page."); line = page.line_count - 1; }
    if (line < 0) return;
    read_line = line;
    // Enter should follow a link on this line: put Tab's place there too.
    struct page_line *l = &page.lines[line];
    for (int b = l->first_box; b < l->first_box + l->box_count; b++) {
        if (page.boxes[b].focusable >= 0) { page_focus = page.boxes[b].focusable; break; }
    }
    scroll_to_line(line);
    say_line(line);
}

// ---- Find in page ----

static bool line_contains(int line, const char *needle) {
    char text[600];
    int n = 0;
    struct page_line *l = &page.lines[line];
    for (int b = l->first_box; b < l->first_box + l->box_count && n < 580; b++) {
        struct page_box *box = &page.boxes[b];
        if (n > 0) text[n++] = ' ';
        for (int k = 0; k < box->length && n < 598; k++) text[n++] = to_lower(page.text[box->text + k]);
    }
    text[n] = '\0';
    int m = text_length(needle);
    for (int i = 0; i + m <= n; i++) {
        int k = 0;
        while (k < m && text[i + k] == to_lower(needle[k])) k++;
        if (k == m) return true;
    }
    return false;
}

static void do_find(int step) {
    if (find_text[0] == '\0') {
        gui_announce("Press the slash key, then type what to find.");
        return;
    }
    for (int i = 1; i <= page.line_count; i++) {
        int line = ((read_line + step * i) % page.line_count + page.line_count) % page.line_count;
        if (line_contains(line, find_text)) {
            move_to_line(line);
            return;
        }
    }
    gui_line_start();
    gui_line_add("Not found: ");
    gui_line_add(find_text);
    gui_line_add(".");
    gui_line_say();
}

// ---- Reading the page aloud ----

static void read_next_line(void) {
    if (reading_line >= page.line_count) {
        reading = false;
        gui_announce("End of the page.");
        request_redraw();
        return;
    }
    char text[600];
    describe_line(reading_line, text, sizeof(text));
    read_line = reading_line;
    scroll_to_line(reading_line);
    gui_announce(text);
    // Aim to send the next line a little before this one finishes being spoken.
    uint64_t speed = areos_setting_get(AREOS_SETTING_SPEECH_SPEED);
    if (speed == 0) speed = 110;
    uint64_t length_ms = (uint64_t)text_length(text) * 70 * 110 / speed;
    reading_next_ms = now_ms + (length_ms > 600 ? length_ms - 300 : 300);
    reading_line++;
    request_redraw();
}

static void start_reading(void) {
    areos_speech_stop();
    reading = true;
    reading_line = read_line;
    read_next_line();
}

// ---- Laying the page out again when the window changes size ----

static void relayout(void) {
    if (js_on && js_shown) { js_show_render(); return; }
    if (rich && wp) {
        int old_height = page.height > 0 ? page.height : 1;
        int old_scroll = scroll_y;
        relayout_keep();
        scroll_y = (int)((long)old_scroll * page.height / old_height);
        if (scroll_y > page.height - PAGE_H) scroll_y = page.height - PAGE_H;
        if (scroll_y < 0) scroll_y = 0;
        return;
    }
    if (source == NULL) return;
    static char saved[32][FIELD_MAX];
    int fields = 0;
    for (int f = 0; f < page.focusable_count && fields < 32; f++) {
        if (page.focusables[f].kind == FOCUS_FIELD) text_copy(saved[fields++], page.focusables[f].value, FIELD_MAX);
    }
    int old_height = page.height > 0 ? page.height : 1;
    page_parse(&page, source, source_length, PAGE_W, source_plain);
    laid_out_width = PAGE_W;
    int restored = 0;
    for (int f = 0; f < page.focusable_count && restored < fields; f++) {
        if (page.focusables[f].kind == FOCUS_FIELD) text_copy(page.focusables[f].value, saved[restored++], FIELD_MAX);
    }
    scroll_y = (int)((long)scroll_y * page.height / old_height);
    if (scroll_y > page.height - PAGE_H) scroll_y = page.height - PAGE_H;
    if (scroll_y < 0) scroll_y = 0;
    if (read_line >= page.line_count) read_line = page.line_count - 1;
    if (read_line < 0) read_line = 0;
    if (page_focus >= page.focusable_count) page_focus = -1;
    request_redraw();
}

static bool page_key(struct areos_key_event *e) {
    // Find box open: typing goes there.
    if (find_active) {
        if (e->key == AREOS_KEY_ESCAPE) {
            find_active = false;
            gui_announce("Find closed.");
        } else if (e->key == AREOS_KEY_ENTER) {
            find_active = false;
            do_find(1);
        } else {
            type_into(find_text, sizeof(find_text), e, NULL);
        }
        request_redraw();
        return true;
    }
    // Typing into a text box on the page.
    if (page_focus >= 0 && page.focusables[page_focus].kind == FOCUS_FIELD && e->key != AREOS_KEY_TAB
        && e->key != AREOS_KEY_ENTER && e->key != AREOS_KEY_UP && e->key != AREOS_KEY_DOWN && !alt_held) {
        struct page_focusable *field = &page.focusables[page_focus];
        no_echo = field->secret;
        bool typed = type_into(field->value, FIELD_MAX, e, NULL);
        no_echo = false;
        if (typed) {
            if (rich && wp) web_doc_set_value(&page, wp, page_focus);
            if (js_on && field->aid > 0) jsh_input(field->aid, field->value);
            return true;
        }
    }
    switch (e->key) {
    case AREOS_KEY_DOWN: move_to_line(read_line + 1); return true;
    case AREOS_KEY_UP:   move_to_line(read_line - 1); return true;
    case AREOS_KEY_TAB: {
        int f = next_focusable(page_focus, shift_held ? -1 : 1);
        if (page_focus < 0 && shift_held) f = -1;
        if (f < 0) { page_focus = -1; return false; }   // off the end: on to the next control
        move_to_focusable(f);
        return true;
    }
    case AREOS_KEY_ENTER:
        if (page_focus >= 0) activate(page_focus);
        else gui_announce("Nothing to follow here. Tab moves to the links.");
        return true;
    case AREOS_KEY_SPACE: case AREOS_KEY_PAGE_DOWN: case AREOS_KEY_PAGE_UP: {
        int step = e->key == AREOS_KEY_PAGE_UP ? -(PAGE_H - 60) : (PAGE_H - 60);
        scroll_y += step;
        if (scroll_y > page.height - PAGE_H) scroll_y = page.height - PAGE_H;
        if (scroll_y < 0) scroll_y = 0;
        for (int i = 0; i < page.line_count; i++) {
            if (page.lines[i].y >= scroll_y) { read_line = i; break; }
        }
        request_redraw();
        say_line(read_line);
        return true;
    }
    case AREOS_KEY_HOME: move_to_line(0); return true;
    case AREOS_KEY_END:  move_to_line(page.line_count - 1); return true;
    case AREOS_KEY_BACKSPACE: go_back(); return true;
    case AREOS_KEY_LEFT:
        if (alt_held) { go_back(); return true; }
        return true;
    case AREOS_KEY_RIGHT:
        if (alt_held) { go_forward(); return true; }
        return true;
    }
    char ch = e->character;
    if (ch == 'h' || ch == 'H') {
        int step = shift_held || ch == 'H' ? -1 : 1;
        for (int i = read_line + step; i >= 0 && i < page.line_count; i += step) {
            if (page.lines[i].heading > 0) { move_to_line(i); return true; }
        }
        gui_announce(step > 0 ? "No more headings." : "No headings before this.");
        return true;
    }
    if (ch == '/') {
        find_active = true;
        find_text[0] = '\0';
        gui_announce("Find in page. Type what to find, then press Enter. Escape cancels.");
        request_redraw();
        return true;
    }
    if (ch == 'n' || ch == 'N') { do_find(shift_held || ch == 'N' ? -1 : 1); return true; }
    if (ch == 'b' || ch == 'B') { add_bookmark(); return true; }
    if (ch == 'r' || ch == 'R') { start_reading(); return true; }
    if (ch == 'k' || ch == 'K') {
        int step = shift_held || ch == 'K' ? -1 : 1;
        for (int f = page_focus + step; f >= 0 && f < page.focusable_count; f += step) {
            if (page.focusables[f].kind == FOCUS_LINK) { move_to_focusable(f); return true; }
        }
        gui_announce(step > 0 ? "No more links." : "No links before this.");
        return true;
    }
    if (ch > ' ' && ch <= '~') return true;   // other letters do nothing on a page
    return false;
}


// Alt+T: the page as the site styled it, or as plain large text.
static void toggle_view(void) {
    want_styled = !want_styled;
    if (js_on && js_shown) {
        hash_len_last = -1;
        js_new_view = false;
        js_show_render();
    } else if (source != NULL) {
        int old = scroll_y;
        load_view(source, source_length, source_plain, false);
        scroll_y = old;
        if (scroll_y > page.height - PAGE_H) scroll_y = page.height - PAGE_H;
        if (scroll_y < 0) scroll_y = 0;
        read_line = 0;
        page_focus = -1;
        update_summary();
    }
    gui_announce(want_styled ? "Styled page." : "Plain text view.");
    request_redraw();
}

static bool key(struct areos_key_event *e, struct gui_item *items, int count, int *focus) {
    (void)items; (void)count;
    if (*focus != last_window_focus) {
        address_fresh = *focus == ADDRESS;
        last_window_focus = *focus;
    }
    // Any key stops reading aloud.
    if (reading) {
        reading = false;
        areos_speech_stop();
        request_redraw();
        if (e->key == AREOS_KEY_CTRL) return true;
    }
    if (e->key == AREOS_KEY_F1 + 4) { redirects = 0; navigate(current_url, false); return true; }   // F5
    if (alt_held && e->key == 'b') { navigate("about:bookmarks", true); return true; }
    if (alt_held && e->key == 'h') { navigate("about:history", true); return true; }
    if (alt_held && e->key == 'j') { navigate("about:console", true); return true; }
    if (alt_held && e->key == 't') { toggle_view(); return true; }
    if (alt_held && e->key == AREOS_KEY_RIGHT) { go_forward(); return true; }
    // Alt+D: the address bar, from anywhere in the browser.
    if (alt_held && e->key == 'd') {
        *focus = ADDRESS;
        last_window_focus = ADDRESS;
        address_fresh = true;
        gui_line_start();
        gui_line_add("Address, edit text, ");
        gui_line_add(address[0] ? address : "blank");
        gui_line_add(".");
        gui_line_say();
        request_redraw();
        return true;
    }
    if (alt_held && e->key == AREOS_KEY_LEFT) { go_back(); return true; }
    if (*focus == ADDRESS) {
        if (e->key == AREOS_KEY_ENTER) { go_to_address(); return true; }
        return type_into(address, sizeof(address), e, &address_fresh);
    }
    if (*focus == PAGE) return page_key(e);
    return false;
}

static void choose(int what) {
    switch (what) {
    case BACK:   go_back(); break;
    case FORWARD: go_forward(); break;
    case RELOAD:
        if (loading()) {
            stop_fetch();
            set_status("Stopped.");
            gui_announce("Stopped loading.");
        } else {
            redirects = 0;
            navigate(current_url, false);
        }
        break;
    case HOME:   navigate("about:home", true); break;
    case GO:     go_to_address(); break;
    case ADDRESS: break;
    case PAGE:
        // A click on the page: follow whatever link (or box) is there.
        if (click_x >= 0 && rich && wp && !style_wait_until) {
            int px = click_x, py = click_y - TOOLBAR_H + scroll_y, hit_focus = -1;
            web_page_hit(wp, px, py, &hit_focus);
            // the line under the click (or the nearest one)
            int line = -1, best = 1 << 30;
            for (int i = 0; i < page.line_count; i++) {
                struct page_line *l = &page.lines[i];
                int d = py < l->y ? l->y - py : (py >= l->y + l->h ? py - (l->y + l->h) + 1 : 0);
                if (d < best) { best = d; line = i; if (d == 0) break; }
            }
            if (line >= 0 && best < 60) read_line = line;
            if (hit_focus >= 0 && hit_focus < page.focusable_count) {
                page_focus = hit_focus;
                if (page.focusables[hit_focus].kind == FOCUS_FIELD) say_focusable(hit_focus);
                else activate(hit_focus);
            } else if (line >= 0 && best < 60) {
                say_line(line);
            }
            request_redraw();
            return;
        }
        if (click_x >= 0) {
            int x = click_x - PAGE_X, y = click_y - TOOLBAR_H + scroll_y;
            for (int b = 0; b < page.box_count; b++) {
                struct page_box *box = &page.boxes[b];
                if (x >= box->x - 2 && x < box->x + box->w + 2 && y >= box->y - 2 && y < box->y + box->h + 6) {
                    read_line = box->line;
                    if (box->focusable >= 0) {
                        struct page_focusable *f = &page.focusables[box->focusable];
                        page_focus = box->focusable;
                        if (f->kind == FOCUS_FIELD) say_focusable(box->focusable);
                        else activate(box->focusable);
                    } else {
                        say_line(box->line);
                    }
                    request_redraw();
                    return;
                }
            }
        }
        break;
    }
    request_redraw();
}

static void opened(void) {
    if (!page_ready_memory) {
        page_ready_memory = page_document_init(&page);
        raw = areos_more_memory(RAW_MAX);
        if (!page_ready_memory || raw == NULL) {
            page_ready_memory = false;
            gui_announce("Not enough memory for the browser.");
            return;
        }
        show_html(home_html, "about:home", false);
    }
    gui_line_start();
    gui_line_add(page_name);
    gui_line_add(". ");
    gui_line_add(summary);
    gui_line_add(". Alt+D goes to the address bar.");
    gui_line_say();
}

// What page scripts can ask about the window (see jsplatform.h).
int jsp_width(void) { return rich ? content_w : PAGE_W; }
int jsp_height(void) { return PAGE_H; }
int jsp_theme(void) { return light_mode ? 1 : 0; }

const struct window_app app_browser = {
    .title = "Browser", .icon = ICON_BROWSER, .width = START_W, .height = START_H, .resizable = true,
    .build = build, .draw = draw, .key = key, .choose = choose, .opened = opened, .tick = tick,
};
