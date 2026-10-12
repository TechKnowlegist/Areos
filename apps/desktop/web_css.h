// web_css.h - inside the style engine (shared by web_css*.c).

#ifndef WEB_CSS_H
#define WEB_CSS_H

#include "web.h"

// ---- Reading values ----

enum vtok_kind { V_END, V_IDENT, V_NUM, V_DIM, V_PCT, V_STR, V_HASH, V_FUNC, V_COMMA, V_SLASH, V_DELIM };

struct vp {
    const char *s;
    int len;
    int pos;
};

struct vtoken {
    int kind;
    const char *s;          // ident / string / hash / function name (without the "(")
    int n;
    int num;                // numbers x 1000
    const char *unit;       // dimensions
    int unit_len;
    int delim;              // V_DELIM: the character
};

void vp_init(struct vp *v, const char *s, int len);
bool vp_next(struct vp *v, struct vtoken *t);          // false at the end
bool vp_peek(struct vp *v, struct vtoken *t);
// After a V_FUNC token: the text between the parentheses, and move past ")".
void vp_func_args(struct vp *v, struct vp *inside);
bool tok_is(const struct vtoken *t, const char *ident);   // an identifier equal to this (lower case)
int tok_keyword(const struct vtoken *t, const char *const *names);   // index in a NULL-ended list, or -1

// What a length or color needs to know to turn into pixels.
struct css_ctx {
    struct page *page;
    int font_px;
    int root_px;
    int vw, vh;
    uint32_t cur_color;
    bool dark;
};

#define LF_NONNEG 1
#define LF_NO_PCT 2
#define LF_NUMBER_OK 4      // a bare number counts (as pixels)

bool css_parse_len(struct vp *v, const struct css_ctx *cx, struct len *out, int flags);
bool css_parse_len_tok(struct vp *v, const struct vtoken *t, const struct css_ctx *cx, struct len *out, int flags);
bool css_parse_color(struct vp *v, const struct css_ctx *cx, uint32_t *out);
bool css_parse_color_tok(struct vp *v, const struct vtoken *t, const struct css_ctx *cx, uint32_t *out);
bool css_parse_number(struct vp *v, const struct css_ctx *cx, int *milli);   // calc() allowed; result x 1000
bool css_parse_angle(struct vp *v, int *tenths_of_degrees);
bool css_parse_time_ignored(struct vp *v);

// Float color math (web_color_f.c).
uint32_t web_oklab_to_rgb(int l_milli, int a_milli, int b_milli);
uint32_t web_oklch_to_rgb(int l_milli, int c_milli, int h_milli);
uint32_t web_lab_to_rgb(int l_milli, int a_milli, int b_milli);
uint32_t web_lch_to_rgb(int l_milli, int c_milli, int h_milli);

// ---- Properties ----

