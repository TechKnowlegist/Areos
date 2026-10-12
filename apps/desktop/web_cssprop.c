// web_cssprop.c - what each CSS property means: turning "margin: 0 auto"
// and friends into numbers in a style.

#include "web_css.h"

// ---- Property names ----

static const char *prop_names[P_COUNT] = {
    "",
#define X(id, name) name,
    PROP_LIST(X)
#undef X
};

#define PROP_HASH_SIZE 1024
static uint16_t prop_hash[PROP_HASH_SIZE];
static bool prop_hash_ready;

static unsigned name_hash(const char *s, int len) {
    unsigned h = 5381;
    for (int i = 0; i < len; i++) h = h * 33 + (unsigned)w_lower((unsigned char)s[i]);
    return h;
}

static void prop_hash_build(void) {
    for (int id = 1; id < P_COUNT; id++) {
        unsigned h = name_hash(prop_names[id], w_len(prop_names[id])) % PROP_HASH_SIZE;
        while (prop_hash[h]) h = (h + 1) % PROP_HASH_SIZE;
        prop_hash[h] = (uint16_t)id;
    }
    prop_hash_ready = true;
}

int css_prop_lookup(const char *name, int len) {
    if (!prop_hash_ready) prop_hash_build();
    // vendor prefixes: -webkit-box-shadow and the like mean the same thing
    if (len > 8 && name[0] == '-' && w_eq(name, 8, "-webkit-")) { name += 8; len -= 8; }
    else if (len > 5 && name[0] == '-' && w_eq(name, 5, "-moz-")) { name += 5; len -= 5; }
    unsigned h = name_hash(name, len) % PROP_HASH_SIZE;
    while (prop_hash[h]) {
        int id = prop_hash[h];
        if (w_eq(name, len, prop_names[id])) return id;
        h = (h + 1) % PROP_HASH_SIZE;
    }
    return 0;
}

const char *css_prop_name(int id) { return id > 0 && id < P_COUNT ? prop_names[id] : ""; }

bool css_supports_prop(const char *name, int length) { return css_prop_lookup(name, length) != 0; }

bool css_prop_is_shorthand(int id) { return id >= P_MARGIN && id < P_COUNT; }

// ---- The starting style ----

static struct style initial_style;
static bool initial_ready;

const struct style *css_default_style(void) {
    if (!initial_ready) {
        struct style *s = &initial_style;
        memset(s, 0, sizeof(*s));
        s->display = D_INLINE;
        s->visibility = 0;
        s->box_sizing = 0;
        s->font_size = 16;
        s->font_weight = 400;
        s->font_family = WF_SERIF;
        s->color = 0xFF000000;
        s->opacity = 255;
        s->flex_shrink = 1000;
        s->width.kind = L_AUTO; s->height.kind = L_AUTO;
        s->min_width.kind = L_AUTO; s->min_height.kind = L_AUTO;
        s->max_width.kind = L_NONE; s->max_height.kind = L_NONE;
        for (int i = 0; i < 4; i++) {
            s->margin[i].kind = L_VAL; s->padding[i].kind = L_VAL;
            s->inset[i].kind = L_AUTO;
            s->border_width[i] = 3;
            s->border_style[i] = BS_NONE;
            s->border_color[i] = COLOR_CURRENT;
            s->radius[i].kind = L_VAL;
        }
        s->flex_basis.kind = L_AUTO;
        s->row_gap.kind = L_VAL; s->column_gap.kind = L_VAL;
        s->text_indent.kind = L_VAL;
        s->vertical_align_len.kind = L_VAL;
        s->translate_x.kind = L_VAL; s->translate_y.kind = L_VAL;
        s->object_pos_x.kind = L_VAL; s->object_pos_x.pct = 5000;
        s->object_pos_y.kind = L_VAL; s->object_pos_y.pct = 5000;
        s->spacing_h.kind = L_VAL; s->spacing_v.kind = L_VAL;
        s->deco_color = COLOR_CURRENT;
        s->align_items = FA_NORMAL; s->justify_content = FA_NORMAL; s->align_content = FA_NORMAL; s->align_self = FA_AUTO;
        s->tab_size = 8;
        s->fill_opacity = 255; s->stroke_opacity = 255;
        s->auto_col.kind = TK_AUTO; s->auto_row.kind = TK_AUTO;
        initial_ready = true;
    }
    return &initial_style;
}

static bool is_ident_char_hack(int c) { return w_is_alnum(c) || c == '-' || c == '_'; }
static bool strstr_hack(const char *h, const char *n) {
    int nl = w_len(n);
    for (int i = 0; h[i]; i++) { int k = 0; while (k < nl && h[i + k] == n[k]) k++; if (k == nl) return true; }
    return false;
}

// ---- Variables: var(--name, fallback) ----

static const struct custom_prop *var_lookup(const struct custom_prop *vars, const char *name, int len) {
    for (const struct custom_prop *v = vars; v; v = v->next) {
        if ((int)strlen(v->name) == len && memcmp(v->name, name, (size_t)len) == 0) return v;
    }
    return NULL;
}

// Copy `in` to `out` with var() and env() replaced. Returns false if something was undefined.
static bool subst(const struct custom_prop *vars, const char *in, int len, char *out, int *olen, int cap, int depth) {
    if (depth > 12) return false;
    int o = *olen;
    for (int i = 0; i < len;) {
        char c = in[i];
        if (c == '"' || c == '\'') {
            int j = i + 1;
            while (j < len && in[j] != c) { if (in[j] == '\\') j++; j++; }
            if (j < len) j++;
            for (int k = i; k < j && o < cap - 1; k++) out[o++] = in[k];
            i = j;
            continue;
        }
        bool is_var = i + 4 <= len && w_eq(in + i, 3, "var") && in[i + 3] == '(' && (i == 0 || !is_ident_char_hack(in[i - 1]));
        bool is_env = i + 4 <= len && w_eq(in + i, 3, "env") && in[i + 3] == '(' && (i == 0 || !is_ident_char_hack(in[i - 1]));
        if (is_var || is_env) {
            int start = i + 4, depth_p = 1, k = start;
            while (k < len && depth_p > 0) {
                if (in[k] == '(') depth_p++;
                else if (in[k] == ')') depth_p--;
                else if (in[k] == '"' || in[k] == '\'') { char q = in[k++]; while (k < len && in[k] != q) k++; }
                k++;
            }
            int end = k - 1;      // the ')'
            if (end < start) return false;
            // name, then optional fallback after the first top-level comma
            int nstart = start;
            while (nstart < end && w_is_space(in[nstart])) nstart++;
            int nend = nstart;
            while (nend < end && in[nend] != ',' && !w_is_space(in[nend])) nend++;
            int comma = -1, dp = 0;
            for (int q = nend; q < end; q++) {
                if (in[q] == '(') dp++;
                else if (in[q] == ')') dp--;
                else if (in[q] == ',' && dp == 0) { comma = q; break; }
            }
            bool done = false;
            if (is_var) {
                const struct custom_prop *v = var_lookup(vars, in + nstart, nend - nstart);
                if (v) {
                    int before = o;
                    if (!subst(vars, v->value, v->length, out, &o, cap, depth + 1)) { o = before; }
                    else done = true;
                }
            }
            if (!done) {
                if (comma >= 0) {
                    int fs = comma + 1;
                    while (fs < end && w_is_space(in[fs])) fs++;
                    if (!subst(vars, in + fs, end - fs, out, &o, cap, depth + 1)) return false;
                } else if (is_env) {
                    out[o++] = '0'; out[o++] = 'p'; out[o++] = 'x';
                } else return false;
            }
            i = k;
            continue;
        }
        if (o < cap - 1) out[o++] = c;
        i++;
    }
    *olen = o;
    return true;
}

// ---- The work area ----

struct A {
    struct style *s;
    const struct style *parent;
    const struct style *initial;
    struct css_ctx cx;
    unsigned char *set;
};

#define MARK(prop) (a->set[prop] = 1)
#define FREE_(prop) (!a->set[prop])

static const char *const kw_display[] = { "none", "block", "inline", "inline-block", "flex", "inline-flex", "grid", "inline-grid", "list-item",
    "table", "inline-table", "table-row-group", "table-header-group", "table-footer-group", "table-row", "table-cell", "table-caption",
    "table-column", "table-column-group", "contents", "flow-root", "ruby", "-webkit-box", "-webkit-inline-box", "-webkit-flex", "-webkit-inline-flex", "run-in", NULL };
static const char *const kw_position[] = { "static", "relative", "absolute", "fixed", "sticky", "-webkit-sticky", NULL };
static const char *const kw_float[] = { "none", "left", "right", "inline-start", "inline-end", NULL };
static const char *const kw_clear[] = { "none", "left", "right", "both", "inline-start", "inline-end", NULL };
static const char *const kw_overflow[] = { "visible", "hidden", "scroll", "auto", "clip", "overlay", NULL };
static const char *const kw_box_sizing[] = { "content-box", "border-box", "padding-box", NULL };
static const char *const kw_text_align[] = { "start", "left", "right", "center", "justify", "end", "match-parent", "-webkit-center", "-webkit-left", "-webkit-right", "-moz-center", NULL };
static const char *const kw_white_space[] = { "normal", "nowrap", "pre", "pre-wrap", "pre-line", "break-spaces", NULL };
static const char *const kw_text_transform[] = { "none", "uppercase", "lowercase", "capitalize", NULL };
static const char *const kw_va[] = { "baseline", "top", "middle", "bottom", "text-top", "text-bottom", "sub", "super", NULL };
static const char *const kw_list_type[] = { "disc", "circle", "square", "decimal", "decimal-leading-zero", "lower-alpha", "upper-alpha", "lower-roman",
    "upper-roman", "lower-greek", "none", "lower-latin", "upper-latin", NULL };
static const char *const kw_border_style[] = { "none", "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset", "hidden", NULL };
static const char *const kw_flex_dir[] = { "row", "row-reverse", "column", "column-reverse", NULL };
static const char *const kw_flex_wrap[] = { "nowrap", "wrap", "wrap-reverse", NULL };
static const char *const kw_align[] = { "auto", "flex-start", "flex-end", "center", "stretch", "baseline", "space-between", "space-around",
    "space-evenly", "normal", "start", "end", "self-start", "self-end", "left", "right", "first baseline", "last baseline", "safe", "unsafe", NULL };
static const char *const kw_object_fit[] = { "fill", "contain", "cover", "none", "scale-down", NULL };
static const char *const kw_word_break[] = { "normal", "break-all", "keep-all", "break-word", NULL };
static const char *const kw_visibility[] = { "visible", "hidden", "collapse", NULL };

static uint8_t map_align(int k) {
    switch (k) {
    case 0: return FA_AUTO; case 1: case 10: case 12: case 14: return FA_START; case 2: case 11: case 13: case 15: return FA_END;
    case 3: return FA_CENTER; case 4: return FA_STRETCH; case 5: case 16: case 17: return FA_BASELINE; case 6: return FA_SPACE_BETWEEN;
    case 7: return FA_SPACE_AROUND; case 8: return FA_SPACE_EVENLY; case 9: return FA_NORMAL;
    default: return FA_AUTO;
    }
}

static bool at_end(struct vp *v) { struct vtoken t; return !vp_peek(v, &t); }

static bool read_len(struct A *a, struct vp *v, struct len *out, int flags) {
    return css_parse_len(v, &a->cx, out, flags);
}

// width/height style values: auto | length | keywords
static bool read_size(struct A *a, struct vp *v, struct len *out, bool allow_none) {
    struct vtoken t;
    if (!vp_next(v, &t)) return false;
    memset(out, 0, sizeof(*out));
    if (t.kind == V_IDENT) {
        if (w_eq(t.s, t.n, "auto")) { out->kind = L_AUTO; return true; }
        if (w_eq(t.s, t.n, "none") && allow_none) { out->kind = L_NONE; return true; }
        if (w_eq(t.s, t.n, "min-content") || w_eq(t.s, t.n, "-webkit-min-content") || w_eq(t.s, t.n, "-moz-min-content")) { out->kind = L_MIN_CONTENT; return true; }
        if (w_eq(t.s, t.n, "max-content") || w_eq(t.s, t.n, "-webkit-max-content") || w_eq(t.s, t.n, "-moz-max-content")) { out->kind = L_MAX_CONTENT; return true; }
        if (w_eq(t.s, t.n, "fit-content") || w_eq(t.s, t.n, "-moz-fit-content") || w_eq(t.s, t.n, "-webkit-fit-content")) { out->kind = L_FIT_CONTENT; return true; }
        if (w_eq(t.s, t.n, "stretch") || w_eq(t.s, t.n, "-webkit-fill-available") || w_eq(t.s, t.n, "-moz-available") || w_eq(t.s, t.n, "fill-available") || w_eq(t.s, t.n, "fill")) { out->kind = L_VAL; out->pct = 10000; return true; }
        return false;
    }
    if (t.kind == V_FUNC && w_eq(t.s, t.n, "fit-content")) { struct vp skip; vp_func_args(v, &skip); out->kind = L_FIT_CONTENT; return true; }
    return css_parse_len_tok(v, &t, &a->cx, out, LF_NONNEG);
}

static bool read_inset(struct A *a, struct vp *v, struct len *out) {
    struct vtoken t;
    if (!vp_next(v, &t)) return false;
    memset(out, 0, sizeof(*out));
    if (tok_is(&t, "auto")) { out->kind = L_AUTO; return true; }
    return css_parse_len_tok(v, &t, &a->cx, out, 0);
}

static void set_len4(struct A *a, int p0, struct len *dst[4], struct vp *v, int flags, bool allow_auto) {
    struct len vals[4];
    int n = 0;
    struct vtoken t;
    while (n < 4 && vp_next(v, &t)) {
        if (allow_auto && tok_is(&t, "auto")) { memset(&vals[n], 0, sizeof(struct len)); vals[n].kind = L_AUTO; n++; continue; }
        if (!css_parse_len_tok(v, &t, &a->cx, &vals[n], flags)) return;
        n++;
    }
    if (n == 0) return;
    int map[4];
    if (n == 1) { map[0] = map[1] = map[2] = map[3] = 0; }
    else if (n == 2) { map[0] = 0; map[1] = 1; map[2] = 0; map[3] = 1; }
    else if (n == 3) { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 1; }
    else { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 3; }
    for (int i = 0; i < 4; i++) {
        if (FREE_(p0 + i)) { *dst[i] = vals[map[i]]; MARK(p0 + i); }
    }
}

static uint32_t resolve_current(const struct A *a, uint32_t c) { return c == COLOR_CURRENT ? a->s->color : c; }

static int border_width_value(struct A *a, struct vtoken *t, struct vp *v, bool *ok) {
    *ok = true;
    if (t->kind == V_IDENT) {
        if (w_eq(t->s, t->n, "thin")) return 1;
        if (w_eq(t->s, t->n, "medium")) return 3;
        if (w_eq(t->s, t->n, "thick")) return 5;
        *ok = false; return 0;
    }
    struct len l;
    if (!css_parse_len_tok(v, t, &a->cx, &l, LF_NONNEG | LF_NO_PCT)) { *ok = false; return 0; }
    int px = l.px;
    if (l.expr) px = len_resolve(&l, 0);
    // thin lines stay visible: anything between 0 and 1 pixel counts as one
    return px;
}

