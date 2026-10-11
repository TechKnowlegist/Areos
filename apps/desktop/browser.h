// browser.h - the parts of the web browser.
//
//   browser_html.c  web addresses, and turning a page's HTML into words
//                   laid out in lines (the "document")
//   app_browser.c   the Browser window: fetching pages over the network,
//                   drawing them, and moving around them by keyboard
//
// A document is made of "boxes" (a word or a few words, all in one style,
// at one place on the page), grouped into lines. Links, text boxes and
// buttons are "focusables": the things Tab moves between.

#ifndef BROWSER_H
#define BROWSER_H

#include "desktop.h"

// Styles of a box.
#define STYLE_BOLD     0x01
#define STYLE_LINK     0x02
#define STYLE_HEADING  0x04
#define STYLE_PRE      0x08
#define STYLE_FIELD    0x10   // a text box in a form
#define STYLE_BUTTON   0x20   // a button in a form
#define STYLE_ITALIC   0x40
#define STYLE_IMAGE    0x80   // the description of a picture

struct page_box {
    int x, y, w, h;
    int text, length;       // where its words are in the document's text
    uint16_t style;
    uint8_t scale;          // 1, or 2 for big headings
    int16_t focusable;      // which focusable it belongs to, or -1
    int line;
};

struct page_line {
    int y, h;
    int first_box, box_count;
    int heading;            // 1-6 if this line is a heading, else 0
};

enum focus_kind { FOCUS_LINK, FOCUS_FIELD, FOCUS_BUTTON };

#define FIELD_MAX 128

struct page_focusable {
    enum focus_kind kind;
    int target;             // links: the address (in the text store)
    int name;               // fields and buttons: their form name (text store), or -1
    int label;              // what it says (text store)
    int form;               // which form it's in, or -1
    int line;               // first line it's on
    char value[FIELD_MAX];  // fields: what's typed in it
};

struct page_form {
    int action;             // where it sends to (text store), or -1 for this page
    int hidden_first, hidden_count;   // hidden name=value pairs (in `hiddens`)
};

struct page_hidden { int name, value; int form; };

#define MAX_BOXES      40000
#define MAX_LINES      12000
#define MAX_FOCUSABLES 1500
#define MAX_FORMS      40
#define MAX_HIDDENS    200
#define TEXT_STORE     (1024 * 1024)

struct page_document {
    char *text;                     // all the words, and strings like link addresses
    int text_used;
    struct page_box *boxes;
    int box_count;
    struct page_line *lines;
    int line_count;
    struct page_focusable *focusables;
    int focusable_count;
    struct page_form forms[MAX_FORMS];
    int form_count;
    struct page_hidden hiddens[MAX_HIDDENS];
    int hidden_count;
    char title[128];
    int height;                     // how tall the laid-out page is
    int link_count, heading_count, field_count;
};

// Get the memory a document needs (once).
bool page_document_init(struct page_document *d);

// Turn HTML into a laid-out document `width` pixels wide.
// `plain` = it's plain text, not HTML: show it as it is.
void page_parse(struct page_document *d, const char *html, int length, int width, bool plain);

// The text of a box, a focusable's label, etc.
const char *page_string(const struct page_document *d, int at);

// ---- Web addresses ("URLs") ----

#define URL_MAX 512

// Split "http://host:port/path" into its parts. False if it isn't http.
bool url_split(const char *url, char *host, int host_max, int *port, char *path, int path_max);
// Work out where a link goes, from the page it's on.
void url_resolve(const char *base, const char *link, char *out, int max);
// Add text to a URL, %-escaping anything that isn't safe there.
void url_append_escaped(char *out, const char *text, int max);
bool text_starts_with(const char *text, const char *prefix);

#endif