#define PROP_LIST(X) \
    X(DISPLAY, "display") X(POSITION, "position") X(FLOAT, "float") X(CLEAR, "clear") X(VISIBILITY, "visibility") \
    X(OVERFLOW_X, "overflow-x") X(OVERFLOW_Y, "overflow-y") X(BOX_SIZING, "box-sizing") X(TEXT_ALIGN, "text-align") \
    X(WHITE_SPACE, "white-space") X(TEXT_TRANSFORM, "text-transform") X(VERTICAL_ALIGN, "vertical-align") \
    X(LIST_STYLE_TYPE, "list-style-type") X(LIST_STYLE_POSITION, "list-style-position") \
    X(TEXT_DECORATION_LINE, "text-decoration-line") X(TEXT_DECORATION_COLOR, "text-decoration-color") \
    X(WORD_BREAK, "word-break") X(OVERFLOW_WRAP, "overflow-wrap") X(TEXT_OVERFLOW, "text-overflow") \
    X(FONT_STYLE, "font-style") X(FONT_FAMILY, "font-family") X(FONT_WEIGHT, "font-weight") X(FONT_SIZE, "font-size") \
    X(LINE_HEIGHT, "line-height") X(LETTER_SPACING, "letter-spacing") X(WORD_SPACING, "word-spacing") \
    X(TEXT_INDENT, "text-indent") X(TAB_SIZE, "tab-size") X(OBJECT_FIT, "object-fit") X(OBJECT_POSITION, "object-position") \
    X(FLEX_DIRECTION, "flex-direction") X(FLEX_WRAP, "flex-wrap") X(JUSTIFY_CONTENT, "justify-content") \
    X(ALIGN_ITEMS, "align-items") X(ALIGN_SELF, "align-self") X(ALIGN_CONTENT, "align-content") \
    X(FLEX_GROW, "flex-grow") X(FLEX_SHRINK, "flex-shrink") X(FLEX_BASIS, "flex-basis") X(ORDER, "order") \
    X(ROW_GAP, "row-gap") X(COLUMN_GAP, "column-gap") \
    X(GRID_TEMPLATE_COLUMNS, "grid-template-columns") X(GRID_TEMPLATE_ROWS, "grid-template-rows") \
    X(GRID_TEMPLATE_AREAS, "grid-template-areas") X(GRID_AUTO_COLUMNS, "grid-auto-columns") \
    X(GRID_AUTO_ROWS, "grid-auto-rows") X(GRID_AUTO_FLOW, "grid-auto-flow") \
    X(GRID_COLUMN_START, "grid-column-start") X(GRID_COLUMN_END, "grid-column-end") \
    X(GRID_ROW_START, "grid-row-start") X(GRID_ROW_END, "grid-row-end") X(GRID_AREA, "grid-area") \
    X(OPACITY, "opacity") X(Z_INDEX, "z-index") \
    X(WIDTH, "width") X(HEIGHT, "height") X(MIN_WIDTH, "min-width") X(MIN_HEIGHT, "min-height") \
    X(MAX_WIDTH, "max-width") X(MAX_HEIGHT, "max-height") \
    X(MARGIN_TOP, "margin-top") X(MARGIN_RIGHT, "margin-right") X(MARGIN_BOTTOM, "margin-bottom") X(MARGIN_LEFT, "margin-left") \
    X(PADDING_TOP, "padding-top") X(PADDING_RIGHT, "padding-right") X(PADDING_BOTTOM, "padding-bottom") X(PADDING_LEFT, "padding-left") \
    X(TOP, "top") X(RIGHT, "right") X(BOTTOM, "bottom") X(LEFT, "left") \
    X(BORDER_TOP_WIDTH, "border-top-width") X(BORDER_RIGHT_WIDTH, "border-right-width") \
    X(BORDER_BOTTOM_WIDTH, "border-bottom-width") X(BORDER_LEFT_WIDTH, "border-left-width") \
    X(BORDER_TOP_STYLE, "border-top-style") X(BORDER_RIGHT_STYLE, "border-right-style") \
    X(BORDER_BOTTOM_STYLE, "border-bottom-style") X(BORDER_LEFT_STYLE, "border-left-style") \
    X(BORDER_TOP_COLOR, "border-top-color") X(BORDER_RIGHT_COLOR, "border-right-color") \
    X(BORDER_BOTTOM_COLOR, "border-bottom-color") X(BORDER_LEFT_COLOR, "border-left-color") \
    X(BORDER_TL_RADIUS, "border-top-left-radius") X(BORDER_TR_RADIUS, "border-top-right-radius") \
    X(BORDER_BR_RADIUS, "border-bottom-right-radius") X(BORDER_BL_RADIUS, "border-bottom-left-radius") \
    X(COLOR, "color") X(BACKGROUND_COLOR, "background-color") X(BACKGROUND_IMAGE, "background-image") \
    X(BACKGROUND_SIZE, "background-size") X(BACKGROUND_POSITION, "background-position") \
    X(BACKGROUND_REPEAT, "background-repeat") X(BACKGROUND_CLIP, "background-clip") X(BACKGROUND_ORIGIN, "background-origin") \
    X(BOX_SHADOW, "box-shadow") X(BORDER_COLLAPSE, "border-collapse") X(BORDER_SPACING, "border-spacing") \
    X(TABLE_LAYOUT, "table-layout") X(ASPECT_RATIO, "aspect-ratio") X(TRANSFORM, "transform") X(TRANSLATE, "translate") \
    X(CONTENT, "content") X(POINTER_EVENTS, "pointer-events") X(USER_SELECT, "user-select") \
    X(FILL, "fill") X(STROKE, "stroke") X(STROKE_WIDTH, "stroke-width") X(FILL_OPACITY, "fill-opacity") \
    X(STROKE_OPACITY, "stroke-opacity") X(STROKE_LINECAP, "stroke-linecap") X(STROKE_LINEJOIN, "stroke-linejoin") \
    /* shorthands */ \
    X(MARGIN, "margin") X(PADDING, "padding") X(BORDER, "border") X(BORDER_TOP, "border-top") \
    X(BORDER_RIGHT, "border-right") X(BORDER_BOTTOM, "border-bottom") X(BORDER_LEFT, "border-left") \
    X(BORDER_WIDTH, "border-width") X(BORDER_STYLE, "border-style") X(BORDER_COLOR, "border-color") \
    X(BORDER_RADIUS, "border-radius") X(BACKGROUND, "background") X(FLEX, "flex") X(FLEX_FLOW, "flex-flow") \
    X(FONT, "font") X(TEXT_DECORATION, "text-decoration") X(LIST_STYLE, "list-style") X(OVERFLOW, "overflow") \
    X(INSET, "inset") X(GAP, "gap") X(GRID_TEMPLATE, "grid-template") X(GRID_COLUMN, "grid-column") \
    X(GRID_ROW, "grid-row") X(PLACE_ITEMS, "place-items") X(PLACE_CONTENT, "place-content") X(PLACE_SELF, "place-self") \
    X(GRID, "grid") X(ALL, "all") X(WORD_WRAP, "word-wrap") X(GRID_GAP, "grid-gap") X(GRID_ROW_GAP, "grid-row-gap") \
    X(GRID_COLUMN_GAP, "grid-column-gap") X(MARGIN_INLINE, "margin-inline") X(MARGIN_BLOCK, "margin-block") \
    X(PADDING_INLINE, "padding-inline") X(PADDING_BLOCK, "padding-block") X(INSET_INLINE, "inset-inline") \
    X(INSET_BLOCK, "inset-block") X(MARGIN_INLINE_START, "margin-inline-start") X(MARGIN_INLINE_END, "margin-inline-end") \
    X(MARGIN_BLOCK_START, "margin-block-start") X(MARGIN_BLOCK_END, "margin-block-end") \
    X(PADDING_INLINE_START, "padding-inline-start") X(PADDING_INLINE_END, "padding-inline-end") \
    X(PADDING_BLOCK_START, "padding-block-start") X(PADDING_BLOCK_END, "padding-block-end") \
    X(INSET_INLINE_START, "inset-inline-start") X(INSET_INLINE_END, "inset-inline-end") \
    X(INSET_BLOCK_START, "inset-block-start") X(INSET_BLOCK_END, "inset-block-end") \
    X(WIDTH_INLINE, "inline-size") X(HEIGHT_BLOCK, "block-size") X(MIN_INLINE_SIZE, "min-inline-size") \
    X(MIN_BLOCK_SIZE, "min-block-size") X(MAX_INLINE_SIZE, "max-inline-size") X(MAX_BLOCK_SIZE, "max-block-size") \
    X(BORDER_START_START_RADIUS, "border-start-start-radius") X(BORDER_END_END_RADIUS, "border-end-end-radius") \
    X(OUTLINE, "outline") X(SCALE, "scale") X(ROTATE, "rotate") X(TEXT_WRAP, "text-wrap") X(ZOOM, "zoom") \
    X(BORDER_INLINE, "border-inline") X(BORDER_BLOCK, "border-block") X(BACKGROUND_POSITION_X, "background-position-x") \
    X(BACKGROUND_POSITION_Y, "background-position-y") X(OVERFLOW_INLINE, "overflow-inline") \
    X(OVERFLOW_BLOCK, "overflow-block") X(PLACE_ITEMS_X, "justify-items") X(JUSTIFY_SELF, "justify-self") \
    X(TEXT_DECORATION_THICKNESS, "text-decoration-thickness") X(TEXT_DECORATION_STYLE, "text-decoration-style") \
    X(WHITE_SPACE_COLLAPSE, "white-space-collapse") X(TEXT_SHADOW, "text-shadow") X(VISIBILITY_X, "-x-visibility")

