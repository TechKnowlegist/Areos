// web.h - the web engine: the page tree, styles, boxes and pictures.
//
// How a page gets from HTML text to pixels:
//
//   web_dom.c     reads HTML into a tree of nodes (elements and text)
//   web_css.c     reads style rules, works out the style of every element
//   web_layout.c  turns elements into boxes and decides where each one goes
//   web_paint.c   draws the boxes (and their pictures) onto a canvas
//   web_page.c    ties it together, and tells the browser which files to fetch
//
// Nothing in here knows about AREOS (see web_port.h), so it can be tested on
// an ordinary computer.

#ifndef WEB_H
#define WEB_H

#include "web_port.h"
#include "web_font.h"

// ================= The page tree =================

enum node_type { N_ELEMENT = 1, N_TEXT = 2 };

enum tag {
    T_OTHER = 0,
    T_A, T_ABBR, T_ADDRESS, T_AREA, T_ARTICLE, T_ASIDE, T_AUDIO, T_B, T_BASE, T_BLOCKQUOTE, T_BODY, T_BR,
    T_BUTTON, T_CANVAS, T_CAPTION, T_CENTER, T_CITE, T_CODE, T_COL, T_COLGROUP, T_DATALIST, T_DD, T_DEL,
    T_DETAILS, T_DFN, T_DIALOG, T_DIV, T_DL, T_DT, T_EM, T_EMBED, T_FIELDSET, T_FIGCAPTION, T_FIGURE,
    T_FOOTER, T_FORM, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6, T_HEAD, T_HEADER, T_HGROUP, T_HR, T_HTML, T_I,
    T_IFRAME, T_IMG, T_INPUT, T_INS, T_KBD, T_LABEL, T_LEGEND, T_LI, T_LINK, T_MAIN, T_MAP, T_MARK, T_MENU,
    T_META, T_METER, T_NAV, T_NOSCRIPT, T_OBJECT, T_OL, T_OPTGROUP, T_OPTION, T_OUTPUT, T_P, T_PARAM,
    T_PICTURE, T_PRE, T_PROGRESS, T_Q, T_S, T_SAMP, T_SCRIPT, T_SECTION, T_SELECT, T_SLOT, T_SMALL, T_SOURCE,
    T_SPAN, T_STRIKE, T_STRONG, T_STYLE, T_SUB, T_SUMMARY, T_SUP, T_SVG, T_TABLE, T_TBODY, T_TD, T_TEMPLATE,
    T_TEXTAREA, T_TFOOT, T_TH, T_THEAD, T_TIME, T_TITLE, T_TR, T_TRACK, T_TT, T_U, T_UL, T_VAR, T_VIDEO,
    T_WBR, T_BIG, T_FONT, T_NOBR, T_FRAME, T_FRAMESET, T_NOFRAMES,
    T_COUNT
};

struct attr {
    const char *name;       // lower case
    const char *value;
    struct attr *next;
};

struct style;
struct box;
struct wimage;

struct node {
    uint8_t type;           // enum node_type
    uint8_t tag;            // enum tag
    bool in_svg;            // inside an <svg>: names keep their case
    bool open;              // <details>/<dialog>: is it open?
    const char *name;       // element name, lower case
    const char *text;       // text nodes: the characters (UTF-8)
    int text_len;
    struct node *parent, *first, *last, *next, *prev;
    struct attr *attrs;
    const char *id;         // the id attribute, or NULL
    const char *cls;        // the class attribute, or NULL
    int aid;                // data-aid: how the page's scripts name this element (0 = none)
    int index;              // position among its parent's element children (1-based), set when styling
    int type_index;         // position among siblings with the same name (1-based)
    int sibling_count;      // how many element children the parent has
    int type_count;         // how many siblings have this name
    struct style *style;
    struct style *before, *after;   // styles of ::before and ::after, if they make something
    struct box *box;
    int focusable;          // index into the document's focusables, or -1
};

struct page;

// Read HTML into a tree. `scripting`: the page's scripts are running, so
// <noscript> is not shown.
struct node *web_parse_html(struct page *p, const char *html, int length, bool scripting);