static bool parse_family(struct vp *v, uint8_t *family) {
    struct vtoken t;
    static const char *const mono[] = { "monospace", "courier", "courier new", "consolas", "menlo", "monaco", "lucida console", "ui-monospace",
        "sf mono", "sfmono-regular", "source code pro", "fira code", "fira mono", "jetbrains mono", "roboto mono", "dejavu sans mono", "liberation mono",
        "cascadia code", "cascadia mono", "andale mono", "ibm plex mono", "inconsolata", "space mono", "hack", "noto sans mono", NULL };
    static const char *const serif[] = { "serif", "times", "times new roman", "georgia", "garamond", "palatino", "palatino linotype", "cambria", "book antiqua",
        "ui-serif", "baskerville", "didot", "bodoni", "playfair display", "merriweather", "lora", "liberation serif", "noto serif", "source serif pro",
        "crimson text", "libre baskerville", "pt serif", "eb garamond", "cormorant garamond", "dejavu serif", "charter", "iowan old style", NULL };
    static const char *const sans[] = { "sans-serif", "system-ui", "-apple-system", "blinkmacsystemfont", "arial", "helvetica", "helvetica neue", "inter",
        "roboto", "segoe ui", "ui-sans-serif", "verdana", "tahoma", "trebuchet ms", "open sans", "lato", "montserrat", "poppins", "nunito", "dejavu sans",
        "liberation sans", "noto sans", "source sans pro", "ubuntu", "cantarell", "oxygen", "fira sans", "droid sans", "geist", "manrope", "dm sans",
        "work sans", "raleway", "sf pro text", "sf pro display", "avenir", "gill sans", "calibri", "arimo", "rubik", "ui-rounded", "emoji", NULL };
    bool any = false;
    while (vp_next(v, &t)) {
        if (t.kind == V_COMMA) continue;
        char name[80];
        int n = 0;
        if (t.kind == V_STR) {
            for (int i = 0; i < t.n && n < 79; i++) name[n++] = (char)w_lower((unsigned char)t.s[i]);
        } else if (t.kind == V_IDENT) {
            for (int i = 0; i < t.n && n < 79; i++) name[n++] = (char)w_lower((unsigned char)t.s[i]);
            // multi-word unquoted names: Segoe UI
            struct vtoken nx;
            while (vp_peek(v, &nx) && nx.kind == V_IDENT && n < 78) { vp_next(v, &nx); name[n++] = ' '; for (int i = 0; i < nx.n && n < 79; i++) name[n++] = (char)w_lower((unsigned char)nx.s[i]); }
        } else continue;
        name[n] = '\0';
        any = true;
        for (int i = 0; mono[i]; i++) if (strcmp(name, mono[i]) == 0) { *family = WF_MONO; return true; }
        for (int i = 0; serif[i]; i++) if (strcmp(name, serif[i]) == 0) { *family = WF_SERIF; return true; }
        for (int i = 0; sans[i]; i++) if (strcmp(name, sans[i]) == 0) { *family = WF_SANS; return true; }
        if (strstr_hack(name, "mono") || strstr_hack(name, "code") || strstr_hack(name, "courier")) { *family = WF_MONO; return true; }
        // unknown name: try the next one in the list
    }
    if (any) *family = WF_SANS;
    return any;
}

// ---- Backgrounds ----

static bool parse_gradient(struct A *a, struct vp *args, const struct vtoken *fn, struct gradient **out) {
    struct gradient *g = w_alloc(&a->cx.page->larena, sizeof(struct gradient));
    if (!g) return false;
    bool lin = w_eq(fn->s, fn->n, "linear-gradient") || w_eq(fn->s, fn->n, "-webkit-linear-gradient") || w_eq(fn->s, fn->n, "repeating-linear-gradient");
    bool rad = w_eq(fn->s, fn->n, "radial-gradient") || w_eq(fn->s, fn->n, "-webkit-radial-gradient") || w_eq(fn->s, fn->n, "repeating-radial-gradient");
    bool conic = w_eq(fn->s, fn->n, "conic-gradient") || w_eq(fn->s, fn->n, "repeating-conic-gradient");
    if (!lin && !rad && !conic) return false;
    g->repeating = fn->n > 9 && w_eq(fn->s, 9, "repeating");
    g->radial = rad || conic;
    g->angle = 1800;        // to bottom
    g->cx.kind = L_VAL; g->cx.pct = 5000; g->cy.kind = L_VAL; g->cy.pct = 5000;
    g->extent = 0;
    struct vtoken t;
    // optional first argument: direction / shape
    struct vp save = *args;
    if (lin) {
        if (vp_peek(args, &t)) {
            if (tok_is(&t, "to")) {
                vp_next(args, &t);
                int dx = 0, dy = 0;
                while (vp_peek(args, &t) && t.kind == V_IDENT) {
                    vp_next(args, &t);
                    if (w_eq(t.s, t.n, "left")) dx = -1; else if (w_eq(t.s, t.n, "right")) dx = 1;
                    else if (w_eq(t.s, t.n, "top")) dy = -1; else if (w_eq(t.s, t.n, "bottom")) dy = 1;
                }
                if (dx != 0 && dy != 0) { g->to_corner = true; g->corner_x = dx; g->corner_y = dy; }
                else if (dx == 1) g->angle = 900; else if (dx == -1) g->angle = 2700;
                else if (dy == -1) g->angle = 0; else g->angle = 1800;
                if (vp_peek(args, &t) && t.kind == V_COMMA) vp_next(args, &t);
            } else if (t.kind == V_DIM || t.kind == V_NUM) {
                struct vp tryv = *args;
                int ang;
                if (css_parse_angle(&tryv, &ang)) {
                    *args = tryv;
                    g->angle = ((ang % 3600) + 3600) % 3600;
                    if (vp_peek(args, &t) && t.kind == V_COMMA) vp_next(args, &t);
                }
            }
        }
    } else if (rad) {
        // [circle|ellipse] [size] [at x y],
        while (vp_peek(args, &t)) {
            if (t.kind == V_COMMA) { vp_next(args, &t); break; }
            if (t.kind == V_IDENT && w_eq(t.s, t.n, "circle")) { vp_next(args, &t); g->shape = 1; continue; }
            if (t.kind == V_IDENT && w_eq(t.s, t.n, "ellipse")) { vp_next(args, &t); g->shape = 0; continue; }
            if (t.kind == V_IDENT && w_eq(t.s, t.n, "closest-side")) { vp_next(args, &t); g->extent = 1; continue; }
            if (t.kind == V_IDENT && w_eq(t.s, t.n, "closest-corner")) { vp_next(args, &t); g->extent = 2; continue; }
            if (t.kind == V_IDENT && w_eq(t.s, t.n, "farthest-side")) { vp_next(args, &t); g->extent = 3; continue; }
            if (t.kind == V_IDENT && w_eq(t.s, t.n, "farthest-corner")) { vp_next(args, &t); g->extent = 0; continue; }
            if (t.kind == V_IDENT && w_eq(t.s, t.n, "at")) {
                vp_next(args, &t);
                // position: up to two of keyword/length
                struct len xs[2]; int nx = 0; bool kw_left = false;
                (void)kw_left;
                int axis[2] = { -1, -1 };
                while (nx < 2 && vp_peek(args, &t) && t.kind != V_COMMA) {
                    vp_next(args, &t);
                    if (t.kind == V_IDENT) {
                        memset(&xs[nx], 0, sizeof(struct len)); xs[nx].kind = L_VAL;
                        if (w_eq(t.s, t.n, "left")) { xs[nx].pct = 0; axis[nx] = 0; }
                        else if (w_eq(t.s, t.n, "right")) { xs[nx].pct = 10000; axis[nx] = 0; }
                        else if (w_eq(t.s, t.n, "top")) { xs[nx].pct = 0; axis[nx] = 1; }
                        else if (w_eq(t.s, t.n, "bottom")) { xs[nx].pct = 10000; axis[nx] = 1; }
                        else if (w_eq(t.s, t.n, "center")) { xs[nx].pct = 5000; axis[nx] = -1; }
                        else break;
                        nx++;
                    } else if (css_parse_len_tok(args, &t, &a->cx, &xs[nx], 0)) nx++;
                    else break;
                }
                if (nx >= 1) {
                    if (axis[0] == 1) { g->cy = xs[0]; if (nx > 1) g->cx = xs[1]; }
                    else { g->cx = xs[0]; if (nx > 1) g->cy = xs[1]; }
                }
                continue;
            }
            // explicit sizes
            struct vp tryv = *args;
            struct len l1;
            if (css_parse_len(&tryv, &a->cx, &l1, LF_NONNEG)) {
                *args = tryv;
                g->rx = l1; g->ry = l1;
                struct vtoken t2;
                struct vp try2 = *args;
                if (vp_peek(&try2, &t2) && (t2.kind == V_DIM || t2.kind == V_PCT || t2.kind == V_NUM)) {
                    struct len l2;
                    if (css_parse_len(&try2, &a->cx, &l2, LF_NONNEG)) { g->ry = l2; *args = try2; }
                } else g->shape = 1;
                g->extent = 4;
                continue;
            }
            break;
        }
    } else {
        // conic: treat as a radial gradient of its stops (an approximation)
        if (vp_peek(args, &t) && (tok_is(&t, "from") || tok_is(&t, "at"))) {
            while (vp_peek(args, &t) && t.kind != V_COMMA) vp_next(args, &t);
            if (vp_peek(args, &t) && t.kind == V_COMMA) vp_next(args, &t);
        }
    }
    (void)save;
    // color stops
    while (g->stop_count < MAX_STOPS) {
        struct vtoken pk;
        if (!vp_peek(args, &pk)) break;
        if (pk.kind == V_COMMA) { vp_next(args, &pk); continue; }
        struct gradient_stop *st = &g->stops[g->stop_count];
        memset(st, 0, sizeof(*st));
        if (!css_parse_color(args, &a->cx, &st->color)) {
            // a color hint (a bare percentage): skip it
            struct len hint;
            if (css_parse_len(args, &a->cx, &hint, 0)) continue;
            return false;
        }
        st->pos.kind = L_VAL;
        struct vtoken nx;
        if (vp_peek(args, &nx) && nx.kind != V_COMMA) {
            struct len p1;
            if (css_parse_len(args, &a->cx, &p1, 0)) {
                st->pos = p1; st->has_pos = true;
                // a color stop with two positions is two stops
                if (vp_peek(args, &nx) && nx.kind != V_COMMA && g->stop_count + 1 < MAX_STOPS) {
                    struct len p2;
                    if (css_parse_len(args, &a->cx, &p2, 0)) {
                        g->stop_count++;
                        struct gradient_stop *st2 = &g->stops[g->stop_count];
                        *st2 = *st; st2->pos = p2; st2->has_pos = true;
                    }
                }
            }
        }
        g->stop_count++;
    }
    if (g->stop_count == 0) return false;
    *out = g;
    return true;
}

static const char *copy_str(struct A *a, const char *s, int n) { return w_strdup(&a->cx.page->larena, s, n); }

static bool parse_url_arg(struct A *a, struct vp *args, const char **out) {
    // contents of url( ... ): trim, strip quotes
    const char *s = args->s;
    int n = args->len;
    int i = 0;
    while (i < n && w_is_space(s[i])) i++;
    while (n > i && w_is_space(s[n - 1])) n--;
    if (n - i >= 2 && (s[i] == '"' || s[i] == '\'') && s[n - 1] == s[i]) { i++; n--; }
    *out = copy_str(a, s + i, n - i);
    return *out != NULL;
}

// One background layer's image: url(...), a gradient, or none.
static bool parse_bg_image(struct A *a, struct vp *v, struct bg_layer *layer, bool *got) {
    struct vtoken t;
    if (!vp_peek(v, &t)) return false;
    *got = false;
    if (t.kind == V_IDENT && w_eq(t.s, t.n, "none")) { vp_next(v, &t); *got = true; return true; }
    if (t.kind == V_FUNC) {
        if (w_eq(t.s, t.n, "url")) {
            vp_next(v, &t);
            struct vp args; vp_func_args(v, &args);
            parse_url_arg(a, &args, &layer->url);
            *got = true;
            return true;
        }
        if (w_eq(t.s, t.n, "image-set") || w_eq(t.s, t.n, "-webkit-image-set")) {
            vp_next(v, &t);
            struct vp args; vp_func_args(v, &args);
            struct vtoken x;
            while (vp_next(&args, &x)) {
                if (x.kind == V_STR) { layer->url = copy_str(a, x.s, x.n); break; }
                if (x.kind == V_FUNC && w_eq(x.s, x.n, "url")) { struct vp u; vp_func_args(&args, &u); parse_url_arg(a, &u, &layer->url); break; }
            }
            *got = true;
            return true;
        }
        if (w_eq(t.s, t.n, "linear-gradient") || w_eq(t.s, t.n, "radial-gradient") || w_eq(t.s, t.n, "conic-gradient")
            || w_eq(t.s, t.n, "repeating-linear-gradient") || w_eq(t.s, t.n, "repeating-radial-gradient") || w_eq(t.s, t.n, "repeating-conic-gradient")
            || w_eq(t.s, t.n, "-webkit-linear-gradient") || w_eq(t.s, t.n, "-webkit-radial-gradient")) {
            vp_next(v, &t);
            struct vp args; vp_func_args(v, &args);
            if (!parse_gradient(a, &args, &t, &layer->gradient)) { *got = true; return true; }   // unusable gradient: no image
            *got = true;
            return true;
        }
    }
    return true;
}

static void bg_defaults(struct bg_layer *l) {
    memset(l, 0, sizeof(*l));
    l->repeat_x = l->repeat_y = true;
    l->size_mode = BGS_AUTO;
    l->pos_x.kind = L_VAL; l->pos_y.kind = L_VAL;
    l->size_w.kind = L_AUTO; l->size_h.kind = L_AUTO;
    l->origin = 0; l->clip = 0;
}

static bool parse_bg_position(struct A *a, struct vp *v, struct len *x, struct len *y) {
    // 1 to 4 tokens: keywords and lengths
    struct vtoken t;
    struct len tmp[4]; int axis[4]; int n = 0;
    while (n < 4 && vp_peek(v, &t)) {
        if (t.kind == V_COMMA || t.kind == V_SLASH) break;
        struct vp copy = *v;
        vp_next(&copy, &t);
        memset(&tmp[n], 0, sizeof(struct len)); tmp[n].kind = L_VAL;
        axis[n] = -1;
        if (t.kind == V_IDENT) {
            if (w_eq(t.s, t.n, "left")) { tmp[n].pct = 0; axis[n] = 0; }
            else if (w_eq(t.s, t.n, "right")) { tmp[n].pct = 10000; axis[n] = 0; }
            else if (w_eq(t.s, t.n, "top")) { tmp[n].pct = 0; axis[n] = 1; }
            else if (w_eq(t.s, t.n, "bottom")) { tmp[n].pct = 10000; axis[n] = 1; }
            else if (w_eq(t.s, t.n, "center")) { tmp[n].pct = 5000; axis[n] = -1; }
            else break;
        } else if (!css_parse_len_tok(&copy, &t, &a->cx, &tmp[n], 0)) break;
        *v = copy;
        n++;
    }
    if (n == 0) return false;
    x->kind = L_VAL; y->kind = L_VAL; x->pct = 5000; y->pct = 5000; x->px = y->px = 0;
    if (n == 1) {
        if (axis[0] == 1) *y = tmp[0]; else *x = tmp[0];
    } else if (n == 2) {
        if (axis[0] == 1 || axis[1] == 0) { *y = tmp[0]; *x = tmp[1]; }
        else { *x = tmp[0]; *y = tmp[1]; }
    } else {
        // keyword offset forms: "right 10px top 20px" or "left 10px center"
        int i = 0;
        struct len *dst[2] = { x, y };
        while (i < n) {
            int ax = axis[i];
            if (ax < 0) ax = (dst[0] == x && i == 0) ? 0 : 1;
            struct len base = tmp[i];
            i++;
            if (i < n && axis[i] == -1 && (tmp[i].pct == 0 || true) && (tmp[i].kind == L_VAL) && axis[i - 1] >= 0) {
                struct len off = tmp[i]; i++;
                // offset from the edge: right 10px = 100% - 10px
                if (base.pct == 10000) { base.px = -off.px; base.pct = 10000 - off.pct; }
                else { base.px = off.px; base.pct += off.pct; }
            }
            if (ax == 0) *x = base; else *y = base;
        }
    }
    return true;
}