enum prop_id {
    P_NONE = 0,
#define X(id, name) P_##id,
    PROP_LIST(X)
#undef X
    P_COUNT,
    P_CUSTOM = 1000
};

int css_prop_lookup(const char *name, int len);
const char *css_prop_name(int id);
bool css_prop_is_shorthand(int id);

// ---- Declarations and rules ----

struct decl {
    uint16_t prop;
    bool important;
    const char *value;
    int length;
    const char *custom;     // P_CUSTOM: the name, with "--"
    int custom_len;
};

struct selector;
struct media_cond {
    const char *text;
    int length;
    struct media_cond *parent;
    int cache_epoch;
    bool cache_value;
    bool supports;          // an @supports condition rather than @media
};

struct rule {
    struct selector **sels;
    int nsel;
    struct decl *decls;
    int ndecls;
    struct media_cond *media;
    int layer;              // 0: not in a layer
    uint32_t order;         // assigned when the page's rules are put in order
    int sheet_index;
};

struct sheet_item {
    struct rule *rule;      // one or the other
    struct sheet *import;
    struct media_cond *import_media;
};

struct sheet {
    struct page *page;
    struct sheet_item *items;
    int count, cap;
    struct sheet *next;     // in document order
    const char *base_url;
    bool user_agent;
    bool loaded;
    int order;
    struct node *owner;
};

void css_parse_declarations(struct page *p, const char *text, int length, struct decl **out, int *count, bool inline_style);

// Apply one declaration to a style. Returns false if the value isn't understood.
struct apply_ctx;
struct style_work;

// Selectors (web_csssel.c)
struct selector *css_parse_selector(struct page *p, const char *text, int length, int *used, bool *has_pseudo_element);
void css_parse_selector_list(struct page *p, const char *text, int length, struct selector ***out, int *count);
bool css_selector_matches(const struct selector *s, struct node *n);
int css_selector_specificity(const struct selector *s);
int css_selector_pseudo_element(const struct selector *s);       // 0 none, 1 before, 2 after
void css_selector_key(const struct selector *s, const char **key, int *kind);   // for bucketing: 1 id, 2 class, 3 tag, 0 none
void css_node_prepare(struct node *root);                           // compute sibling indexes

// Cascade (web_css.c)
struct css_index;
const struct style *css_default_style(void);
bool css_supports_prop(const char *name, int length);
struct vp;
bool css_apply_decl(struct style *s, const struct style *parent, const struct css_ctx *cx, const struct decl *d,
                    unsigned char *set, struct style *initial);

#endif