const char *node_attr(const struct node *n, const char *name);
bool node_has_attr(const struct node *n, const char *name);
struct node *node_new_element(struct page *p, const char *name);
void node_set_attr(struct page *p, struct node *n, const char *name, const char *value);
void node_remove_attr(struct node *n, const char *name);
// The text inside a node (all its text nodes joined, spaces tidied). Returns length.
int node_text(const struct node *n, char *out, int max);
enum tag tag_from_name(const char *name, int length);

// ================= Lengths and colors =================

// A length is `px` pixels plus `pct` hundredths of a percent of something
// (what that something is depends on the property), or a keyword.
// Anything more complicated (min(), max(), clamp()) is a small tree in `expr`.
enum len_kind { L_AUTO = 0, L_VAL, L_MIN_CONTENT, L_MAX_CONTENT, L_FIT_CONTENT, L_NONE };

struct lexpr;
struct len {
    int px;
    int pct;                // 10000 = 100%
    uint8_t kind;
    struct lexpr *expr;     // if set, overrides px/pct
};

enum lexpr_op { LE_LEN, LE_ADD, LE_SUB, LE_MUL, LE_DIV, LE_MIN, LE_MAX, LE_CLAMP };
struct lexpr {
    uint8_t op;
    int num;                // MUL/DIV: the plain number, times 1000
    struct len leaf;        // LE_LEN
    struct lexpr *a, *b, *c;
};

int len_resolve(const struct len *l, int base);      // px, given what 100% means
static inline bool len_is_auto(const struct len *l) { return l->kind == L_AUTO; }
static inline bool len_is_val(const struct len *l) { return l->kind == L_VAL; }

// Colors are 0xAARRGGBB.
#define COLOR_TRANSPARENT 0x00000000u
#define COLOR_CURRENT 0x00A1B2C3u   // "currentColor" (it's see-through, so never drawn as itself)
#define RGBA(r, g, b, a) ((uint32_t)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))

// ================= Styles =================

enum display {
    D_NONE, D_BLOCK, D_INLINE, D_INLINE_BLOCK, D_FLEX, D_INLINE_FLEX, D_GRID, D_INLINE_GRID, D_LIST_ITEM,
    D_TABLE, D_INLINE_TABLE, D_TABLE_ROW_GROUP, D_TABLE_HEADER_GROUP, D_TABLE_FOOTER_GROUP, D_TABLE_ROW,
    D_TABLE_CELL, D_TABLE_CAPTION, D_TABLE_COLUMN, D_TABLE_COLUMN_GROUP, D_CONTENTS, D_RUBY
};
enum position { POS_STATIC, POS_RELATIVE, POS_ABSOLUTE, POS_FIXED, POS_STICKY };
enum floating { FL_NONE, FL_LEFT, FL_RIGHT };
enum clearing { CL_NONE, CL_LEFT, CL_RIGHT, CL_BOTH };
enum overflow { OV_VISIBLE, OV_HIDDEN, OV_SCROLL, OV_AUTO, OV_CLIP };
enum border_style { BS_NONE, BS_SOLID, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_GROOVE, BS_RIDGE, BS_INSET, BS_OUTSET, BS_HIDDEN };
enum text_align { TA_START, TA_LEFT, TA_RIGHT, TA_CENTER, TA_JUSTIFY, TA_END };
enum white_space { WS_NORMAL, WS_NOWRAP, WS_PRE, WS_PRE_WRAP, WS_PRE_LINE, WS_BREAK_SPACES };
enum text_transform { TT_NONE, TT_UPPER, TT_LOWER, TT_CAPITALIZE };
enum vertical_align { VA_BASELINE, VA_TOP, VA_MIDDLE, VA_BOTTOM, VA_TEXT_TOP, VA_TEXT_BOTTOM, VA_SUB, VA_SUPER, VA_LEN };
enum list_style { LS_DISC, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_DECIMAL_ZERO, LS_LOWER_ALPHA, LS_UPPER_ALPHA, LS_LOWER_ROMAN,
                  LS_UPPER_ROMAN, LS_LOWER_GREEK, LS_NONE };
enum flex_dir { FD_ROW, FD_ROW_REVERSE, FD_COLUMN, FD_COLUMN_REVERSE };
enum flex_align { FA_AUTO, FA_START, FA_END, FA_CENTER, FA_STRETCH, FA_BASELINE, FA_SPACE_BETWEEN, FA_SPACE_AROUND,
                  FA_SPACE_EVENLY, FA_NORMAL };