static bool parse_background_layer(struct A *a, struct vp *v, struct bg_layer *layer, uint32_t *color, bool *has_color) {
    bg_defaults(layer);
    struct vtoken t;
    bool any = false, got_image = false, got_pos = false;
    while (vp_peek(v, &t)) {
        if (t.kind == V_COMMA) break;
        // image
        if (!got_image && (t.kind == V_FUNC || (t.kind == V_IDENT && w_eq(t.s, t.n, "none")))) {
            bool got;
            struct vp tryv = *v;
            if (t.kind == V_FUNC) {
                parse_bg_image(a, v, layer, &got);
                if (got) { got_image = true; any = true; continue; }
                *v = tryv;
            } else { vp_next(v, &t); got_image = true; any = true; continue; }
        }
        // color
        {
            struct vp tryv = *v;
            uint32_t c;
            if ((t.kind == V_HASH || t.kind == V_FUNC || t.kind == V_IDENT) && css_parse_color(&tryv, &a->cx, &c)) {
                *v = tryv; *color = c; *has_color = true; any = true; continue;
            }
        }
        if (t.kind == V_IDENT) {
            vp_next(v, &t);
            if (w_eq(t.s, t.n, "no-repeat")) { layer->repeat_x = layer->repeat_y = false; }
            else if (w_eq(t.s, t.n, "repeat") || w_eq(t.s, t.n, "space") || w_eq(t.s, t.n, "round")) {
                layer->repeat_x = layer->repeat_y = true;
                struct vtoken nx;
                if (vp_peek(v, &nx) && nx.kind == V_IDENT && (w_eq(nx.s, nx.n, "no-repeat") || w_eq(nx.s, nx.n, "repeat"))) { vp_next(v, &nx); layer->repeat_y = !w_eq(nx.s, nx.n, "no-repeat"); }
            }
            else if (w_eq(t.s, t.n, "repeat-x")) { layer->repeat_x = true; layer->repeat_y = false; }
            else if (w_eq(t.s, t.n, "repeat-y")) { layer->repeat_x = false; layer->repeat_y = true; }
            else if (w_eq(t.s, t.n, "scroll") || w_eq(t.s, t.n, "fixed") || w_eq(t.s, t.n, "local")) { }
            else if (w_eq(t.s, t.n, "border-box")) { layer->origin = 1; layer->clip = 0; }
            else if (w_eq(t.s, t.n, "padding-box")) { layer->origin = 0; layer->clip = 1; }
            else if (w_eq(t.s, t.n, "content-box")) { layer->origin = 2; layer->clip = 2; }
            else if (w_eq(t.s, t.n, "text")) { layer->clip = 3; }
            else {
                // position keywords
                struct vp back = *v;
                (void)back;
                // un-read: positions start at this token
                v->pos -= t.n;
                if (!parse_bg_position(a, v, &layer->pos_x, &layer->pos_y)) return false;
                got_pos = true;
            }
            any = true;
            continue;
        }
        if (t.kind == V_SLASH) {
            vp_next(v, &t);
            // size
            struct vtoken s1;
            if (!vp_peek(v, &s1)) return false;
            if (tok_is(&s1, "cover")) { vp_next(v, &s1); layer->size_mode = BGS_COVER; }
            else if (tok_is(&s1, "contain")) { vp_next(v, &s1); layer->size_mode = BGS_CONTAIN; }
            else {
                layer->size_mode = BGS_LEN;
                struct len w1, h1;
                w1.kind = h1.kind = L_AUTO; w1.expr = h1.expr = NULL; w1.px = w1.pct = h1.px = h1.pct = 0;
                vp_next(v, &s1);
                if (tok_is(&s1, "auto")) w1.kind = L_AUTO;
                else if (!css_parse_len_tok(v, &s1, &a->cx, &w1, LF_NONNEG)) return false;
                struct vtoken s2;
                if (vp_peek(v, &s2) && s2.kind != V_COMMA && s2.kind != V_SLASH && (s2.kind == V_DIM || s2.kind == V_PCT || s2.kind == V_NUM || tok_is(&s2, "auto") || s2.kind == V_FUNC)
                    && !(s2.kind == V_FUNC && (w_eq(s2.s, s2.n, "url") || w_eq(s2.s, s2.n, "rgb") || w_eq(s2.s, s2.n, "rgba") || w_eq(s2.s, s2.n, "hsl") || w_eq(s2.s, s2.n, "hsla") || w_eq(s2.s, s2.n, "linear-gradient")))) {
                    vp_next(v, &s2);
                    if (tok_is(&s2, "auto")) h1.kind = L_AUTO;
                    else if (!css_parse_len_tok(v, &s2, &a->cx, &h1, LF_NONNEG)) return false;
                }
                layer->size_w = w1; layer->size_h = h1;
            }
            any = true;
            continue;
        }
        if (t.kind == V_DIM || t.kind == V_PCT || t.kind == V_NUM || (t.kind == V_FUNC)) {
            if (!parse_bg_position(a, v, &layer->pos_x, &layer->pos_y)) return false;
            got_pos = true;
            any = true;
            continue;
        }
        return false;
    }
    (void)got_pos;
    return any;
}

// ---- box-shadow ----

static bool parse_shadows(struct A *a, struct vp *v) {
    struct style *s = a->s;
    s->shadow_count = 0;
    struct vtoken t;
    if (vp_peek(v, &t) && tok_is(&t, "none")) return true;
    for (;;) {
        struct shadow sh;
        memset(&sh, 0, sizeof(sh));
        sh.color = a->s->color;
        bool has_color = false;
        struct len nums[4]; int n = 0;
        bool bad = false;
        while (vp_peek(v, &t) && t.kind != V_COMMA) {
            if (tok_is(&t, "inset")) { vp_next(v, &t); sh.inset = true; continue; }
            struct vp tryv = *v;
            uint32_t c;
            if (!has_color && (t.kind == V_HASH || t.kind == V_FUNC || t.kind == V_IDENT) && css_parse_color(&tryv, &a->cx, &c)) { *v = tryv; sh.color = c; has_color = true; continue; }
            vp_next(v, &t);
            if (n >= 4 || !css_parse_len_tok(v, &t, &a->cx, &nums[n], 0)) { bad = true; break; }
            n++;
        }
        if (bad || n < 2) return false;
        sh.x = nums[0].px; sh.y = nums[1].px;
        sh.blur = n > 2 ? nums[2].px : 0; if (sh.blur < 0) sh.blur = 0;
        sh.spread = n > 3 ? nums[3].px : 0;
        if (s->shadow_count < MAX_SHADOWS) s->shadows[s->shadow_count++] = sh;
        if (vp_peek(v, &t) && t.kind == V_COMMA) { vp_next(v, &t); continue; }
        break;
    }
    return true;
}

// ---- Grid track lists ----

static bool parse_track_one(struct A *a, struct vp *v, struct vtoken *t, struct track *tr) {
    memset(tr, 0, sizeof(*tr));
    if (t->kind == V_IDENT) {
        if (w_eq(t->s, t->n, "auto")) { tr->kind = TK_AUTO; return true; }
        if (w_eq(t->s, t->n, "min-content")) { tr->kind = TK_MIN_CONTENT; return true; }
        if (w_eq(t->s, t->n, "max-content")) { tr->kind = TK_MAX_CONTENT; return true; }
        return false;
    }
    if (t->kind == V_DIM && w_eq(t->unit, t->unit_len, "fr")) { tr->kind = TK_FR; tr->v = t->num; return true; }
    if (t->kind == V_FUNC) {
        if (w_eq(t->s, t->n, "minmax")) {
            struct vp args; vp_func_args(v, &args);
            struct vtoken x;
            tr->kind = TK_MINMAX;
            for (int side = 0; side < 2; side++) {
                if (!vp_next(&args, &x)) return false;
                uint8_t k; int val = 0;
                if (x.kind == V_IDENT && w_eq(x.s, x.n, "auto")) k = TK_AUTO;
                else if (x.kind == V_IDENT && w_eq(x.s, x.n, "min-content")) k = TK_MIN_CONTENT;
                else if (x.kind == V_IDENT && w_eq(x.s, x.n, "max-content")) k = TK_MAX_CONTENT;
                else if (x.kind == V_DIM && w_eq(x.unit, x.unit_len, "fr")) { k = TK_FR; val = x.num; }
                else {
                    struct len l;
                    if (!css_parse_len_tok(&args, &x, &a->cx, &l, LF_NONNEG)) return false;
                    if (l.pct) { k = TK_PCT; val = l.pct; } else { k = TK_PX; val = l.px; }
                }
                if (side == 0) { tr->min_kind = k; tr->min_v = val; } else { tr->max_kind = k; tr->max_v = val; }
                if (side == 0 && vp_peek(&args, &x) && x.kind == V_COMMA) vp_next(&args, &x);
            }
            return true;
        }
        if (w_eq(t->s, t->n, "fit-content")) {
            struct vp args; vp_func_args(v, &args);
            struct len l;
            if (!css_parse_len(&args, &a->cx, &l, LF_NONNEG)) return false;
            tr->kind = TK_AUTO; tr->has_fit = true; tr->fit = l;
            tr->kind = TK_MINMAX; tr->min_kind = TK_AUTO; tr->max_kind = l.pct ? TK_PCT : TK_PX; tr->max_v = l.pct ? l.pct : l.px;
            return true;
        }
    }
    struct len l;
    if (!css_parse_len_tok(v, t, &a->cx, &l, LF_NONNEG)) return false;
    if (l.pct && !l.px) { tr->kind = TK_PCT; tr->v = l.pct; } else { tr->kind = TK_PX; tr->v = l.px; if (l.pct) { tr->kind = TK_PCT; tr->v = l.pct; } }
    return true;
}

static void parse_track_list(struct A *a, struct vp *v, struct track *out, int *count) {
    *count = 0;
    struct vtoken t;
    while (vp_next(v, &t)) {
        if (t.kind == V_DELIM && t.delim == '[') { while (vp_next(v, &t) && !(t.kind == V_DELIM && t.delim == ']')) {} continue; }
        if (t.kind == V_IDENT && (w_eq(t.s, t.n, "none") || w_eq(t.s, t.n, "subgrid") || w_eq(t.s, t.n, "masonry"))) { *count = 0; return; }
        if (t.kind == V_FUNC && w_eq(t.s, t.n, "repeat")) {
            struct vp args; vp_func_args(v, &args);
            struct vtoken c;
            if (!vp_next(&args, &c)) continue;
            int reps = 1; int mode = 0;
            if (c.kind == V_NUM) reps = c.num / 1000;
            else if (tok_is(&c, "auto-fill")) mode = 1;
            else if (tok_is(&c, "auto-fit")) mode = 2;
            else continue;
            struct vtoken comma;
            if (vp_peek(&args, &comma) && comma.kind == V_COMMA) vp_next(&args, &comma);
            struct track pattern[MAX_TRACKS]; int pn = 0;
            struct vtoken x;
            while (pn < MAX_TRACKS && vp_next(&args, &x)) {
                if (x.kind == V_DELIM && x.delim == '[') { while (vp_next(&args, &x) && !(x.kind == V_DELIM && x.delim == ']')) {} continue; }
                if (!parse_track_one(a, &args, &x, &pattern[pn])) { pn = 0; break; }
                pn++;
            }
            if (pn == 0) continue;
            if (mode) {
                if (*count < MAX_TRACKS) { out[*count] = pattern[0]; out[*count].repeat = mode; (*count)++; }
            } else {
                if (reps < 1) reps = 1;
                for (int r = 0; r < reps && *count < MAX_TRACKS; r++)
                    for (int k = 0; k < pn && *count < MAX_TRACKS; k++) out[(*count)++] = pattern[k];
            }
            continue;
        }
        struct track tr;
        if (!parse_track_one(a, v, &t, &tr)) return;
        if (*count < MAX_TRACKS) out[(*count)++] = tr;
    }
}

// grid-column-start etc: "auto", "3", "-1", "span 2", name
static void parse_grid_line(struct vp *v, int *start, int *span) {
    struct vtoken t;
    *start = 0; *span = 0;
    while (vp_next(v, &t)) {
        if (t.kind == V_NUM) { *start = t.num / 1000; }
        else if (tok_is(&t, "span")) { struct vtoken n; if (vp_next(v, &n) && n.kind == V_NUM) *span = n.num / 1000; else *span = 1; }
        else if (tok_is(&t, "auto")) { }
        else if (t.kind == V_IDENT) { /* a named line: not supported */ }
    }
}

// ---- Keywords: inherit / initial / unset ----

