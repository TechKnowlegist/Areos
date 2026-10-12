// web_lay.h - inside the layout engine (shared by web_box.c, web_layout.c, web_flex.c ...).

#ifndef WEB_LAY_H
#define WEB_LAY_H

#include "web.h"

// ---- Building the box tree (web_box.c) ----
bool box_is_block_level(const struct box *b);
struct box *box_new(struct page *p, struct node *n, struct style *s, int type);
void box_append(struct box *parent, struct box *child);
bool node_is_focusable(const struct node *n);
int list_marker_text(const struct style *s, int index, char *out, int max);   // "1. ", "\xE2\x80\xA2 " ...; returns length
const char *node_input_type(const struct node *n);   // lower-case type of an <input>

// ---- Layout (web_layout.c) ----

struct fl_item { int x, y, w, h; bool left; };      // a float, in its formatting context's coordinates

struct fctx {                                       // a block formatting context: the floats in it
    struct fl_item *items;
    int count, cap;
    int clear_left, clear_right;                   // lowest bottom edge of left / right floats
};

struct lay {
    struct page *p;
    int vw, vh;                                    // the viewport
    struct box *abs_stack[64];                     // containers waiting for out-of-flow children
    int abs_depth;
};

// What a box is given when asked to lay itself out.
struct lin {
    int cb_w;               // width of the containing block's content box (percentages are of this)
    int cb_h;               // height of the containing block's content box, or -1 if unknown
    int avail_w;            // for shrink-to-fit: the room there is
    int forced_w;           // border-box width to use, or -1 (flex items, grid items, cells)
    int forced_h;           // border-box height to use, or -1
    struct fctx *f;         // float context the box sits in (NULL: start a new one)
    int fx, fy;             // where the box's border-box top-left is, in that context
    bool shrink;            // width auto means shrink-to-fit
};

void lay_box(struct lay *L, struct box *b, const struct lin *in);   // size and place the contents; sets b->w, b->h and margins/pads
void lay_intrinsic(struct lay *L, struct box *b);                    // fills min_content / max_content (border-box, without margins)
void lay_abs_children(struct lay *L, struct box *b);
void lay_edges(struct box *b, int cb_w);                           // margins (auto = 0), padding and borders in px
bool box_is_container(const struct box *b);                        // out-of-flow children are placed relative to it
int lay_baseline(const struct box *b);                             // distance from top of border box to the baseline, or -1
void lay_replaced_size(struct lay *L, struct box *b, int cb_w, int cb_h, int *w, int *h);   // content size
int font_for(const struct style *s);                               // font number for a style                // out-of-flow boxes waiting on b
int len_to_px(const struct len *l, int base, int def);              // resolves a length, with `def` for auto/none
int box_font_height(const struct style *s, int *ascent, int *descent);   // line height from a style
int lay_block_in(struct lay *L, struct box *b, struct fctx *f, int cb_w, int cb_h, int fx, int fy);   // convenience

// ---- Flex / grid / table (web_flex.c, web_grid.c, web_table.c) ----
void lay_flex(struct lay *L, struct box *b, const struct lin *in, int content_w, int *content_h);
void lay_grid(struct lay *L, struct box *b, const struct lin *in, int content_w, int *content_h);
void lay_table(struct lay *L, struct box *b, const struct lin *in, int avail_w);
void lay_shift_content(struct box *b, int dy);                    // move a box's contents down (cell alignment)
void intr_flex(struct lay *L, struct box *b, int *mn, int *mx);
void intr_grid(struct lay *L, struct box *b, int *mn, int *mx);
void intr_table(struct lay *L, struct box *b, int *mn, int *mx);

// ---- Inline layout (web_inline.c) ----
void lay_inline(struct lay *L, struct box *b, const struct lin *in, struct fctx *f, int content_w, int cx, int cy, int *content_h);
void intr_inline(struct lay *L, struct box *b, int *mn, int *mx);
void text_prepare(struct page *p, struct box *t);

// ---- Floats ----
void fl_add(struct fctx *f, int x, int y, int w, int h, bool left);
// Free space at the vertical band [y, y+h): the left and right edges (in context coordinates), within [lo, hi].
void fl_edges(const struct fctx *f, int y, int h, int lo, int hi, int *left, int *right);
// The lowest y >= y where something changes (a float ends), or INT32_MAX.
int fl_next_edge(const struct fctx *f, int y);
int fl_clear_y(const struct fctx *f, int y, int clear);   // y after clearing floats

// The page-level layout driver.
void layout_page(struct page *p, int width, int height);

#endif