enum object_fit { OF_FILL, OF_CONTAIN, OF_COVER, OF_NONE, OF_SCALE_DOWN };
enum word_break { WB_NORMAL, WB_BREAK_ALL, WB_KEEP_ALL, WB_BREAK_WORD };

#define DECO_UNDERLINE 1
#define DECO_OVERLINE 2
#define DECO_LINETHROUGH 4

#define MAX_BG 4
#define MAX_STOPS 10
#define MAX_SHADOWS 4
#define MAX_TRACKS 24

struct gradient_stop { uint32_t color; struct len pos; bool has_pos; };
struct gradient {
    bool radial;
    int angle;                  // linear: degrees * 10 (CSS angle: 0 = up, 90 = right)
    bool to_corner;             // "to top right": angle depends on the box
    int corner_x, corner_y;     // -1, 0, 1: the corner "to ..." points at
    struct len cx, cy;          // radial: center
    int shape;                  // radial: 0 ellipse, 1 circle
    int extent;                 // radial: 0 farthest-corner, 1 closest-side, 2 closest-corner, 3 farthest-side
    struct len rx, ry;          // radial: explicit size
    bool repeating;
    int stop_count;
    struct gradient_stop stops[MAX_STOPS];
};

enum bg_size_mode { BGS_AUTO, BGS_COVER, BGS_CONTAIN, BGS_LEN };
struct bg_layer {
    const char *url;            // an image address, or NULL
    struct gradient *gradient;  // or a gradient
    uint8_t size_mode;
    struct len size_w, size_h;
    struct len pos_x, pos_y;    // 0%..100%, or px
    bool repeat_x, repeat_y;
    uint8_t clip;               // 0 border-box, 1 padding-box, 2 content-box, 3 text
    uint8_t origin;             // 0 padding-box, 1 border-box, 2 content-box
};

struct shadow { int x, y, blur, spread; uint32_t color; bool inset; };

enum track_kind { TK_PX, TK_PCT, TK_FR, TK_AUTO, TK_MIN_CONTENT, TK_MAX_CONTENT, TK_MINMAX };
struct track {
    uint8_t kind;
    int v;                      // px, hundredths of a percent, or fr * 1000
    uint8_t min_kind, max_kind; // TK_MINMAX: the two ends
    int min_v, max_v;
    int repeat;                 // 0: normal; 1: auto-fill; 2: auto-fit (this track repeats)
    struct len fit;             // fit-content(x)
    bool has_fit;
};

struct custom_prop {
    const char *name;           // "--name"
    const char *value;
    int length;
    struct custom_prop *next;
};