static void copy_prop(struct style *d, const struct style *s, int prop) {
    switch (prop) {
#define CP(id, field) case P_##id: d->field = s->field; break;
    CP(DISPLAY, display) CP(POSITION, position) CP(FLOAT, floating) CP(CLEAR, clear) CP(VISIBILITY, visibility)
    CP(OVERFLOW_X, overflow_x) CP(OVERFLOW_Y, overflow_y) CP(BOX_SIZING, box_sizing) CP(TEXT_ALIGN, text_align)
    CP(WHITE_SPACE, white_space) CP(TEXT_TRANSFORM, text_transform) CP(LIST_STYLE_TYPE, list_style)
    CP(LIST_STYLE_POSITION, list_pos_inside) CP(TEXT_DECORATION_LINE, text_decoration) CP(TEXT_DECORATION_COLOR, deco_color)
    CP(WORD_BREAK, word_break) CP(TEXT_OVERFLOW, text_overflow_ellipsis) CP(FONT_STYLE, font_italic) CP(FONT_FAMILY, font_family)
    CP(FONT_WEIGHT, font_weight) CP(FONT_SIZE, font_size) CP(LETTER_SPACING, letter_spacing) CP(WORD_SPACING, word_spacing)
    CP(TEXT_INDENT, text_indent) CP(TAB_SIZE, tab_size) CP(OBJECT_FIT, object_fit)
    CP(FLEX_DIRECTION, flex_dir) CP(FLEX_WRAP, flex_wrap) CP(JUSTIFY_CONTENT, justify_content) CP(ALIGN_ITEMS, align_items)
    CP(ALIGN_SELF, align_self) CP(ALIGN_CONTENT, align_content) CP(PLACE_ITEMS_X, justify_items) CP(JUSTIFY_SELF, justify_self) CP(FLEX_GROW, flex_grow) CP(FLEX_SHRINK, flex_shrink)
    CP(FLEX_BASIS, flex_basis) CP(ORDER, order) CP(ROW_GAP, row_gap) CP(COLUMN_GAP, column_gap)
    CP(OPACITY, opacity) CP(WIDTH, width) CP(HEIGHT, height) CP(MIN_WIDTH, min_width) CP(MIN_HEIGHT, min_height)
    CP(MAX_WIDTH, max_width) CP(MAX_HEIGHT, max_height) CP(COLOR, color) CP(BACKGROUND_COLOR, bg_color)
    CP(BORDER_COLLAPSE, border_collapse) CP(TABLE_LAYOUT, table_layout_fixed) CP(POINTER_EVENTS, pointer_events_none)
    CP(USER_SELECT, user_select_none) CP(BORDER_SPACING, spacing_h)
    CP(MARGIN_TOP, margin[0]) CP(MARGIN_RIGHT, margin[1]) CP(MARGIN_BOTTOM, margin[2]) CP(MARGIN_LEFT, margin[3])
    CP(PADDING_TOP, padding[0]) CP(PADDING_RIGHT, padding[1]) CP(PADDING_BOTTOM, padding[2]) CP(PADDING_LEFT, padding[3])
    CP(TOP, inset[0]) CP(RIGHT, inset[1]) CP(BOTTOM, inset[2]) CP(LEFT, inset[3])
    CP(BORDER_TOP_WIDTH, border_width[0]) CP(BORDER_RIGHT_WIDTH, border_width[1]) CP(BORDER_BOTTOM_WIDTH, border_width[2]) CP(BORDER_LEFT_WIDTH, border_width[3])
    CP(BORDER_TOP_STYLE, border_style[0]) CP(BORDER_RIGHT_STYLE, border_style[1]) CP(BORDER_BOTTOM_STYLE, border_style[2]) CP(BORDER_LEFT_STYLE, border_style[3])
    CP(BORDER_TOP_COLOR, border_color[0]) CP(BORDER_RIGHT_COLOR, border_color[1]) CP(BORDER_BOTTOM_COLOR, border_color[2]) CP(BORDER_LEFT_COLOR, border_color[3])
    CP(BORDER_TL_RADIUS, radius[0]) CP(BORDER_TR_RADIUS, radius[1]) CP(BORDER_BR_RADIUS, radius[2]) CP(BORDER_BL_RADIUS, radius[3])
    CP(FILL_OPACITY, fill_opacity) CP(STROKE_OPACITY, stroke_opacity) CP(STROKE_LINECAP, svg_linecap) CP(STROKE_LINEJOIN, svg_linejoin)
#undef CP
    case P_Z_INDEX: d->z_index = s->z_index; d->has_z = s->has_z; break;
    case P_VERTICAL_ALIGN: d->vertical_align = s->vertical_align; d->vertical_align_len = s->vertical_align_len; break;
    case P_LINE_HEIGHT: d->line_height_pct = s->line_height_pct; d->line_height_px = s->line_height_px; break;
    case P_OBJECT_POSITION: d->object_pos_x = s->object_pos_x; d->object_pos_y = s->object_pos_y; break;
    case P_BACKGROUND_IMAGE: case P_BACKGROUND_SIZE: case P_BACKGROUND_POSITION: case P_BACKGROUND_REPEAT: case P_BACKGROUND_CLIP: case P_BACKGROUND_ORIGIN:
        d->bg_count = s->bg_count; memcpy(d->bg, s->bg, sizeof(d->bg)); break;
    case P_BOX_SHADOW: d->shadow_count = s->shadow_count; memcpy(d->shadows, s->shadows, sizeof(d->shadows)); break;
    case P_ASPECT_RATIO: d->aspect_w = s->aspect_w; d->aspect_h = s->aspect_h; break;
    case P_TRANSFORM: case P_TRANSLATE: d->translate_x = s->translate_x; d->translate_y = s->translate_y; d->has_transform = s->has_transform; break;
    case P_CONTENT: d->content = s->content; d->content_len = s->content_len; d->content_kind = s->content_kind; break;
    case P_FILL: d->svg_fill = s->svg_fill; d->svg_fill_set = s->svg_fill_set; d->svg_fill_ref = s->svg_fill_ref; break;
    case P_STROKE: d->svg_stroke = s->svg_stroke; d->svg_stroke_set = s->svg_stroke_set; d->svg_stroke_ref = s->svg_stroke_ref; break;
    case P_STROKE_WIDTH: d->svg_stroke_width = s->svg_stroke_width; break;
    case P_GRID_TEMPLATE_COLUMNS: d->col_track_count = s->col_track_count; memcpy(d->col_tracks, s->col_tracks, sizeof(d->col_tracks)); break;
    case P_GRID_TEMPLATE_ROWS: d->row_track_count = s->row_track_count; memcpy(d->row_tracks, s->row_tracks, sizeof(d->row_tracks)); break;
    case P_GRID_TEMPLATE_AREAS: d->grid_areas = s->grid_areas; break;
    case P_GRID_AUTO_COLUMNS: d->auto_col = s->auto_col; break;
    case P_GRID_AUTO_ROWS: d->auto_row = s->auto_row; break;
    case P_GRID_AUTO_FLOW: d->grid_auto_flow_column = s->grid_auto_flow_column; d->grid_dense = s->grid_dense; break;
    case P_GRID_COLUMN_START: d->col_start = s->col_start; break;
    case P_GRID_COLUMN_END: d->col_end = s->col_end; d->col_span = s->col_span; break;
    case P_GRID_ROW_START: d->row_start = s->row_start; break;
    case P_GRID_ROW_END: d->row_end = s->row_end; d->row_span = s->row_span; break;
    case P_OVERFLOW_WRAP: break;
    default: break;
    }
}

static bool is_inherited_prop(int prop) {
    switch (prop) {
    case P_VISIBILITY: case P_TEXT_ALIGN: case P_WHITE_SPACE: case P_TEXT_TRANSFORM: case P_LIST_STYLE_TYPE: case P_LIST_STYLE_POSITION:
    case P_WORD_BREAK: case P_OVERFLOW_WRAP: case P_FONT_STYLE: case P_FONT_FAMILY: case P_FONT_WEIGHT: case P_FONT_SIZE: case P_LINE_HEIGHT:
    case P_LETTER_SPACING: case P_WORD_SPACING: case P_TEXT_INDENT: case P_TAB_SIZE: case P_COLOR: case P_BORDER_COLLAPSE: case P_BORDER_SPACING:
    case P_POINTER_EVENTS: case P_USER_SELECT: case P_FILL: case P_STROKE: case P_STROKE_WIDTH: case P_FILL_OPACITY: case P_STROKE_OPACITY:
    case P_STROKE_LINECAP: case P_STROKE_LINEJOIN: case P_TEXT_DECORATION_LINE:
        return true;
    default: return false;
    }
}

// The longhands a shorthand covers (for `inherit` and friends).
static int shorthand_members(int prop, int *out) {
    int n = 0;
    #define ADD(p) out[n++] = (p)
    switch (prop) {
    case P_MARGIN: ADD(P_MARGIN_TOP); ADD(P_MARGIN_RIGHT); ADD(P_MARGIN_BOTTOM); ADD(P_MARGIN_LEFT); break;
    case P_PADDING: ADD(P_PADDING_TOP); ADD(P_PADDING_RIGHT); ADD(P_PADDING_BOTTOM); ADD(P_PADDING_LEFT); break;
    case P_INSET: ADD(P_TOP); ADD(P_RIGHT); ADD(P_BOTTOM); ADD(P_LEFT); break;
    case P_BORDER: for (int i = 0; i < 4; i++) { ADD(P_BORDER_TOP_WIDTH + i); ADD(P_BORDER_TOP_STYLE + i); ADD(P_BORDER_TOP_COLOR + i); } break;
    case P_BORDER_TOP: ADD(P_BORDER_TOP_WIDTH); ADD(P_BORDER_TOP_STYLE); ADD(P_BORDER_TOP_COLOR); break;
    case P_BORDER_RIGHT: ADD(P_BORDER_RIGHT_WIDTH); ADD(P_BORDER_RIGHT_STYLE); ADD(P_BORDER_RIGHT_COLOR); break;
    case P_BORDER_BOTTOM: ADD(P_BORDER_BOTTOM_WIDTH); ADD(P_BORDER_BOTTOM_STYLE); ADD(P_BORDER_BOTTOM_COLOR); break;
    case P_BORDER_LEFT: ADD(P_BORDER_LEFT_WIDTH); ADD(P_BORDER_LEFT_STYLE); ADD(P_BORDER_LEFT_COLOR); break;
    case P_BORDER_WIDTH: for (int i = 0; i < 4; i++) ADD(P_BORDER_TOP_WIDTH + i); break;
    case P_BORDER_STYLE: for (int i = 0; i < 4; i++) ADD(P_BORDER_TOP_STYLE + i); break;
    case P_BORDER_COLOR: for (int i = 0; i < 4; i++) ADD(P_BORDER_TOP_COLOR + i); break;
    case P_BORDER_RADIUS: for (int i = 0; i < 4; i++) ADD(P_BORDER_TL_RADIUS + i); break;
    case P_BACKGROUND: ADD(P_BACKGROUND_COLOR); ADD(P_BACKGROUND_IMAGE); break;
    case P_FLEX: ADD(P_FLEX_GROW); ADD(P_FLEX_SHRINK); ADD(P_FLEX_BASIS); break;
    case P_FLEX_FLOW: ADD(P_FLEX_DIRECTION); ADD(P_FLEX_WRAP); break;
    case P_FONT: ADD(P_FONT_STYLE); ADD(P_FONT_WEIGHT); ADD(P_FONT_SIZE); ADD(P_LINE_HEIGHT); ADD(P_FONT_FAMILY); break;
    case P_TEXT_DECORATION: ADD(P_TEXT_DECORATION_LINE); ADD(P_TEXT_DECORATION_COLOR); break;
    case P_LIST_STYLE: ADD(P_LIST_STYLE_TYPE); ADD(P_LIST_STYLE_POSITION); break;
    case P_OVERFLOW: ADD(P_OVERFLOW_X); ADD(P_OVERFLOW_Y); break;
    case P_GAP: case P_GRID_GAP: ADD(P_ROW_GAP); ADD(P_COLUMN_GAP); break;
    case P_GRID_TEMPLATE: ADD(P_GRID_TEMPLATE_COLUMNS); ADD(P_GRID_TEMPLATE_ROWS); ADD(P_GRID_TEMPLATE_AREAS); break;
    case P_GRID_COLUMN: ADD(P_GRID_COLUMN_START); ADD(P_GRID_COLUMN_END); break;
    case P_GRID_ROW: ADD(P_GRID_ROW_START); ADD(P_GRID_ROW_END); break;
    case P_PLACE_ITEMS: ADD(P_ALIGN_ITEMS); ADD(P_PLACE_ITEMS_X); break;
    case P_PLACE_CONTENT: ADD(P_ALIGN_CONTENT); ADD(P_JUSTIFY_CONTENT); break;
    case P_PLACE_SELF: ADD(P_ALIGN_SELF); ADD(P_JUSTIFY_SELF); break;
    case P_WORD_WRAP: ADD(P_OVERFLOW_WRAP); break;
    default: ADD(prop); break;
    }
    #undef ADD
    return n;
}

static void keyword_apply(struct A *a, int prop, int kind) {   // kind: 0 initial, 1 inherit
    int members[16];
    int n = css_prop_is_shorthand(prop) ? shorthand_members(prop, members) : (members[0] = prop, 1);
    for (int i = 0; i < n; i++) {
        int p = members[i];
        if (a->set[p]) continue;
        const struct style *src = kind == 1 ? a->parent : a->initial;
        if (kind == 1 && !src) src = a->initial;
        copy_prop(a->s, src, p);
        a->set[p] = 1;
    }
}

// ---- The big switch ----

static bool apply_value(struct A *a, int prop, struct vp *v);

static bool set_kw(struct A *a, int prop, struct vp *v, const char *const *list, uint8_t *dst, int *index_out) {
    struct vtoken t;
    if (!vp_next(v, &t)) return false;
    int k = tok_keyword(&t, list);
    if (k < 0) return false;
    *dst = (uint8_t)k;
    if (index_out) *index_out = k;
    MARK(prop);
    return true;
}

static void apply_border_side(struct A *a, int side, struct vp *v) {
    // width, style and color in any order
    struct vtoken t;
    int width = 3; uint8_t style = BS_NONE; uint32_t color = COLOR_CURRENT;
    bool any_w = false, any_s = false, any_c = false;
    while (vp_peek(v, &t)) {
        struct vp tryv = *v;
        vp_next(&tryv, &t);
        int k = tok_keyword(&t, kw_border_style);
        if (k >= 0) { style = (uint8_t)k; any_s = true; *v = tryv; continue; }
        bool ok;
        struct vp tv2 = tryv;
        int w = border_width_value(a, &t, &tv2, &ok);
        if (ok && (t.kind == V_DIM || t.kind == V_NUM || t.kind == V_FUNC || t.kind == V_IDENT)) {
            if (!(t.kind == V_FUNC && !(w_eq(t.s, t.n, "calc") || w_eq(t.s, t.n, "min") || w_eq(t.s, t.n, "max") || w_eq(t.s, t.n, "clamp")))) {
                width = w; any_w = true; *v = tv2; continue;
            }
        }
        struct vp tv3 = *v;
        uint32_t c;
        if (css_parse_color(&tv3, &a->cx, &c)) { color = c; any_c = true; *v = tv3; continue; }
        return;
    }
    (void)any_w; (void)any_s; (void)any_c;
    if (FREE_(P_BORDER_TOP_WIDTH + side)) { a->s->border_width[side] = width; MARK(P_BORDER_TOP_WIDTH + side); }
    if (FREE_(P_BORDER_TOP_STYLE + side)) { a->s->border_style[side] = style; MARK(P_BORDER_TOP_STYLE + side); }
    if (FREE_(P_BORDER_TOP_COLOR + side)) { a->s->border_color[side] = color; MARK(P_BORDER_TOP_COLOR + side); }
}

