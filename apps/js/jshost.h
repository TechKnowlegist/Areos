// jshost.h - the browser's view of the JavaScript engine (whole numbers and
// pointers only, because the engine is built with floating point and the
// browser is not).
#ifndef JSHOST_H
#define JSHOST_H

#include <stdbool.h>

// What jsh_pump() reports (bits).
#define JSH_DIRTY    0x01   // the page changed: render it again
#define JSH_NAV      0x02   // the page wants to go to another address (jsh_nav_*)
#define JSH_URL      0x04   // the address changed without leaving the page (pushState)
#define JSH_TITLE    0x08   // the title changed
#define JSH_LOADING  0x10   // scripts are still being run
#define JSH_SPEAK    0x20   // jsh_announcement() has something to say
#define JSH_FATAL    0x40   // the engine gave up: the page can't go on
#define JSH_TIMERS   0x80   // there are timers waiting

int js_host_eval_text(const char *source, char *out, int out_max);

// Start running a page. `html` is the page's source; `url` where it came from.
bool jsh_page_start(const char *url, const char *html, int length);
void jsh_page_stop(void);
bool jsh_page_active(void);
// Do some work (scripts, timers). Call often. Returns JSH_* bits.
int jsh_pump(void);
// The page as simple HTML for the browser to lay out. Valid until the next call.
const char *jsh_render(int *length);
// The whole page as HTML (every element and attribute), for the styled view.
const char *jsh_render_full(int *length);
// The user activated something / typed / pressed a key (ids come from data-aid).
int jsh_click(int aid);
void jsh_input(int aid, const char *text);
int jsh_key(int aid, const char *key);
// Where the page wants to go (after JSH_NAV).
const char *jsh_nav_url(void);
bool jsh_nav_replace(void);
const char *jsh_nav_method(void);
const char *jsh_nav_body(void);
const char *jsh_nav_content_type(void);
// After JSH_URL: the new address and how it came about (0 pushed, 1 replaced, 2 history traversal).
const char *jsh_url(int *mode);
const char *jsh_title(void);
// Things the page asked to be said aloud (alert...). NULL when none are left.
const char *jsh_announcement(void);
// Everything the page wrote to the console, and any errors.
const char *jsh_console(void);
// Move through the page's own history (pushState entries). True if it did.
bool jsh_history_go(int delta);
bool jsh_history_can_go(int delta);
// Wanted a reload of the page?
bool jsh_wants_reload(void);
// Information for screen: how many ms until the next timer (-1 none).
int jsh_next_timer_ms(void);

#endif