struct style {
    // box
    uint8_t display, position, floating, clear, visibility, overflow_x, overflow_y, box_sizing;
    uint8_t text_align, white_space, text_transform, vertical_align, list_style, list_pos_inside;
    uint8_t text_decoration, word_break, text_overflow_ellipsis, font_italic, font_family, object_fit;
    uint8_t flex_dir, flex_wrap, justify_content, align_items, align_self, align_content;
    uint8_t pointer_events_none, border_collapse, table_layout_fixed, grid_auto_flow_column, grid_dense;
    uint8_t user_select_none, underline_style;
    uint8_t justify_items, justify_self;
    uint8_t svg_fill_set, svg_stroke_set, svg_linecap, svg_linejoin, fill_opacity, stroke_opacity;
    uint8_t content_kind;       // 0 normal/none, 1 string
    uint8_t opacity;            // 0..255
    bool has_z;
    bool has_transform;
    bool is_pseudo;
    int16_t font_weight;        // 100..900
    int z_index;
    struct len width, height, min_width, min_height, max_width, max_height;
    struct len margin[4], padding[4];      // top, right, bottom, left
    struct len inset[4];                   // top, right, bottom, left
    int border_width[4];
    uint8_t border_style[4];
    uint32_t border_color[4];
    struct len radius[4];                  // top-left, top-right, bottom-right, bottom-left
    uint32_t color, bg_color, deco_color;
    int bg_count;
    struct bg_layer bg[MAX_BG];
    int shadow_count;
    struct shadow shadows[MAX_SHADOWS];
    int font_size;              // pixels
    int line_height_pct;        // > 0: a number (x 100) times the font size
    int line_height_px;         // when line_height_pct == 0 and this > 0
    int letter_spacing;         // 1/64 px
    int word_spacing;           // 1/64 px
    struct len text_indent;
    struct len vertical_align_len;
    int tab_size;
    // flex
    int flex_grow, flex_shrink; // x 1000
    struct len flex_basis;
    int order;
    struct len row_gap, column_gap;
    // grid
    int col_track_count, row_track_count;
    struct track col_tracks[MAX_TRACKS], row_tracks[MAX_TRACKS];
    struct track auto_col, auto_row;
    int col_start, col_end, row_start, row_end;    // grid-column: 1 / 3 (0 = auto; negative counts from the end)
    int col_span, row_span;
    const char *grid_areas;     // grid-template-areas, raw
    const char *grid_area;      // grid-area: name
    // table
    struct len spacing_h, spacing_v;
    // replaced content
    struct len object_pos_x, object_pos_y;
    int aspect_w, aspect_h;
    // transform (translate only)
    struct len translate_x, translate_y;
    // generated content
    const char *content;
    int content_len;
    // svg paint (inherited)
    uint32_t svg_fill, svg_stroke;
    int svg_stroke_width;       // 1/1000 px; 0 = not set
    const char *svg_fill_ref, *svg_stroke_ref;
    // custom properties (--name: value), inherited
    struct custom_prop *vars;
    // heading level etc. come from the tag, not the style.
};

// ================= Style sheets =================

struct sheet;
struct page;
struct sheet *css_sheet_new(struct page *p);                      // empty, not yet in the page's list
void css_sheet_fill(struct page *p, struct sheet *sh, const char *text, int length, const char *base_url);
struct sheet *css_parse_sheet(struct page *p, const char *text, int length, const char *base_url);
void css_reset(struct page *p);                                   // forget layer names (new page)
void css_style_page(struct page *p);
// Ask the browser for a file (stylesheets, pictures). Returns the resource (existing one if the address was already asked for).
struct resource *web_queue_resource(struct page *p, const char *url, int kind, struct sheet *sheet);

// ================= Boxes (the layout) =================

enum box_type { BT_BLOCK, BT_INLINE, BT_TEXT, BT_BR, BT_REPLACED };
enum replaced_kind { RK_NONE, RK_IMAGE, RK_INPUT_TEXT, RK_INPUT_CHECK, RK_INPUT_RADIO, RK_SELECT, RK_TEXTAREA, RK_BLANK, RK_SVG,
                     RK_RANGE, RK_PROGRESS, RK_COLOR, RK_FILE };

enum frag_type { FRAG_TEXT, FRAG_BOX, FRAG_DECOR };
struct frag {
    uint8_t type;
    bool first_piece, last_piece;
    struct box *box;            // text: the text box; box: the atomic box; decor: the inline box
    int x, y, w, h;             // y is the top of the glyph cell (relative to the block's content box origin)
    int text_off, text_len;     // text: which part of the box's text
    int baseline;               // distance from y down to the baseline
    int line;
    int focusable;
};

struct line {
    int y, h;
    int first_frag, frag_count;
    int baseline;               // from y
    int left, right;            // the free space it was laid out in
};

struct edges { int top, right, bottom, left; };

struct box {
    struct node *node;
    struct style *style;
    uint8_t type;               // enum box_type
    uint8_t replaced;           // enum replaced_kind
    bool inline_level;          // sits in a line of text
    bool ifc;                   // its children are inline-level: it makes lines
    bool anon;
    bool out_of_flow;           // floated or positioned absolutely/fixed
    bool is_float;
    bool bfc;                   // starts its own block formatting context
    bool is_marker_host;        // list item
    bool replaced_inline_block; // inline-block-like atomic
    bool hidden;                // visibility: hidden
    bool contains_float;
    bool intr_done;
    bool center_v;              // button: centre the content vertically
    bool block_in_inline;
    bool from_pseudo;
    struct box *parent, *first, *last, *next;