static bool apply_value(struct A *a, int prop, struct vp *v) {
    struct style *s = a->s;
    struct vtoken t;
    switch (prop) {
    case P_DISPLAY: {
        if (!vp_next(v, &t)) return false;
        if (t.kind != V_IDENT) return false;
        int k = tok_keyword(&t, kw_display);
        if (k < 0) return false;
        static const uint8_t map[] = { D_NONE, D_BLOCK, D_INLINE, D_INLINE_BLOCK, D_FLEX, D_INLINE_FLEX, D_GRID, D_INLINE_GRID, D_LIST_ITEM,
            D_TABLE, D_INLINE_TABLE, D_TABLE_ROW_GROUP, D_TABLE_HEADER_GROUP, D_TABLE_FOOTER_GROUP, D_TABLE_ROW, D_TABLE_CELL, D_TABLE_CAPTION,
            D_TABLE_COLUMN, D_TABLE_COLUMN_GROUP, D_CONTENTS, D_BLOCK, D_INLINE, D_FLEX, D_INLINE_FLEX, D_FLEX, D_INLINE_FLEX, D_BLOCK };
        uint8_t d = map[k];
        // two-value syntax: "inline flex", "block grid", "inline-block flow-root"
        struct vtoken t2;
        if (vp_peek(v, &t2) && t2.kind == V_IDENT) {
            vp_next(v, &t2);
            bool outer_inline = k == 2;
            if (w_eq(t2.s, t2.n, "flex")) d = outer_inline ? D_INLINE_FLEX : D_FLEX;
            else if (w_eq(t2.s, t2.n, "grid")) d = outer_inline ? D_INLINE_GRID : D_GRID;
            else if (w_eq(t2.s, t2.n, "flow-root")) d = outer_inline ? D_INLINE_BLOCK : D_BLOCK;
            else if (w_eq(t2.s, t2.n, "table")) d = outer_inline ? D_INLINE_TABLE : D_TABLE;
            else if (w_eq(t2.s, t2.n, "flow")) d = outer_inline ? D_INLINE : D_BLOCK;
            else if (w_eq(t2.s, t2.n, "list-item")) d = D_LIST_ITEM;
        }
        s->display = d;
        MARK(P_DISPLAY);
        return true;
    }
    case P_POSITION: { int k; if (!set_kw(a, prop, v, kw_position, &s->position, &k)) return false; if (k == 5) s->position = POS_STICKY; return true; }
    case P_FLOAT: { int k; if (!set_kw(a, prop, v, kw_float, &s->floating, &k)) return false; if (k == 3) s->floating = FL_LEFT; if (k == 4) s->floating = FL_RIGHT; return true; }
    case P_CLEAR: { int k; if (!set_kw(a, prop, v, kw_clear, &s->clear, &k)) return false; if (k == 4) s->clear = CL_LEFT; if (k == 5) s->clear = CL_RIGHT; return true; }
    case P_VISIBILITY: return set_kw(a, prop, v, kw_visibility, &s->visibility, NULL);
    case P_OVERFLOW_X: { int k; if (!set_kw(a, prop, v, kw_overflow, &s->overflow_x, &k)) return false; if (k == 5) s->overflow_x = OV_AUTO; return true; }
    case P_OVERFLOW_Y: { int k; if (!set_kw(a, prop, v, kw_overflow, &s->overflow_y, &k)) return false; if (k == 5) s->overflow_y = OV_AUTO; return true; }
    case P_OVERFLOW: {
        struct vtoken t1, t2;
        if (!vp_next(v, &t1)) return false;
        int k1 = tok_keyword(&t1, kw_overflow), k2 = k1;
        if (k1 < 0) return false;
        if (vp_next(v, &t2)) { k2 = tok_keyword(&t2, kw_overflow); if (k2 < 0) return false; }
        if (k1 == 5) k1 = OV_AUTO; if (k2 == 5) k2 = OV_AUTO;
        if (FREE_(P_OVERFLOW_X)) { s->overflow_x = (uint8_t)k1; MARK(P_OVERFLOW_X); }
        if (FREE_(P_OVERFLOW_Y)) { s->overflow_y = (uint8_t)k2; MARK(P_OVERFLOW_Y); }
        return true;
    }
    case P_BOX_SIZING: return set_kw(a, prop, v, kw_box_sizing, &s->box_sizing, NULL);
    case P_TEXT_ALIGN: {
        int k; if (!set_kw(a, prop, v, kw_text_align, &s->text_align, &k)) return false;
        if (k == 6) s->text_align = TA_START;
        if (k == 7 || k == 10) s->text_align = TA_CENTER;
        if (k == 8) s->text_align = TA_LEFT; if (k == 9) s->text_align = TA_RIGHT;
        return true;
    }
    case P_WHITE_SPACE: return set_kw(a, prop, v, kw_white_space, &s->white_space, NULL);
    case P_TEXT_TRANSFORM: return set_kw(a, prop, v, kw_text_transform, &s->text_transform, NULL);
    case P_VERTICAL_ALIGN: {
        if (!vp_peek(v, &t)) return false;
        struct vp tryv = *v;
        vp_next(&tryv, &t);
        int k = tok_keyword(&t, kw_va);
        if (k >= 0) { s->vertical_align = (uint8_t)k; *v = tryv; MARK(prop); return true; }
        struct len l;
        if (!css_parse_len_tok(&tryv, &t, &a->cx, &l, 0)) return false;
        s->vertical_align = VA_LEN; s->vertical_align_len = l; *v = tryv; MARK(prop);
        return true;
    }
    case P_LIST_STYLE_TYPE: {
        if (!vp_next(v, &t)) return false;
        if (t.kind == V_STR) { s->list_style = LS_DISC; MARK(prop); return true; }
        int k = tok_keyword(&t, kw_list_type);
        if (k < 0) { if (t.kind == V_IDENT) { s->list_style = LS_DISC; MARK(prop); return true; } return false; }
        if (k == 11) k = LS_LOWER_ALPHA; else if (k == 12) k = LS_UPPER_ALPHA;
        s->list_style = (uint8_t)k; MARK(prop);
        return true;
    }
    case P_LIST_STYLE_POSITION: {
        if (!vp_next(v, &t)) return false;
        if (tok_is(&t, "inside")) s->list_pos_inside = 1; else if (tok_is(&t, "outside")) s->list_pos_inside = 0; else return false;
        MARK(prop); return true;
    }
    case P_LIST_STYLE: {
        while (vp_peek(v, &t)) {
            struct vp tryv = *v;
            vp_next(&tryv, &t);
            if (tok_is(&t, "inside") || tok_is(&t, "outside")) { if (FREE_(P_LIST_STYLE_POSITION)) { s->list_pos_inside = tok_is(&t, "inside"); MARK(P_LIST_STYLE_POSITION); } *v = tryv; continue; }
            if (t.kind == V_FUNC && w_eq(t.s, t.n, "url")) { struct vp skip; vp_func_args(&tryv, &skip); *v = tryv; continue; }
            struct vp v2 = *v;
            if (FREE_(P_LIST_STYLE_TYPE) && apply_value(a, P_LIST_STYLE_TYPE, &v2)) { *v = v2; continue; }
            *v = tryv;
        }
        if (FREE_(P_LIST_STYLE_TYPE)) { /* nothing given: initial */ s->list_style = LS_DISC; MARK(P_LIST_STYLE_TYPE); }
        if (FREE_(P_LIST_STYLE_POSITION)) { s->list_pos_inside = 0; MARK(P_LIST_STYLE_POSITION); }
        return true;
    }
    case P_TEXT_DECORATION_LINE: {
        uint8_t lines = 0;
        bool any = false;
        while (vp_next(v, &t)) {
            if (tok_is(&t, "none")) { lines = 0; any = true; }
            else if (tok_is(&t, "underline")) { lines |= DECO_UNDERLINE; any = true; }
            else if (tok_is(&t, "overline")) { lines |= DECO_OVERLINE; any = true; }
            else if (tok_is(&t, "line-through")) { lines |= DECO_LINETHROUGH; any = true; }
            else if (tok_is(&t, "blink") || tok_is(&t, "spelling-error") || tok_is(&t, "grammar-error")) any = true;
            else return false;
        }
        if (!any) return false;
        s->text_decoration = lines; MARK(prop);
        return true;
    }
    case P_TEXT_DECORATION_COLOR: {
        uint32_t c; if (!css_parse_color(v, &a->cx, &c)) return false;
        s->deco_color = c; MARK(prop); return true;
    }
    case P_TEXT_DECORATION: {
        uint8_t lines = 0; uint32_t color = COLOR_CURRENT;
        bool any = false;
        while (vp_peek(v, &t)) {
            struct vp tryv = *v;
            vp_next(&tryv, &t);
            if (tok_is(&t, "none")) { lines = 0; any = true; *v = tryv; continue; }
            if (tok_is(&t, "underline")) { lines |= DECO_UNDERLINE; any = true; *v = tryv; continue; }
            if (tok_is(&t, "overline")) { lines |= DECO_OVERLINE; any = true; *v = tryv; continue; }
            if (tok_is(&t, "line-through")) { lines |= DECO_LINETHROUGH; any = true; *v = tryv; continue; }
            if (tok_is(&t, "solid") || tok_is(&t, "double") || tok_is(&t, "dotted") || tok_is(&t, "dashed") || tok_is(&t, "wavy") || tok_is(&t, "from-font") || tok_is(&t, "auto") || tok_is(&t, "blink")) { *v = tryv; any = true; continue; }
            if (t.kind == V_DIM || t.kind == V_PCT) { *v = tryv; continue; }
            uint32_t c;
            struct vp t3 = *v;
            if (css_parse_color(&t3, &a->cx, &c)) { color = c; *v = t3; continue; }
            return false;
        }
        if (!any && color == COLOR_CURRENT) return false;
        if (FREE_(P_TEXT_DECORATION_LINE)) { s->text_decoration = lines; MARK(P_TEXT_DECORATION_LINE); }
        if (FREE_(P_TEXT_DECORATION_COLOR)) { s->deco_color = color; MARK(P_TEXT_DECORATION_COLOR); }
        return true;
    }
    case P_WORD_BREAK: { int k; if (!set_kw(a, prop, v, kw_word_break, &s->word_break, &k)) return false; return true; }
    case P_OVERFLOW_WRAP: case P_WORD_WRAP: {
        if (!vp_next(v, &t)) return false;
        if (tok_is(&t, "break-word") || tok_is(&t, "anywhere")) { if (s->word_break == WB_NORMAL) s->word_break = WB_BREAK_WORD; }
        else if (!tok_is(&t, "normal")) return false;
        MARK(P_OVERFLOW_WRAP);
        return true;
    }
    case P_TEXT_OVERFLOW: {
        if (!vp_next(v, &t)) return false;
        if (tok_is(&t, "ellipsis")) s->text_overflow_ellipsis = 1; else s->text_overflow_ellipsis = 0;
        MARK(prop); return true;
    }
    case P_FONT_STYLE: {
        if (!vp_next(v, &t)) return false;
        if (tok_is(&t, "italic") || tok_is(&t, "oblique")) s->font_italic = 1; else if (tok_is(&t, "normal")) s->font_italic = 0; else return false;
        MARK(prop); return true;
    }
    case P_FONT_FAMILY: { uint8_t f = s->font_family; if (!parse_family(v, &f)) return false; s->font_family = f; MARK(prop); return true; }
    case P_FONT_WEIGHT: {
        if (!vp_next(v, &t)) return false;
        int w;
        if (t.kind == V_NUM) w = t.num / 1000;
        else if (tok_is(&t, "normal")) w = 400;
        else if (tok_is(&t, "bold")) w = 700;
        else if (tok_is(&t, "bolder")) { int p = a->parent ? a->parent->font_weight : 400; w = p < 400 ? 400 : (p < 600 ? 700 : 900); }
        else if (tok_is(&t, "lighter")) { int p = a->parent ? a->parent->font_weight : 400; w = p > 700 ? 700 : (p > 500 ? 400 : 100); }
        else if (t.kind == V_FUNC) { struct vp args; vp_func_args(v, &args); w = 400; struct vtoken x; if (vp_next(&args, &x) && x.kind == V_NUM) w = x.num / 1000; }
        else return false;
        if (w < 1) w = 1; if (w > 1000) w = 1000;
        s->font_weight = (int16_t)w; MARK(prop);
        return true;
    }
    case P_FONT_SIZE: {
        if (!vp_next(v, &t)) return false;
        int parent_px = a->parent ? a->parent->font_size : 16;
        int px;
        if (t.kind == V_IDENT) {
            static const char *const names[] = { "xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large", "larger", "smaller", "math", NULL };
            static const int sizes[] = { 9, 10, 13, 16, 18, 24, 32, 48, 0, 0, 16 };
            int k = tok_keyword(&t, names);
            if (k < 0) return false;
            if (k == 8) px = (parent_px * 12 + 5) / 10; else if (k == 9) px = (parent_px * 10 + 6) / 12; else px = sizes[k];
        } else {
            struct css_ctx saved = a->cx;
            a->cx.font_px = parent_px;
            struct len l;
            bool ok = css_parse_len_tok(v, &t, &a->cx, &l, LF_NONNEG);
            a->cx.font_px = saved.font_px;
            if (!ok) return false;
            px = l.px + (int)((long long)parent_px * l.pct / 10000);
            if (l.expr) px = len_resolve(&l, parent_px);
        }
        if (px < 0) px = 0;
        s->font_size = px;
        a->cx.font_px = px;
        MARK(prop);
        return true;
    }
    case P_LINE_HEIGHT: {
        if (!vp_next(v, &t)) return false;
        if (tok_is(&t, "normal")) { s->line_height_pct = 0; s->line_height_px = 0; MARK(prop); return true; }
        if (t.kind == V_NUM) { s->line_height_pct = t.num / 10; s->line_height_px = 0; if (s->line_height_pct <= 0) s->line_height_pct = 1; MARK(prop); return true; }
        if (t.kind == V_PCT) { s->line_height_pct = t.num / 1000 * 1 + (t.num % 1000) / 10 * 0; s->line_height_pct = t.num / 1000; s->line_height_px = 0; if (s->line_height_pct <= 0) s->line_height_pct = 1; MARK(prop); return true; }
        struct len l;
        if (!css_parse_len_tok(v, &t, &a->cx, &l, LF_NONNEG)) {
            return false;
        }
        int px = l.px;
        if (l.pct || l.expr) px = len_resolve(&l, s->font_size);
        s->line_height_pct = 0; s->line_height_px = px < 1 ? 1 : px; MARK(prop);
        return true;
    }
    case P_LETTER_SPACING: case P_WORD_SPACING: {
        if (!vp_next(v, &t)) return false;
        int val = 0;
        if (!tok_is(&t, "normal")) {
            struct len l;
            if (!css_parse_len_tok(v, &t, &a->cx, &l, LF_NO_PCT)) return false;
            // lengths were rounded to pixels: redo from the raw number for em units so 0.05em isn't lost
            val = l.px * 64;
            if (t.kind == V_DIM && w_eq(t.unit, t.unit_len, "em")) val = (int)((long long)t.num * a->cx.font_px * 64 / 1000);
            else if (t.kind == V_DIM && w_eq(t.unit, t.unit_len, "px")) val = (int)((long long)t.num * 64 / 1000);
        }
        if (prop == P_LETTER_SPACING) s->letter_spacing = val; else s->word_spacing = val;
        MARK(prop);
        return true;
    }
    case P_TEXT_INDENT: { struct len l; if (!read_len(a, v, &l, 0)) return false; s->text_indent = l; MARK(prop); return true; }
    case P_TAB_SIZE: { int n; if (!css_parse_number(v, &a->cx, &n)) return false; s->tab_size = n / 1000; if (s->tab_size < 1) s->tab_size = 1; MARK(prop); return true; }
    case P_OBJECT_FIT: return set_kw(a, prop, v, kw_object_fit, &s->object_fit, NULL);
    case P_OBJECT_POSITION: {
        struct len x, y;
        if (!parse_bg_position(a, v, &x, &y)) return false;
        s->object_pos_x = x; s->object_pos_y = y; MARK(prop); return true;
    }
    case P_FLEX_DIRECTION: return set_kw(a, prop, v, kw_flex_dir, &s->flex_dir, NULL);
    case P_FLEX_WRAP: return set_kw(a, prop, v, kw_flex_wrap, &s->flex_wrap, NULL);
    case P_JUSTIFY_CONTENT: case P_ALIGN_ITEMS: case P_ALIGN_SELF: case P_ALIGN_CONTENT: case P_JUSTIFY_SELF: case P_PLACE_ITEMS_X: {
        // skip "safe"/"unsafe" overflow words
        for (;;) {
            if (!vp_next(v, &t)) return false;
            if (!(tok_is(&t, "safe") || tok_is(&t, "unsafe"))) break;
        }
        int k = tok_keyword(&t, kw_align);
        if (k < 0) return false;
        if (k == 16 || k == 17) { struct vtoken skip; vp_peek(v, &skip); }
        uint8_t m = map_align(k);
        if (prop == P_JUSTIFY_CONTENT) { s->justify_content = m; if (k == 14) s->justify_content = FA_START; if (k == 15) s->justify_content = FA_END; }
        else if (prop == P_ALIGN_ITEMS) s->align_items = m;
        else if (prop == P_ALIGN_SELF) s->align_self = m;
        else if (prop == P_ALIGN_CONTENT) s->align_content = m;
        else if (prop == P_PLACE_ITEMS_X) s->justify_items = m;
        else if (prop == P_JUSTIFY_SELF) s->justify_self = m;
        else return true;
        MARK(prop);
        return true;
    }
    case P_PLACE_ITEMS: {
        struct vp c = *v;
        if (!apply_value(a, P_ALIGN_ITEMS, &c)) return false;
        struct vtoken x;
        if (vp_peek(&c, &x)) { if (FREE_(P_PLACE_ITEMS_X)) apply_value(a, P_PLACE_ITEMS_X, &c); }
        else { struct vp c2 = *v; if (FREE_(P_PLACE_ITEMS_X)) apply_value(a, P_PLACE_ITEMS_X, &c2); }
        return true;
    }
    case P_PLACE_CONTENT: {
        struct vp c1 = *v;
        if (!apply_value(a, P_ALIGN_CONTENT, &c1)) return false;
        struct vtoken x;
        struct vp c2 = c1;
        if (vp_peek(&c2, &x)) { if (FREE_(P_JUSTIFY_CONTENT)) apply_value(a, P_JUSTIFY_CONTENT, &c2); }
        else { struct vp c3 = *v; if (FREE_(P_JUSTIFY_CONTENT)) apply_value(a, P_JUSTIFY_CONTENT, &c3); }
        return true;
    }
    case P_PLACE_SELF: {
        struct vp c = *v;
        if (!apply_value(a, P_ALIGN_SELF, &c)) return false;
        struct vtoken x;
        if (vp_peek(&c, &x)) { if (FREE_(P_JUSTIFY_SELF)) apply_value(a, P_JUSTIFY_SELF, &c); }
        else { struct vp c2 = *v; if (FREE_(P_JUSTIFY_SELF)) apply_value(a, P_JUSTIFY_SELF, &c2); }
        return true;
    }
    case P_FLEX_GROW: case P_FLEX_SHRINK: {
        int n; if (!css_parse_number(v, &a->cx, &n)) return false;
        if (n < 0) return false;
        if (prop == P_FLEX_GROW) s->flex_grow = n; else s->flex_shrink = n;
        MARK(prop); return true;
    }
    case P_FLEX_BASIS: {
        struct len l; if (!read_size(a, v, &l, false)) return false;
        s->flex_basis = l; MARK(prop); return true;
    }
    case P_FLEX: {
        int grow = 0, shrink = 1000;
        struct len basis; memset(&basis, 0, sizeof(basis)); basis.kind = L_AUTO;
        if (!vp_peek(v, &t)) return false;
        if (tok_is(&t, "none")) { grow = 0; shrink = 0; }
        else if (tok_is(&t, "auto")) { grow = 1000; shrink = 1000; }
        else if (tok_is(&t, "initial")) { grow = 0; shrink = 1000; }
        else {
            int numbers = 0;
            bool got_basis = false;
            while (vp_peek(v, &t)) {
                struct vp tryv = *v;
                vp_next(&tryv, &t);
                if (t.kind == V_NUM && numbers < 2 && !got_basis) {
                    if (numbers == 0) grow = t.num; else shrink = t.num;
                    numbers++; *v = tryv;
                    // a bare unitless 0 after the numbers could be the basis; ignore that corner
                    continue;
                }
                struct len l;
                struct vp t2 = *v;
                if (read_size(a, &t2, &l, false)) { basis = l; got_basis = true; *v = t2; continue; }
                return false;
            }
            if (numbers > 0 && !got_basis) { basis.kind = L_VAL; basis.px = 0; basis.pct = 0; }
            if (numbers == 0 && !got_basis) return false;
        }
        if (FREE_(P_FLEX_GROW)) { s->flex_grow = grow; MARK(P_FLEX_GROW); }
        if (FREE_(P_FLEX_SHRINK)) { s->flex_shrink = shrink; MARK(P_FLEX_SHRINK); }
        if (FREE_(P_FLEX_BASIS)) { s->flex_basis = basis; MARK(P_FLEX_BASIS); }
        return true;
    }
    case P_FLEX_FLOW: {
        while (vp_peek(v, &t)) {
            struct vp c = *v;
            if (FREE_(P_FLEX_DIRECTION) && apply_value(a, P_FLEX_DIRECTION, &c)) { *v = c; continue; }
            c = *v;
            if (FREE_(P_FLEX_WRAP) && apply_value(a, P_FLEX_WRAP, &c)) { *v = c; continue; }
            return false;
        }
        return true;
    }
    case P_ORDER: { int n; if (!css_parse_number(v, &a->cx, &n)) return false; s->order = n / 1000; MARK(prop); return true; }
    case P_ROW_GAP: case P_COLUMN_GAP: case P_GRID_ROW_GAP: case P_GRID_COLUMN_GAP: {
        int dst = (prop == P_ROW_GAP || prop == P_GRID_ROW_GAP) ? P_ROW_GAP : P_COLUMN_GAP;
        if (!vp_peek(v, &t)) return false;
        struct len l;
        if (tok_is(&t, "normal")) { vp_next(v, &t); memset(&l, 0, sizeof(l)); l.kind = L_VAL; }
        else if (!read_len(a, v, &l, LF_NONNEG)) return false;
        if (dst == P_ROW_GAP) s->row_gap = l; else s->column_gap = l;
        MARK(dst); return true;
    }
    case P_GAP: case P_GRID_GAP: {
        struct len l1, l2;
        struct vtoken x;
        if (!vp_peek(v, &x)) return false;
        if (tok_is(&x, "normal")) { vp_next(v, &x); memset(&l1, 0, sizeof(l1)); l1.kind = L_VAL; }
        else if (!read_len(a, v, &l1, LF_NONNEG)) return false;
        l2 = l1;
        if (vp_peek(v, &x)) {
            if (tok_is(&x, "normal")) { vp_next(v, &x); memset(&l2, 0, sizeof(l2)); l2.kind = L_VAL; }
            else if (!read_len(a, v, &l2, LF_NONNEG)) return false;
        }
        if (FREE_(P_ROW_GAP)) { s->row_gap = l1; MARK(P_ROW_GAP); }
        if (FREE_(P_COLUMN_GAP)) { s->column_gap = l2; MARK(P_COLUMN_GAP); }
        return true;
    }
    case P_GRID_TEMPLATE_COLUMNS: parse_track_list(a, v, s->col_tracks, &s->col_track_count); MARK(prop); return true;
    case P_GRID_TEMPLATE_ROWS: parse_track_list(a, v, s->row_tracks, &s->row_track_count); MARK(prop); return true;
    case P_GRID_TEMPLATE_AREAS: {
        // strings, one per row
        char *buf = w_alloc(&a->cx.page->larena, (size_t)v->len + 2);
        int n = 0;
        while (vp_next(v, &t)) {
            if (t.kind == V_STR) { for (int i = 0; i < t.n; i++) buf[n++] = t.s[i]; buf[n++] = '\n'; }
            else if (!tok_is(&t, "none")) return false;
        }
        buf[n] = '\0';
        s->grid_areas = n ? buf : NULL;
        MARK(prop); return true;
    }
    case P_GRID_TEMPLATE: {
        // rows / columns (only the simplest form): tracks "/" tracks
        struct vp rows = *v;
        int slash = -1, dp = 0;
        for (int i = 0; i < v->len; i++) { if (v->s[i] == '(') dp++; else if (v->s[i] == ')') dp--; else if (v->s[i] == '/' && dp == 0) { slash = i; break; } }
        if (slash >= 0) {
            struct vp r, c;
            vp_init(&r, v->s, slash); vp_init(&c, v->s + slash + 1, v->len - slash - 1);
            if (FREE_(P_GRID_TEMPLATE_ROWS)) { parse_track_list(a, &r, s->row_tracks, &s->row_track_count); MARK(P_GRID_TEMPLATE_ROWS); }
            if (FREE_(P_GRID_TEMPLATE_COLUMNS)) { parse_track_list(a, &c, s->col_tracks, &s->col_track_count); MARK(P_GRID_TEMPLATE_COLUMNS); }
        }
        (void)rows;
        return true;
    }
    case P_GRID: return true;
    case P_GRID_AUTO_COLUMNS: case P_GRID_AUTO_ROWS: {
        struct track tr;
        if (!vp_next(v, &t)) return false;
        if (!parse_track_one(a, v, &t, &tr)) return false;
        if (prop == P_GRID_AUTO_COLUMNS) s->auto_col = tr; else s->auto_row = tr;
        MARK(prop); return true;
    }
    case P_GRID_AUTO_FLOW: {
        s->grid_auto_flow_column = 0; s->grid_dense = 0;
        while (vp_next(v, &t)) {
            if (tok_is(&t, "column")) s->grid_auto_flow_column = 1;
            else if (tok_is(&t, "dense")) s->grid_dense = 1;
        }
        MARK(prop); return true;
    }
    case P_GRID_COLUMN_START: { int span; parse_grid_line(v, &s->col_start, &span); MARK(prop); if (span) s->col_span = span; return true; }
    case P_GRID_COLUMN_END: { int e, span; parse_grid_line(v, &e, &span); s->col_end = e; if (span) s->col_span = span; MARK(prop); return true; }
    case P_GRID_ROW_START: { int span; parse_grid_line(v, &s->row_start, &span); MARK(prop); if (span) s->row_span = span; return true; }
    case P_GRID_ROW_END: { int e, span; parse_grid_line(v, &e, &span); s->row_end = e; if (span) s->row_span = span; MARK(prop); return true; }
    case P_GRID_COLUMN: case P_GRID_ROW: {
        int slash = -1;
        for (int i = 0; i < v->len; i++) if (v->s[i] == '/') { slash = i; break; }
        struct vp first, second;
        if (slash >= 0) { vp_init(&first, v->s, slash); vp_init(&second, v->s + slash + 1, v->len - slash - 1); }
        else { vp_init(&first, v->s, v->len); vp_init(&second, v->s + v->len, 0); }
        int st, sp1, en = 0, sp2;
        parse_grid_line(&first, &st, &sp1);
        parse_grid_line(&second, &en, &sp2);
        int span = 0;
        if (sp1) span = sp1;
        if (sp2) span = sp2;
        if (prop == P_GRID_COLUMN) {
            if (FREE_(P_GRID_COLUMN_START)) { s->col_start = st; MARK(P_GRID_COLUMN_START); }
            if (FREE_(P_GRID_COLUMN_END)) { s->col_end = en; s->col_span = span; MARK(P_GRID_COLUMN_END); }
        } else {
            if (FREE_(P_GRID_ROW_START)) { s->row_start = st; MARK(P_GRID_ROW_START); }
            if (FREE_(P_GRID_ROW_END)) { s->row_end = en; s->row_span = span; MARK(P_GRID_ROW_END); }
        }
        return true;
    }
    case P_GRID_AREA: {
        if (!vp_peek(v, &t)) return false;
        if (t.kind == V_IDENT && !tok_is(&t, "auto") && !tok_is(&t, "span")) {
            vp_next(v, &t);
            s->grid_area = copy_str(a, t.s, t.n);
            MARK(prop); return true;
        }
        // row-start / column-start / row-end / column-end
        int parts[4] = { 0, 0, 0, 0 }, np = 0, start = 0, dp = 0;
        for (int i = 0; i <= v->len; i++) {
            if (i < v->len && v->s[i] == '(') dp++;
            else if (i < v->len && v->s[i] == ')') dp--;
            if (i == v->len || (v->s[i] == '/' && dp == 0)) {
                struct vp part; vp_init(&part, v->s + start, i - start);
                int st, sp;
                parse_grid_line(&part, &st, &sp);
                if (np < 4) parts[np++] = st;
                start = i + 1;
            }
        }
        s->row_start = parts[0]; s->col_start = np > 1 ? parts[1] : 0; s->row_end = np > 2 ? parts[2] : 0; s->col_end = np > 3 ? parts[3] : 0;
        MARK(prop);
        return true;
    }
    case P_OPACITY: {
        int n; if (!css_parse_number(v, &a->cx, &n)) return false;
        int o = (n * 255 + 500) / 1000;
        s->opacity = (uint8_t)w_clampi(o, 0, 255); MARK(prop); return true;
    }
    case P_Z_INDEX: {
        if (!vp_next(v, &t)) return false;
        if (tok_is(&t, "auto")) { s->has_z = false; s->z_index = 0; }
        else if (t.kind == V_NUM) { s->has_z = true; s->z_index = t.num / 1000; }
        else return false;
        MARK(prop); return true;
    }
    case P_WIDTH: { struct len l; if (!read_size(a, v, &l, false)) return false; s->width = l; MARK(prop); return true; }
    case P_HEIGHT: { struct len l; if (!read_size(a, v, &l, false)) return false; s->height = l; MARK(prop); return true; }
    case P_MIN_WIDTH: { struct len l; if (!read_size(a, v, &l, false)) return false; s->min_width = l; MARK(prop); return true; }
    case P_MIN_HEIGHT: { struct len l; if (!read_size(a, v, &l, false)) return false; s->min_height = l; MARK(prop); return true; }
    case P_MAX_WIDTH: { struct len l; if (!read_size(a, v, &l, true)) return false; s->max_width = l; MARK(prop); return true; }
    case P_MAX_HEIGHT: { struct len l; if (!read_size(a, v, &l, true)) return false; s->max_height = l; MARK(prop); return true; }
    case P_WIDTH_INLINE: { struct vp c = *v; return apply_value(a, P_WIDTH, &c); }
    case P_HEIGHT_BLOCK: { struct vp c = *v; return apply_value(a, P_HEIGHT, &c); }
    case P_MIN_INLINE_SIZE: { struct vp c = *v; return apply_value(a, P_MIN_WIDTH, &c); }
    case P_MIN_BLOCK_SIZE: { struct vp c = *v; return apply_value(a, P_MIN_HEIGHT, &c); }
    case P_MAX_INLINE_SIZE: { struct vp c = *v; return apply_value(a, P_MAX_WIDTH, &c); }
    case P_MAX_BLOCK_SIZE: { struct vp c = *v; return apply_value(a, P_MAX_HEIGHT, &c); }
    case P_MARGIN_TOP: case P_MARGIN_RIGHT: case P_MARGIN_BOTTOM: case P_MARGIN_LEFT: {
        struct vtoken x; if (!vp_next(v, &x)) return false;
        struct len l; memset(&l, 0, sizeof(l));
        if (tok_is(&x, "auto")) l.kind = L_AUTO; else if (!css_parse_len_tok(v, &x, &a->cx, &l, 0)) return false;
        s->margin[prop - P_MARGIN_TOP] = l; MARK(prop); return true;
    }
    case P_PADDING_TOP: case P_PADDING_RIGHT: case P_PADDING_BOTTOM: case P_PADDING_LEFT: {
        struct len l; if (!read_len(a, v, &l, LF_NONNEG)) return false;
        s->padding[prop - P_PADDING_TOP] = l; MARK(prop); return true;
    }
    case P_TOP: case P_RIGHT: case P_BOTTOM: case P_LEFT: {
        struct len l; if (!read_inset(a, v, &l)) return false;
        s->inset[prop - P_TOP] = l; MARK(prop); return true;
    }
    case P_MARGIN: { struct len *d[4] = { &s->margin[0], &s->margin[1], &s->margin[2], &s->margin[3] }; set_len4(a, P_MARGIN_TOP, d, v, 0, true); return true; }
    case P_PADDING: { struct len *d[4] = { &s->padding[0], &s->padding[1], &s->padding[2], &s->padding[3] }; set_len4(a, P_PADDING_TOP, d, v, LF_NONNEG, false); return true; }
    case P_INSET: {
        struct len vals[4]; int n = 0;
        while (n < 4 && vp_peek(v, &t)) { if (!read_inset(a, v, &vals[n])) return false; n++; }
        if (n == 0) return false;
        int map[4];
        if (n == 1) map[0] = map[1] = map[2] = map[3] = 0;
        else if (n == 2) { map[0] = 0; map[1] = 1; map[2] = 0; map[3] = 1; }
        else if (n == 3) { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 1; }
        else { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 3; }
        for (int i = 0; i < 4; i++) if (FREE_(P_TOP + i)) { s->inset[i] = vals[map[i]]; MARK(P_TOP + i); }
        return true;
    }
    case P_MARGIN_INLINE: case P_MARGIN_BLOCK: case P_PADDING_INLINE: case P_PADDING_BLOCK: case P_INSET_INLINE: case P_INSET_BLOCK: {
        bool margin = prop == P_MARGIN_INLINE || prop == P_MARGIN_BLOCK, padding = prop == P_PADDING_INLINE || prop == P_PADDING_BLOCK;
        bool inl = prop == P_MARGIN_INLINE || prop == P_PADDING_INLINE || prop == P_INSET_INLINE;
        struct len vals[2]; int n = 0;
        while (n < 2 && vp_peek(v, &t)) {
            if (margin || !padding) { if (!read_inset(a, v, &vals[n])) return false; }
            else if (!read_len(a, v, &vals[n], LF_NONNEG)) return false;
            n++;
        }
        if (n == 0) return false;
        if (n == 1) vals[1] = vals[0];
        int base = margin ? P_MARGIN_TOP : (padding ? P_PADDING_TOP : P_TOP);
        struct len *arr = margin ? s->margin : (padding ? s->padding : s->inset);
        int i0 = inl ? 3 : 0, i1 = inl ? 1 : 2;
        if (FREE_(base + i0)) { arr[i0] = vals[0]; MARK(base + i0); }
        if (FREE_(base + i1)) { arr[i1] = vals[1]; MARK(base + i1); }
        return true;
    }
    case P_MARGIN_INLINE_START: case P_MARGIN_INLINE_END: case P_MARGIN_BLOCK_START: case P_MARGIN_BLOCK_END: {
        int to = prop == P_MARGIN_INLINE_START ? P_MARGIN_LEFT : prop == P_MARGIN_INLINE_END ? P_MARGIN_RIGHT : prop == P_MARGIN_BLOCK_START ? P_MARGIN_TOP : P_MARGIN_BOTTOM;
        struct vp c = *v; if (!FREE_(to)) return true; return apply_value(a, to, &c);
    }
    case P_PADDING_INLINE_START: case P_PADDING_INLINE_END: case P_PADDING_BLOCK_START: case P_PADDING_BLOCK_END: {
        int to = prop == P_PADDING_INLINE_START ? P_PADDING_LEFT : prop == P_PADDING_INLINE_END ? P_PADDING_RIGHT : prop == P_PADDING_BLOCK_START ? P_PADDING_TOP : P_PADDING_BOTTOM;
        struct vp c = *v; if (!FREE_(to)) return true; return apply_value(a, to, &c);
    }
    case P_INSET_INLINE_START: case P_INSET_INLINE_END: case P_INSET_BLOCK_START: case P_INSET_BLOCK_END: {
        int to = prop == P_INSET_INLINE_START ? P_LEFT : prop == P_INSET_INLINE_END ? P_RIGHT : prop == P_INSET_BLOCK_START ? P_TOP : P_BOTTOM;
        struct vp c = *v; if (!FREE_(to)) return true; return apply_value(a, to, &c);
    }
    case P_BORDER_TOP_WIDTH: case P_BORDER_RIGHT_WIDTH: case P_BORDER_BOTTOM_WIDTH: case P_BORDER_LEFT_WIDTH: {
        if (!vp_next(v, &t)) return false;
        bool ok; int w = border_width_value(a, &t, v, &ok);
        if (!ok) return false;
        s->border_width[prop - P_BORDER_TOP_WIDTH] = w; MARK(prop); return true;
    }
    case P_BORDER_TOP_STYLE: case P_BORDER_RIGHT_STYLE: case P_BORDER_BOTTOM_STYLE: case P_BORDER_LEFT_STYLE: {
        return set_kw(a, prop, v, kw_border_style, &s->border_style[prop - P_BORDER_TOP_STYLE], NULL);
    }
    case P_BORDER_TOP_COLOR: case P_BORDER_RIGHT_COLOR: case P_BORDER_BOTTOM_COLOR: case P_BORDER_LEFT_COLOR: {
        uint32_t c;
        if (vp_peek(v, &t) && tok_is(&t, "currentcolor")) { vp_next(v, &t); c = COLOR_CURRENT; }
        else if (!css_parse_color(v, &a->cx, &c)) return false;
        s->border_color[prop - P_BORDER_TOP_COLOR] = c; MARK(prop); return true;
    }
    case P_BORDER_TL_RADIUS: case P_BORDER_TR_RADIUS: case P_BORDER_BR_RADIUS: case P_BORDER_BL_RADIUS: {
        struct len l; if (!read_len(a, v, &l, LF_NONNEG)) return false;
        s->radius[prop - P_BORDER_TL_RADIUS] = l; MARK(prop); return true;
    }
    case P_BORDER_RADIUS: {
        // horizontal radii only (the part after "/" is ignored)
        struct len vals[4]; int n = 0;
        while (n < 4 && vp_peek(v, &t) && t.kind != V_SLASH) { if (!read_len(a, v, &vals[n], LF_NONNEG)) return false; n++; }
        if (n == 0) return false;
        int map[4];
        if (n == 1) map[0] = map[1] = map[2] = map[3] = 0;
        else if (n == 2) { map[0] = 0; map[1] = 1; map[2] = 0; map[3] = 1; }
        else if (n == 3) { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 1; }
        else { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 3; }
        for (int i = 0; i < 4; i++) if (FREE_(P_BORDER_TL_RADIUS + i)) { s->radius[i] = vals[map[i]]; MARK(P_BORDER_TL_RADIUS + i); }
        return true;
    }
    case P_BORDER: case P_BORDER_TOP: case P_BORDER_RIGHT: case P_BORDER_BOTTOM: case P_BORDER_LEFT: case P_BORDER_INLINE: case P_BORDER_BLOCK: {
        if (prop == P_BORDER_INLINE) { struct vp c1 = *v, c2 = *v; apply_border_side(a, 3, &c1); apply_border_side(a, 1, &c2); return true; }
        if (prop == P_BORDER_BLOCK) { struct vp c1 = *v, c2 = *v; apply_border_side(a, 0, &c1); apply_border_side(a, 2, &c2); return true; }
        if (!vp_peek(v, &t)) return false;
        if (prop == P_BORDER) { for (int side = 0; side < 4; side++) { struct vp c = *v; apply_border_side(a, side, &c); } }
        else { apply_border_side(a, prop - P_BORDER_TOP, v); }
        return true;
    }
    case P_BORDER_WIDTH: {
        int w[4]; int n = 0;
        while (n < 4 && vp_next(v, &t)) { bool ok; w[n] = border_width_value(a, &t, v, &ok); if (!ok) return false; n++; }
        if (n == 0) return false;
        int map[4];
        if (n == 1) map[0] = map[1] = map[2] = map[3] = 0; else if (n == 2) { map[0] = 0; map[1] = 1; map[2] = 0; map[3] = 1; }
        else if (n == 3) { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 1; } else { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 3; }
        for (int i = 0; i < 4; i++) if (FREE_(P_BORDER_TOP_WIDTH + i)) { s->border_width[i] = w[map[i]]; MARK(P_BORDER_TOP_WIDTH + i); }
        return true;
    }
    case P_BORDER_STYLE: {
        int st[4]; int n = 0;
        while (n < 4 && vp_next(v, &t)) { st[n] = tok_keyword(&t, kw_border_style); if (st[n] < 0) return false; n++; }
        if (n == 0) return false;
        int map[4];
        if (n == 1) map[0] = map[1] = map[2] = map[3] = 0; else if (n == 2) { map[0] = 0; map[1] = 1; map[2] = 0; map[3] = 1; }
        else if (n == 3) { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 1; } else { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 3; }
        for (int i = 0; i < 4; i++) if (FREE_(P_BORDER_TOP_STYLE + i)) { s->border_style[i] = (uint8_t)st[map[i]]; MARK(P_BORDER_TOP_STYLE + i); }
        return true;
    }
    case P_BORDER_COLOR: {
        uint32_t c[4]; int n = 0;
        while (n < 4 && vp_peek(v, &t)) {
            if (tok_is(&t, "currentcolor")) { vp_next(v, &t); c[n] = COLOR_CURRENT; }
            else if (!css_parse_color(v, &a->cx, &c[n])) return false;
            n++;
        }
        if (n == 0) return false;
        int map[4];
        if (n == 1) map[0] = map[1] = map[2] = map[3] = 0; else if (n == 2) { map[0] = 0; map[1] = 1; map[2] = 0; map[3] = 1; }
        else if (n == 3) { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 1; } else { map[0] = 0; map[1] = 1; map[2] = 2; map[3] = 3; }
        for (int i = 0; i < 4; i++) if (FREE_(P_BORDER_TOP_COLOR + i)) { s->border_color[i] = c[map[i]]; MARK(P_BORDER_TOP_COLOR + i); }
        return true;
    }
    case P_COLOR: {
        uint32_t c;
        if (vp_peek(v, &t) && tok_is(&t, "currentcolor")) { vp_next(v, &t); c = a->parent ? a->parent->color : 0xFF000000; }
        else if (!css_parse_color(v, &a->cx, &c)) return false;
        s->color = c; a->cx.cur_color = c; MARK(prop); return true;
    }
    case P_BACKGROUND_COLOR: {
        uint32_t c;
        if (!css_parse_color(v, &a->cx, &c)) return false;
        s->bg_color = c; MARK(prop); return true;
    }
    case P_BACKGROUND: {
        s->bg_count = 0;
        uint32_t color = 0; bool has_color = false;
        for (;;) {
            struct bg_layer layer;
            bool hc = false; uint32_t c = 0;
            if (!parse_background_layer(a, v, &layer, &c, &hc)) return false;
            if (hc) { color = c; has_color = true; }
            if (s->bg_count < MAX_BG) s->bg[s->bg_count++] = layer;
            if (vp_peek(v, &t) && t.kind == V_COMMA) { vp_next(v, &t); continue; }
            break;
        }
        // layers with neither image nor gradient are dropped
        int k = 0;
        for (int i = 0; i < s->bg_count; i++) if (s->bg[i].url || s->bg[i].gradient) s->bg[k++] = s->bg[i];
        s->bg_count = k;
        MARK(P_BACKGROUND_IMAGE);
        if (FREE_(P_BACKGROUND_COLOR)) { s->bg_color = has_color ? color : 0; MARK(P_BACKGROUND_COLOR); }
        MARK(P_BACKGROUND_SIZE); MARK(P_BACKGROUND_POSITION); MARK(P_BACKGROUND_REPEAT); MARK(P_BACKGROUND_CLIP); MARK(P_BACKGROUND_ORIGIN);
        return true;
    }
    case P_BACKGROUND_IMAGE: {
        s->bg_count = 0;
        for (;;) {
            struct bg_layer layer; bg_defaults(&layer);
            bool got;
            if (!parse_bg_image(a, v, &layer, &got) || !got) return false;
            if ((layer.url || layer.gradient) && s->bg_count < MAX_BG) s->bg[s->bg_count++] = layer;
            if (vp_peek(v, &t) && t.kind == V_COMMA) { vp_next(v, &t); continue; }
            break;
        }
        MARK(prop); return true;
    }
    case P_BACKGROUND_SIZE: {
        // applies to each existing layer in turn
        int idx = 0;
        for (;;) {
            struct bg_layer *l = idx < s->bg_count ? &s->bg[idx] : NULL;
            if (!vp_next(v, &t)) return idx > 0;
            uint8_t mode = BGS_AUTO; struct len w1, h1;
            memset(&w1, 0, sizeof(w1)); memset(&h1, 0, sizeof(h1)); w1.kind = h1.kind = L_AUTO;
            if (tok_is(&t, "cover")) mode = BGS_COVER;
            else if (tok_is(&t, "contain")) mode = BGS_CONTAIN;
            else {
                mode = BGS_LEN;
                if (!tok_is(&t, "auto") && !css_parse_len_tok(v, &t, &a->cx, &w1, LF_NONNEG)) return false;
                struct vtoken t2;
                if (vp_peek(v, &t2) && t2.kind != V_COMMA) {
                    vp_next(v, &t2);
                    if (!tok_is(&t2, "auto") && !css_parse_len_tok(v, &t2, &a->cx, &h1, LF_NONNEG)) return false;
                }
            }
            if (l) { l->size_mode = mode; l->size_w = w1; l->size_h = h1; }
            idx++;
            if (vp_peek(v, &t) && t.kind == V_COMMA) { vp_next(v, &t); continue; }
            break;
        }
        MARK(prop); return true;
    }
    case P_BACKGROUND_POSITION: {
        int idx = 0;
        for (;;) {
            struct len x, y;
            if (!parse_bg_position(a, v, &x, &y)) return idx > 0;
            if (idx < s->bg_count) { s->bg[idx].pos_x = x; s->bg[idx].pos_y = y; }
            idx++;
            if (vp_peek(v, &t) && t.kind == V_COMMA) { vp_next(v, &t); continue; }
            break;
        }
        MARK(prop); return true;
    }
    case P_BACKGROUND_REPEAT: {
        int idx = 0;
        for (;;) {
            if (!vp_next(v, &t)) return idx > 0;
            bool rx = true, ry = true;
            if (tok_is(&t, "no-repeat")) rx = ry = false;
            else if (tok_is(&t, "repeat-x")) { rx = true; ry = false; }
            else if (tok_is(&t, "repeat-y")) { rx = false; ry = true; }
            else if (tok_is(&t, "repeat") || tok_is(&t, "space") || tok_is(&t, "round")) {
                struct vtoken t2;
                if (vp_peek(v, &t2) && t2.kind == V_IDENT) { vp_next(v, &t2); ry = !tok_is(&t2, "no-repeat"); }
            } else return false;
            if (idx < s->bg_count) { s->bg[idx].repeat_x = rx; s->bg[idx].repeat_y = ry; }
            idx++;
            if (vp_peek(v, &t) && t.kind == V_COMMA) { vp_next(v, &t); continue; }
            break;
        }
        MARK(prop); return true;
    }
    case P_BACKGROUND_CLIP: case P_BACKGROUND_ORIGIN: {
        int idx = 0;
        for (;;) {
            if (!vp_next(v, &t)) return idx > 0;
            uint8_t val = 0;
            if (tok_is(&t, "border-box")) val = 0; else if (tok_is(&t, "padding-box")) val = 1; else if (tok_is(&t, "content-box")) val = 2;
            else if (tok_is(&t, "text") || tok_is(&t, "-webkit-text")) val = 3; else return false;
            if (idx < s->bg_count) {
                if (prop == P_BACKGROUND_CLIP) s->bg[idx].clip = val;
                else s->bg[idx].origin = val == 0 ? 1 : (val == 1 ? 0 : 2);
            }
            idx++;
            if (vp_peek(v, &t) && t.kind == V_COMMA) { vp_next(v, &t); continue; }
            break;
        }
        MARK(prop); return true;
    }
    case P_BACKGROUND_POSITION_X: case P_BACKGROUND_POSITION_Y: {
        struct len l;
        if (!vp_next(v, &t)) return false;
        memset(&l, 0, sizeof(l)); l.kind = L_VAL;
        if (tok_is(&t, "left") || tok_is(&t, "top")) l.pct = 0; else if (tok_is(&t, "right") || tok_is(&t, "bottom")) l.pct = 10000; else if (tok_is(&t, "center")) l.pct = 5000;
        else if (!css_parse_len_tok(v, &t, &a->cx, &l, 0)) return false;
        for (int i = 0; i < s->bg_count; i++) { if (prop == P_BACKGROUND_POSITION_X) s->bg[i].pos_x = l; else s->bg[i].pos_y = l; }
        MARK(prop); return true;
    }
    case P_BOX_SHADOW: if (!parse_shadows(a, v)) return false; MARK(prop); return true;
    case P_TEXT_SHADOW: case P_OUTLINE: case P_SCALE: case P_ROTATE: case P_TEXT_WRAP: case P_ZOOM: case P_WHITE_SPACE_COLLAPSE:
    case P_TEXT_DECORATION_THICKNESS: case P_TEXT_DECORATION_STYLE: case P_OVERFLOW_INLINE: case P_OVERFLOW_BLOCK: case P_VISIBILITY_X:
    case P_BORDER_START_START_RADIUS: case P_BORDER_END_END_RADIUS:
        MARK(prop); return true;
    case P_BORDER_COLLAPSE: { if (!vp_next(v, &t)) return false; s->border_collapse = tok_is(&t, "collapse"); MARK(prop); return true; }
    case P_BORDER_SPACING: {
        struct len h, vv;
        if (!read_len(a, v, &h, LF_NONNEG)) return false;
        vv = h;
        if (!at_end(v) && !read_len(a, v, &vv, LF_NONNEG)) return false;
        s->spacing_h = h; s->spacing_v = vv; MARK(prop); return true;
    }
    case P_TABLE_LAYOUT: { if (!vp_next(v, &t)) return false; s->table_layout_fixed = tok_is(&t, "fixed"); MARK(prop); return true; }
    case P_ASPECT_RATIO: {
        if (!vp_next(v, &t)) return false;
        s->aspect_w = s->aspect_h = 0;
        if (tok_is(&t, "auto")) {
            struct vtoken t2;
            if (vp_peek(v, &t2) && t2.kind == V_NUM) vp_next(v, &t);
            else { MARK(prop); return true; }
        }
        if (t.kind != V_NUM) return false;
        int w = t.num, h = 1000;
        struct vtoken t2;
        if (vp_peek(v, &t2) && t2.kind == V_SLASH) { vp_next(v, &t2); if (!vp_next(v, &t2) || t2.kind != V_NUM) return false; h = t2.num; }
        if (w <= 0 || h <= 0) return true;
        s->aspect_w = w; s->aspect_h = h; MARK(prop); return true;
    }
    case P_TRANSFORM: case P_TRANSLATE: {
        s->has_transform = false;
        s->translate_x.px = s->translate_x.pct = s->translate_y.px = s->translate_y.pct = 0;
        s->translate_x.kind = s->translate_y.kind = L_VAL; s->translate_x.expr = s->translate_y.expr = NULL;
        if (prop == P_TRANSLATE) {
            struct len x, y; memset(&y, 0, sizeof(y)); y.kind = L_VAL;
            if (vp_peek(v, &t) && tok_is(&t, "none")) { MARK(prop); return true; }
            if (!read_len(a, v, &x, 0)) return false;
            if (!at_end(v)) { if (!read_len(a, v, &y, 0)) return false; }
            s->translate_x = x; s->translate_y = y; s->has_transform = true; MARK(prop); return true;
        }
        while (vp_next(v, &t)) {
            if (t.kind != V_FUNC) { if (tok_is(&t, "none")) break; return false; }
            struct vp args; vp_func_args(v, &args);
            if (w_eq(t.s, t.n, "translate") || w_eq(t.s, t.n, "translate3d")) {
                struct len x, y; memset(&y, 0, sizeof(y)); y.kind = L_VAL;
                if (!read_len(a, &args, &x, 0)) return false;
                struct vtoken c; if (vp_peek(&args, &c) && c.kind == V_COMMA) vp_next(&args, &c);
                if (!at_end(&args)) { if (!read_len(a, &args, &y, 0)) return false; }
                s->translate_x = x; s->translate_y = y; s->has_transform = true;
            } else if (w_eq(t.s, t.n, "translatex")) { struct len x; if (!read_len(a, &args, &x, 0)) return false; s->translate_x = x; s->has_transform = true; }
            else if (w_eq(t.s, t.n, "translatey")) { struct len y; if (!read_len(a, &args, &y, 0)) return false; s->translate_y = y; s->has_transform = true; }
            // scale, rotate, skew, matrix and the rest are not drawn
        }
        MARK(prop); return true;
    }
    case P_CONTENT: {
        s->content_kind = 0; s->content = NULL; s->content_len = 0;
        char *buf = w_alloc(&a->cx.page->larena, (size_t)v->len + 8);
        int n = 0;
        bool any_string = false;
        while (vp_next(v, &t)) {
            if (t.kind == V_STR) {
                for (int i = 0; i < t.n; i++) {
                    if (t.s[i] == '\\' && i + 1 < t.n) {
                        i++;
                        if (w_hex(t.s[i]) >= 0) {
                            uint32_t cp = 0; int k = 0;
                            while (i < t.n && k < 6 && w_hex(t.s[i]) >= 0) { cp = cp * 16 + (uint32_t)w_hex(t.s[i]); i++; k++; }
                            if (i < t.n && w_is_space(t.s[i])) i++;
                            i--;
                            if (n + 5 < v->len + 8) n += w_utf8_put(cp, buf + n);
                            continue;
                        }
                        if (t.s[i] == 'n' && false) { buf[n++] = '\n'; continue; }
                    }
                    buf[n++] = t.s[i];
                }
                any_string = true;
            } else if (t.kind == V_IDENT && (w_eq(t.s, t.n, "open-quote"))) { n += w_utf8_put(0x201C, buf + n); any_string = true; }
            else if (t.kind == V_IDENT && (w_eq(t.s, t.n, "close-quote"))) { n += w_utf8_put(0x201D, buf + n); any_string = true; }
            else if (t.kind == V_FUNC) { struct vp skip; vp_func_args(v, &skip); }
            else if (tok_is(&t, "none") || tok_is(&t, "normal")) { }
        }
        buf[n] = '\0';
        if (any_string) { s->content_kind = 1; s->content = buf; s->content_len = n; }
        MARK(prop); return true;
    }
    case P_POINTER_EVENTS: { if (!vp_next(v, &t)) return false; s->pointer_events_none = tok_is(&t, "none"); MARK(prop); return true; }
    case P_USER_SELECT: { if (!vp_next(v, &t)) return false; s->user_select_none = tok_is(&t, "none"); MARK(prop); return true; }
    case P_FILL: case P_STROKE: {
        if (!vp_peek(v, &t)) return false;
        uint32_t c = 0; const char *ref = NULL; uint8_t set = 1;
        if (tok_is(&t, "none")) { vp_next(v, &t); c = 0; }
        else if (t.kind == V_FUNC && w_eq(t.s, t.n, "url")) { vp_next(v, &t); struct vp args; vp_func_args(v, &args); parse_url_arg(a, &args, &ref); c = 0xFF000000; }
        else if (tok_is(&t, "context-fill") || tok_is(&t, "context-stroke")) { vp_next(v, &t); c = a->s->color; }
        else if (!css_parse_color(v, &a->cx, &c)) return false;
        if (prop == P_FILL) { s->svg_fill = c; s->svg_fill_set = set; s->svg_fill_ref = ref; }
        else { s->svg_stroke = c; s->svg_stroke_set = set; s->svg_stroke_ref = ref; }
        MARK(prop); return true;
    }
    case P_STROKE_WIDTH: {
        if (!vp_next(v, &t)) return false;
        if (t.kind == V_NUM) s->svg_stroke_width = t.num;
        else if (t.kind == V_DIM && w_eq(t.unit, t.unit_len, "px")) s->svg_stroke_width = t.num;
        else { struct len l; if (!css_parse_len_tok(v, &t, &a->cx, &l, LF_NONNEG)) return false; s->svg_stroke_width = l.px * 1000; }
        MARK(prop); return true;
    }
    case P_FILL_OPACITY: case P_STROKE_OPACITY: {
        int n; if (!css_parse_number(v, &a->cx, &n)) return false;
        int o = (n * 255 + 500) / 1000; o = w_clampi(o, 0, 255);
        if (prop == P_FILL_OPACITY) s->fill_opacity = (uint8_t)o; else s->stroke_opacity = (uint8_t)o;
        MARK(prop); return true;
    }
    case P_STROKE_LINECAP: { if (!vp_next(v, &t)) return false; s->svg_linecap = tok_is(&t, "round") ? 1 : (tok_is(&t, "square") ? 2 : 0); MARK(prop); return true; }
    case P_STROKE_LINEJOIN: { if (!vp_next(v, &t)) return false; s->svg_linejoin = tok_is(&t, "round") ? 1 : (tok_is(&t, "bevel") ? 2 : 0); MARK(prop); return true; }
    case P_FONT: {
        // [style] [variant] [weight] size[/line-height] family
        uint8_t italic = 0; int16_t weight = 400;
        struct vtoken x;
        bool got_size = false;
        // special system font names
        if (vp_peek(v, &x) && x.kind == V_IDENT && (w_eq(x.s, x.n, "caption") || w_eq(x.s, x.n, "icon") || w_eq(x.s, x.n, "menu") || w_eq(x.s, x.n, "message-box") || w_eq(x.s, x.n, "small-caption") || w_eq(x.s, x.n, "status-bar"))) {
            if (FREE_(P_FONT_SIZE)) { s->font_size = 13; a->cx.font_px = 13; MARK(P_FONT_SIZE); }
            if (FREE_(P_FONT_FAMILY)) { s->font_family = WF_SANS; MARK(P_FONT_FAMILY); }
            return true;
        }
        while (vp_peek(v, &x)) {
            if (x.kind == V_IDENT) {
                if (w_eq(x.s, x.n, "italic") || w_eq(x.s, x.n, "oblique")) { vp_next(v, &x); italic = 1; continue; }
                if (w_eq(x.s, x.n, "normal") || w_eq(x.s, x.n, "small-caps")) { vp_next(v, &x); continue; }
                if (w_eq(x.s, x.n, "bold")) { vp_next(v, &x); weight = 700; continue; }
                if (w_eq(x.s, x.n, "bolder")) { vp_next(v, &x); weight = 700; continue; }
                if (w_eq(x.s, x.n, "lighter")) { vp_next(v, &x); weight = 300; continue; }
            }
            if (x.kind == V_NUM && x.num >= 1000 && x.num <= 1000000 && x.num % 1000 == 0) { vp_next(v, &x); weight = (int16_t)(x.num / 1000); continue; }
            break;
        }
        // size
        struct vp sv = *v;
        if (!vp_next(&sv, &x)) return false;
        if (FREE_(P_FONT_SIZE)) {
            struct vp c = *v;
            // apply font-size on its own
            struct vp cc = c;
            if (!apply_value(a, P_FONT_SIZE, &cc)) return false;
            *v = cc;
            got_size = true;
        } else {
            if (!vp_next(v, &x)) return false;
            got_size = true;
        }
        if (vp_peek(v, &x) && x.kind == V_SLASH) {
            vp_next(v, &x);
            struct vp lv = *v;
            if (FREE_(P_LINE_HEIGHT)) { if (!apply_value(a, P_LINE_HEIGHT, &lv)) return false; *v = lv; }
            else vp_next(v, &x);
        } else if (FREE_(P_LINE_HEIGHT)) { s->line_height_pct = 0; s->line_height_px = 0; MARK(P_LINE_HEIGHT); }
        (void)got_size;
        if (FREE_(P_FONT_STYLE)) { s->font_italic = italic; MARK(P_FONT_STYLE); }
        if (FREE_(P_FONT_WEIGHT)) { s->font_weight = weight; MARK(P_FONT_WEIGHT); }
        if (FREE_(P_FONT_FAMILY)) { uint8_t f = s->font_family; if (!parse_family(v, &f)) return false; s->font_family = f; MARK(P_FONT_FAMILY); }
        return true;
    }
    case P_ALL: return true;
    default: break;
    }
    return false;
}

