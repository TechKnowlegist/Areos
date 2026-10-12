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
#include "browser_doc.h"

// Get the memory a document needs (once).
bool page_document_init(struct page_document *d);

// Turn HTML into a laid-out document `width` pixels wide.
// `plain` = it's plain text, not HTML: show it as it is.
void page_parse(struct page_document *d, const char *html, int length, int width, bool plain);

// The text of a box, a focusable's label, etc.
const char *page_string(const struct page_document *d, int at);

// ---- Web addresses ("URLs") ----

#define URL_MAX 1024

// Split "http(s)://host:port/path" into its parts. False if it isn't http or https.
bool url_split(const char *url, char *host, int host_max, int *port, char *path, int path_max, bool *secure);
// Work out where a link goes, from the page it's on.
void url_resolve(const char *base, const char *link, char *out, int max);
// Add text to a URL, %-escaping anything that isn't safe there.
void url_append_escaped(char *out, const char *text, int max);
bool text_starts_with(const char *text, const char *prefix);

// Called by the page's scripts before they use the network: a style sheet or picture on its way is put back in the queue.
void browser_pause_resources(void);

#endif