    // text
    const char *text;
    int text_len;
    char *transformed;          // the text as laid out: spaces collapsed, text-transform applied
    int tlen;

    // layout results (relative to the parent's border box origin)
    int x, y, w, h;
    struct edges margin, pad, border;
    int rel_x, rel_y;           // position: relative / sticky offset
    int content_w, content_h;
    int escaped_bottom;         // bottom margin that collapsed out of it

    // inline formatting
    struct line *lines;
    int line_count;
    struct frag *frags;
    int frag_count;

    // out-of-flow children waiting for their containing block
    struct box *abs_first, *abs_last, *abs_next;

    // intrinsic widths
    int min_content, max_content;

    // replaced content
    struct wimage *image;
    int intrinsic_w, intrinsic_h;
    const char *alt;

    // lists
    int list_index;
    // tables
    int colspan, rowspan, cell_col, cell_row;
    int table_cols;
    int *col_widths;
    int *row_heights;
    int grid_x, grid_y;         // grid items: their cell
    int order;
    int stack_order;            // paint order among siblings
    int focusable;
    int z;                      // computed stacking value (z-index), if any
    // where it ended up on the page (absolute), filled in after layout
    int ax, ay;
    bool collapse_first;        // its first child's top margin joined its own
    int sx, sy;                 // out-of-flow: where it would have been (static position), relative to shost's content box
    struct box *shost;
    int mt_eff;                 // top margin including collapsed children
    bool float_shrink_hint;
    bool has_collapse_border;   // table cell: its borders were decided by the collapsing rules
    int collapse_border[4];
    int line_h_hint;
};

// ================= Pictures =================

enum image_state { IMG_WAITING, IMG_LOADING, IMG_READY, IMG_FAILED };

struct wimage {
    const char *url;
    uint8_t state;
    int w, h;                   // pixels in `px`
    int natural_w, natural_h;   // the size the picture is meant to be shown at
    uint32_t *px;               // 0xAARRGGBB (not premultiplied)
    bool has_alpha;
    struct wimage *next;
    // a scaled copy, made when the picture is drawn smaller than it is
    uint32_t *scaled;
    int sw, sh;
    struct svg_doc *svg;        // vector pictures: drawn at any size
    bool counted;
    bool shared;                // the pixels belong to the cache (see web_cache_*)
};

// Decoders: return a new image or NULL. `max_dim` lets big pictures be made smaller while decoding.
struct wimage *img_decode(const void *data, int length, int max_dim);
bool img_decode_png(const uint8_t *data, int length, int max_dim, struct wimage *out);
bool img_decode_jpeg(const uint8_t *data, int length, int max_dim, struct wimage *out);
bool img_decode_gif(const uint8_t *data, int length, int max_dim, struct wimage *out);
bool img_decode_webp(const uint8_t *data, int length, int max_dim, struct wimage *out);
bool img_decode_bmp(const uint8_t *data, int length, int max_dim, struct wimage *out);
void img_free_pixels(struct wimage *img);

// ================= The page =================

enum res_kind { RES_STYLESHEET, RES_IMAGE, RES_IMPORT };
enum res_state { RS_WANTED, RS_REQUESTED, RS_DONE, RS_FAILED };

struct resource {
    const char *url;
    uint8_t kind;               // enum res_kind
    uint8_t state;              // enum res_state
    struct resource *next;
    struct sheet *sheet;        // stylesheets: where it goes in the order
    struct wimage *image;
    int order;
};

struct page_document;

struct page {
    struct w_arena arena;       // the tree and the style sheets (kept for the life of the page)
    struct w_arena larena;      // styles and boxes (thrown away and rebuilt at every layout)
    const char *base_url;       // the page's address (for relative links)
    const char *doc_url;
    struct node *root;          // the <html> element
    struct node *head, *body;
    char title[160];
    bool scripting;
    bool dark;                  // the viewer prefers dark colors
    int viewport_w, viewport_h; // size of the window's page area
    // style sheets, in order
    struct sheet *sheets, *last_sheet;
    int sheet_count;
    struct resource *resources, *last_resource;
    int resource_count;
    struct wimage *images;
    // layout
    struct box *root_box;
    struct box *fixed_first, *fixed_last;
    uint32_t canvas_color;      // the page's background
    bool canvas_set;
    bool canvas_from_body;      // the body's background was moved to the canvas
    bool plain;                 // no author styles at all
    int content_w, content_h;   // how big the laid-out page is
    struct page_document *doc;
    // paint order helpers
    int next_order;
    bool needs_layout;
    int focus_count;
    struct node **focus_nodes;  // focusable elements in page order (malloc'd)
    int focus_cap;
    // details/summary state (kept across layouts by node pointer index)
    int layout_serial;
    int focus_cur;              // the focused element (index into focus_nodes), or -1
    int paint_seq;
    int generation;             // changes when the tree is rebuilt
    void *svgc;                 // pictures drawn from <svg> (see svg.c)
};