// Applies one declaration. Variables are replaced first; keywords handled; then the value is read.
bool css_apply_decl(struct style *s, const struct style *parent, const struct css_ctx *cx, const struct decl *d,
                    unsigned char *set, struct style *initial) {
    struct A a;
    a.s = s; a.parent = parent; a.initial = initial; a.cx = *cx; a.set = set;
    a.cx.font_px = s->font_size;
    a.cx.cur_color = s->color;
    char buf[4096];
    const char *text = d->value;
    int len = d->length;
    // trim
    while (len > 0 && w_is_space(text[0])) { text++; len--; }
    while (len > 0 && w_is_space(text[len - 1])) len--;
    if (len == 0) return false;
    bool has_var = false;
    for (int i = 0; i + 3 < len; i++) {
        if ((text[i] == 'v' || text[i] == 'V') && (text[i + 1] == 'a' || text[i + 1] == 'A') && (text[i + 2] == 'r' || text[i + 2] == 'R') && text[i + 3] == '(') { has_var = true; break; }
        if ((text[i] == 'e' || text[i] == 'E') && (text[i + 1] == 'n' || text[i + 1] == 'N') && (text[i + 2] == 'v' || text[i + 2] == 'V') && text[i + 3] == '(') { has_var = true; break; }
    }
    if (has_var) {
        int ol = 0;
        if (!subst(s->vars, text, len, buf, &ol, (int)sizeof(buf), 0)) return false;
        text = buf; len = ol;
        while (len > 0 && w_is_space(text[len - 1])) len--;
        if (len == 0) return false;
    }
    int prop = d->prop;
    // inherit / initial / unset / revert
    if ((len == 7 && w_eq(text, 7, "inherit")) || (len == 7 && w_eq(text, 7, "initial")) || (len == 5 && w_eq(text, 5, "unset"))
        || (len == 6 && w_eq(text, 6, "revert")) || (len == 12 && w_eq(text, 12, "revert-layer"))) {
        int kind;
        if (w_eq(text, len, "inherit")) kind = 1;
        else if (w_eq(text, len, "initial")) kind = 0;
        else kind = is_inherited_prop(prop) || css_prop_is_shorthand(prop) ? (css_prop_is_shorthand(prop) ? 0 : 1) : 0;
        if (w_eq(text, len, "unset") && css_prop_is_shorthand(prop)) {
            // unset on a shorthand: each longhand according to whether it inherits
            int members[16]; int n = shorthand_members(prop, members);
            for (int i = 0; i < n; i++) {
                if (a.set[members[i]]) continue;
                copy_prop(s, is_inherited_prop(members[i]) && parent ? parent : initial, members[i]);
                a.set[members[i]] = 1;
            }
            return true;
        }
        if (prop == P_ALL) {
            for (int p = 1; p < P_COUNT; p++) {
                if (css_prop_is_shorthand(p) || a.set[p]) continue;
                bool inh = is_inherited_prop(p);
                const struct style *src = (kind == 1 || (w_eq(text, len, "unset") && inh)) && parent ? parent : initial;
                copy_prop(s, src, p);
                a.set[p] = 1;
            }
            return true;
        }
        keyword_apply(&a, prop, kind);
        return true;
    }
    if (prop == P_ALL) return true;
    struct vp v;
    vp_init(&v, text, len);
    // Some properties set several things; if the value isn't understood nothing is set.
    unsigned char before[P_COUNT];
    memcpy(before, a.set, sizeof(before));
    bool ok = apply_value(&a, prop, &v);
    if (!ok) { memcpy(a.set, before, sizeof(before)); }
    return ok;
}
