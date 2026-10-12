// browser_doc.h - the "document" the browser reads and speaks: words grouped into lines,
// and the links, text boxes and buttons Tab moves between. It has no idea how
// the page looks; web_doc.c fills it in from the laid-out page.

#ifndef BROWSER_DOC_H
#define BROWSER_DOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

// What kind of control a button is.
#define SUB_NONE    0
#define SUB_CHECK   1
#define SUB_RADIO   2
#define SUB_SELECT  3
#define SUB_SUMMARY 4
#define SUB_FILE    5

struct page_focusable {
    enum focus_kind kind;
    int target;             // links: the address (in the text store)
    int name;               // fields and buttons: their form name (text store), or -1
    int label;              // what it says (text store)
    int form;               // which form it's in, or -1
    int line;               // first line it's on
    int aid;                // the page's script knows this control by this number (0: none)
    bool secret;            // a password box: never shown or spoken
    uint8_t sub;            // for buttons: SUB_*
    bool checked;           // check boxes and radio buttons
    bool submits;           // a button that sends its form
    void *node;             // the page element (web engine), or NULL
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


#endif