struct page *web_page_new(void);
void web_page_free(struct page *p);

// Set up a page from HTML. `doc_url` is its address. `bytes` is the HTML.
void web_page_load(struct page *p, const char *html, int length, const char *doc_url, bool scripting);

// The cache of fetched style sheets and pictures. Free the pages that use it before trimming or clearing.
void web_cache_clear(void);
void web_cache_trim(void);

// Pending files. Returns the next resource the browser should fetch (and marks
// it as requested), or NULL.
struct resource *web_next_resource(struct page *p);
// A file arrived (data == NULL: it couldn't be fetched).
void web_resource_arrived(struct page *p, struct resource *r, const void *data, int length, const char *content_type);
// Style sheets all in? (Pictures may still be coming.)
bool web_styles_ready(const struct page *p);
int web_pending_count(const struct page *p, bool images_too);

// Work out styles and place everything for a window `width` x `height` pixels.
void web_page_layout(struct page *p, int width, int height, bool dark);
// Draw the part of the page starting at `scroll_y` onto the canvas.
#ifdef WEB_HOST
struct gui_canvas { uint32_t *pixels; int width, height; };
#else
struct gui_canvas;
#endif
void web_page_paint(struct page *p, struct gui_canvas *c, int scroll_y);

// Which element is at this spot of the page (after a paint)? `focus` gets its focus number or -1.
struct node *web_page_hit(struct page *p, int x, int y, int *focus);
bool web_page_focus_rect(struct page *p, int focus_id, int *x, int *y, int *w, int *h);

// The words and controls of a page, for the keyboard and screen reader (web_doc.c).
struct page_document;
void web_doc_fill(struct page_document *d, struct page *p);
bool web_doc_toggle(struct page_document *d, struct page *p, int f);
void web_doc_cycle_select(struct page_document *d, struct page *p, int f);
void web_doc_set_value(struct page_document *d, struct page *p, int f);
void web_doc_select_value(struct page_document *d, int f, char *out, int max);

// Resolve a link against the page's address.
void web_url_resolve(const char *base, const char *link, char *out, int max);

// Make a data: address into bytes. Returns a malloc'd buffer, or NULL.
uint8_t *web_data_url(const char *url, int *length, char *mime, int mime_max);

// ================= Shared helpers =================

struct wimage *web_image_get(struct page *p, const char *url);   // find or create (queues a fetch)
void web_image_finish(struct wimage *img);
// A copy of the picture scaled to w x h (cached). NULL if it has no pixels yet.
const uint32_t *web_image_scaled(struct wimage *img, int w, int h);

// Layout and paint internals shared across files.
struct box *layout_build(struct page *p);
struct style *css_inherit_style(struct page *p, const struct style *parent, int display);   // an unnamed box's style
void layout_run(struct page *p);
void paint_page(struct page *p, struct gui_canvas *c, int scroll_y);
void style_compute_all(struct page *p);

// SVG
struct svg_doc;
struct svg_doc *svg_parse_text(const char *text, int length);
struct svg_doc *svg_from_node(struct page *p, struct node *n);
void svg_size(struct svg_doc *d, int *w, int *h);
void svg_free(struct svg_doc *d);
bool svg_node_size(struct node *n, int *w, int *h);   // the size an <svg> asks for (false: it doesn't say)
void svg_cache_free(struct page *p);
// Draw the vector picture into a w x h pixel buffer (0xAARRGGBB, transparent to start).
void svg_render(struct svg_doc *d, uint32_t *out, int w, int h, uint32_t current_color);

#endif
