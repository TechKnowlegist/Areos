// web_box.c - from the page tree to boxes.
//
// Each element that is shown becomes a box. Text becomes text boxes. Boxes that
// have both words and blocks inside get their words wrapped in unnamed blocks,
// so that every container holds either only lines or only blocks.

#include "web_lay.h"

static const char *INPUT_BUTTONS[] = { "submit", "button", "reset", "image", NULL };

const char *node_input_type(const struct node *n) {
    static char buf[24];
    const char *t = node_attr(n, "type");
    if (!t) return "text";
    int i = 0;
    while (t[i] && i < 23) { buf[i] = (char)w_lower((unsigned char)t[i]); i++; }
    buf[i] = '\0';
    return buf;
}

bool node_is_focusable(const struct node *n) {
    if (n->type != N_ELEMENT) return false;
    if (n->style && (n->style->display == D_NONE || n->style->visibility)) return false;
    switch (n->tag) {
    case T_A: case T_AREA: return node_has_attr(n, "href") || node_has_attr(n, "tabindex") || n->aid > 0;
    case T_BUTTON: case T_SELECT: case T_TEXTAREA: return !node_has_attr(n, "disabled");
    case T_INPUT: return !w_eq0(node_input_type(n), "hidden") && !node_has_attr(n, "disabled");
    case T_SUMMARY: return true;
    default: break;
    }
    const char *ti = node_attr(n, "tabindex");
    if (ti && (ti[0] != '-')) return true;
    const char *role = node_attr(n, "role");
    if (role && n->aid > 0 && (w_eq0(role, "button") || w_eq0(role, "link") || w_eq0(role, "tab") || w_eq0(role, "menuitem") || w_eq0(role, "checkbox") || w_eq0(role, "switch"))) return true;
    if (n->aid > 0) return true;
    return false;
}

struct box *box_new(struct page *p, struct node *n, struct style *s, int type) {
    struct box *b = w_alloc(&p->larena, sizeof(struct box));
    b->node = n; b->style = s; b->type = (uint8_t)type;
    b->focusable = -1;
    if (n && type != BT_TEXT && n->type == N_ELEMENT && !n->box && s && !s->is_pseudo) n->box = b;
    return b;
}

void box_append(struct box *parent, struct box *child) {
    child->parent = parent;
    child->next = NULL;
    if (parent->last) parent->last->next = child; else parent->first = child;
    parent->last = child;
}

bool box_is_block_level(const struct box *b) {
    if (b->out_of_flow) return false;
    return (b->type == BT_BLOCK || b->type == BT_REPLACED) && !b->inline_level;
}

static bool display_is_inline_level(int d) {
    return d == D_INLINE || d == D_INLINE_BLOCK || d == D_INLINE_FLEX || d == D_INLINE_GRID || d == D_INLINE_TABLE || d == D_RUBY;
}

struct bstate {
    struct page *p;
    int focus;          // the focusable ancestor's index, or -1
};

static int focus_register(struct page *p, struct node *n) {
    if (p->focus_count >= p->focus_cap) {
        int nc = p->focus_cap ? p->focus_cap * 2 : 64;
        struct node **nn = realloc(p->focus_nodes, sizeof(struct node *) * (size_t)nc);
        if (!nn) return -1;
        p->focus_nodes = nn; p->focus_cap = nc;
    }
    p->focus_nodes[p->focus_count] = n;
    return p->focus_count++;
}

static struct box *build_element(struct bstate *bs, struct node *n);

static struct box *text_box(struct bstate *bs, struct style *s, struct node *n, const char *text, int len) {
    struct box *t = box_new(bs->p, n, s, BT_TEXT);
    t->inline_level = true;
    t->text = text; t->text_len = len;
    t->focusable = bs->focus;
    return t;
}

static bool all_space(const char *s, int len) {
    for (int i = 0; i < len; i++) if (!w_is_space((unsigned char)s[i])) return false;
    return true;
}

static void build_children(struct bstate *bs, struct box *parent, struct node *n);

static struct box *pseudo_box(struct bstate *bs, struct node *host, struct style *ps) {
    struct page *p = bs->p;
    bool inl = display_is_inline_level(ps->display);
    struct box *b = box_new(p, host, ps, ps->display == D_INLINE ? BT_INLINE : BT_BLOCK);
    b->inline_level = inl;
    b->from_pseudo = true;
    b->focusable = bs->focus;
    if (ps->position == POS_ABSOLUTE || ps->position == POS_FIXED) { b->out_of_flow = true; }
    if (ps->floating != FL_NONE) { b->out_of_flow = true; b->is_float = true; }
    if (ps->content_len > 0) {
        struct box *t = text_box(bs, ps, host, ps->content, ps->content_len);
        t->from_pseudo = true;
        box_append(b, t);
    }
    return b;
}

static void wrap_inline_runs(struct bstate *bs, struct box *b) {
    // b holds a mix: wrap each run of inline-level boxes in an unnamed block
    struct page *p = bs->p;
    bool any_block = false, any_inline = false;
    for (struct box *c = b->first; c; c = c->next) {
        if (c->out_of_flow) continue;
        if (box_is_block_level(c)) any_block = true; else any_inline = true;
    }
    if (!any_block) { b->ifc = any_inline || b->first != NULL; return; }
    struct box *first = b->first;
    b->first = b->last = NULL;
    struct box *run = NULL;
    for (struct box *c = first, *nx; c; c = nx) {
        nx = c->next;
        c->next = NULL;
        bool blockish = box_is_block_level(c);
        if (blockish) {
            run = NULL;
            box_append(b, c);
        } else if (c->out_of_flow && !run) {
            box_append(b, c);       // floats/abs between blocks stay at this level
        } else {
            if (!run) {
                // whitespace-only text between blocks isn't worth a block
                if (c->type == BT_TEXT && all_space(c->text, c->text_len) && c->style->white_space <= WS_NOWRAP) continue;
                struct style *as = css_inherit_style(p, b->style, D_BLOCK);
                run = box_new(p, NULL, as, BT_BLOCK);
                run->anon = true; run->ifc = true;
                box_append(b, run);
            }
            box_append(run, c);
        }
    }
    // drop anonymous blocks that hold only spaces
    struct box *prev = NULL;
    for (struct box *c = b->first, *nx; c; c = nx) {
        nx = c->next;
        bool drop = false;
        if (c->anon && c->ifc) {
            drop = true;
            for (struct box *k = c->first; k; k = k->next)
                if (!(k->type == BT_TEXT && all_space(k->text, k->text_len) && k->style->white_space <= WS_NOWRAP)) { drop = false; break; }
        }
        if (drop) { if (prev) prev->next = nx; else b->first = nx; if (!nx) b->last = prev; }
        else prev = c;
    }
}

// Items of a flex or grid container: text runs and inline things each become an item.
static void make_items(struct bstate *bs, struct box *b) {
    struct page *p = bs->p;
    struct box *first = b->first;
    b->first = b->last = NULL;
    struct box *run = NULL;
    for (struct box *c = first, *nx; c; c = nx) {
        nx = c->next;
        c->next = NULL;
        if (c->is_float) { c->is_float = false; c->out_of_flow = c->style->position == POS_ABSOLUTE || c->style->position == POS_FIXED; }
        if (c->out_of_flow) { run = NULL; box_append(b, c); continue; }
        if (c->type == BT_TEXT) {
            if (all_space(c->text, c->text_len) && c->style->white_space <= WS_NOWRAP) continue;
            if (!run) {
                struct style *as = css_inherit_style(p, b->style, D_BLOCK);
                run = box_new(p, NULL, as, BT_BLOCK);
                run->anon = true; run->ifc = true;
                box_append(b, run);
            }
            box_append(run, c);
            continue;
        }
        run = NULL;
        // a flex item is blockified
        if (c->inline_level) {
            c->inline_level = false;
            if (c->type == BT_INLINE) {
                c->type = BT_BLOCK;
                wrap_inline_runs(bs, c);
            }
        }
        box_append(b, c);
    }
}

// ---- Tables: make the boxes line up as table > rows > cells ----

static bool is_table_part(int d) {
    return d == D_TABLE_ROW_GROUP || d == D_TABLE_HEADER_GROUP || d == D_TABLE_FOOTER_GROUP || d == D_TABLE_ROW || d == D_TABLE_CELL
        || d == D_TABLE_CAPTION || d == D_TABLE_COLUMN || d == D_TABLE_COLUMN_GROUP;
}

static struct box *anon_box(struct bstate *bs, struct box *parent, int display) {
    struct style *as = css_inherit_style(bs->p, parent->style, display);
    struct box *b = box_new(bs->p, NULL, as, BT_BLOCK);
    b->anon = true;
    return b;
}

static void normalize_row(struct bstate *bs, struct box *row) {
    struct box *first = row->first;
    row->first = row->last = NULL;
    struct box *cell = NULL;
    for (struct box *c = first, *nx; c; c = nx) {
        nx = c->next; c->next = NULL;
        if (c->style->display == D_TABLE_CELL && !c->out_of_flow) { cell = NULL; box_append(row, c); continue; }
        if (c->out_of_flow) { box_append(row, c); continue; }
        if (c->type == BT_TEXT && all_space(c->text, c->text_len)) continue;
        if (!cell) {
            cell = anon_box(bs, row, D_TABLE_CELL);
            cell->ifc = true;
            box_append(row, cell);
        }
        box_append(cell, c);
    }
    for (struct box *c = row->first; c; c = c->next) if (c->anon && c->style->display == D_TABLE_CELL) wrap_inline_runs(bs, c);
}

static void normalize_table(struct bstate *bs, struct box *t) {
    struct box *first = t->first;
    t->first = t->last = NULL;
    struct box *row = NULL;
    for (struct box *c = first, *nx; c; c = nx) {
        nx = c->next; c->next = NULL;
        int d = c->style->display;
        if (c->out_of_flow) { box_append(t, c); continue; }
        if (d == D_TABLE_CAPTION || d == D_TABLE_COLUMN || d == D_TABLE_COLUMN_GROUP) { row = NULL; box_append(t, c); continue; }
        if (d == D_TABLE_ROW_GROUP || d == D_TABLE_HEADER_GROUP || d == D_TABLE_FOOTER_GROUP) {
            row = NULL;
            // normalize the group's rows
            struct box *gfirst = c->first;
            c->first = c->last = NULL;
            struct box *grow = NULL;
            for (struct box *g = gfirst, *gn; g; g = gn) {
                gn = g->next; g->next = NULL;
                if (g->style->display == D_TABLE_ROW) { grow = NULL; box_append(c, g); }
                else if (g->out_of_flow) box_append(c, g);
                else if (g->type == BT_TEXT && all_space(g->text, g->text_len)) continue;
                else {
                    if (!grow) { grow = anon_box(bs, c, D_TABLE_ROW); box_append(c, grow); }
                    box_append(grow, g);
                }
            }
            for (struct box *g = c->first; g; g = g->next) if (g->style->display == D_TABLE_ROW) normalize_row(bs, g);
            box_append(t, c);
            continue;
        }
        if (d == D_TABLE_ROW) { row = NULL; normalize_row(bs, c); box_append(t, c); continue; }
        if (c->type == BT_TEXT && all_space(c->text, c->text_len)) continue;
        // cells and anything else go into an unnamed row
        if (!row) { row = anon_box(bs, t, D_TABLE_ROW); box_append(t, row); }
        box_append(row, c);
    }
    for (struct box *c = t->first; c; c = c->next) if (c->anon && c->style->display == D_TABLE_ROW) normalize_row(bs, c);
}

// ---- Elements ----

static int list_counter_start(struct node *list) {
    if (!list) return 1;
    const char *st = node_attr(list, "start");
    int v = 1;
    if (st) { v = 0; bool neg = false; const char *q = st; if (*q == '-') { neg = true; q++; } while (w_is_digit(*q)) { v = v * 10 + (*q - '0'); q++; } if (neg) v = -v; }
    return v;
}

static void build_children(struct bstate *bs, struct box *parent, struct node *n) {
    struct page *p = bs->p;
    bool is_list = n->tag == T_OL || n->tag == T_UL || n->tag == T_MENU;
    bool reversed = n->tag == T_OL && node_has_attr(n, "reversed");
    int counter = n->tag == T_OL ? list_counter_start(n) : 1;
    if (reversed && !node_attr(n, "start")) {
        counter = 0;
        for (struct node *c = n->first; c; c = c->next) if (c->type == N_ELEMENT && c->tag == T_LI) counter++;
    }
    struct style *hs = n->style;
    if (n->before && !(n->tag == T_DETAILS)) { struct box *pb = pseudo_box(bs, n, n->before); box_append(parent, pb); }
    bool in_details_closed = n->tag == T_DETAILS && !n->open;
    bool seen_summary = false;
    for (struct node *c = n->first; c; c = c->next) {
        if (c->type == N_TEXT) {
            if (c->text_len == 0) continue;
            if (in_details_closed) continue;
            box_append(parent, text_box(bs, hs, c, c->text, c->text_len));
            continue;
        }
        if (in_details_closed) {
            if (c->tag == T_SUMMARY && !seen_summary) { seen_summary = true; }
            else continue;
        }
        if (!c->style) continue;
        if (c->style->display == D_CONTENTS) {
            // no box of its own: its children join this parent
            struct bstate sub = *bs;
            build_children(&sub, parent, c);
            continue;
        }
        struct box *cb = build_element(bs, c);
        if (!cb) continue;
        if (is_list && c->style->display == D_LIST_ITEM) {
            const char *val = node_attr(c, "value");
            if (val) { int v = 0; bool neg = false; const char *q = val; if (*q == '-') { neg = true; q++; } while (w_is_digit(*q)) { v = v * 10 + (*q - '0'); q++; } counter = neg ? -v : v; }
            cb->list_index = counter;
            counter += reversed ? -1 : 1;
        } else if (c->style->display == D_LIST_ITEM) {
            cb->list_index = 1;
        }
        box_append(parent, cb);
    }
    if (n->after) { struct box *pb = pseudo_box(bs, n, n->after); box_append(parent, pb); }
    (void)p;
}

static void set_flow_flags(struct box *b) {
    struct style *s = b->style;
    if (s->position == POS_ABSOLUTE || s->position == POS_FIXED) b->out_of_flow = true;
    if (s->floating != FL_NONE && !b->out_of_flow) { b->out_of_flow = true; b->is_float = true; }
    b->hidden = s->visibility != 0;
}

static void set_bfc(struct box *b) {
    struct style *s = b->style;
    int d = s->display;
    b->bfc = b->out_of_flow || b->inline_level || s->overflow_x != OV_VISIBLE || s->overflow_y != OV_VISIBLE
          || d == D_FLEX || d == D_GRID || d == D_INLINE_FLEX || d == D_INLINE_GRID || d == D_TABLE_CELL || d == D_TABLE || d == D_INLINE_TABLE
          || s->position == POS_ABSOLUTE || s->position == POS_FIXED;
}


// ---- Picture addresses ----

static bool type_unsupported(const char *t) {
    if (!t) return false;
    for (const char *q = t; *q; q++) {
        if (w_starts(q, "webp") && false) return true;
        if (w_starts(q, "avif") || w_starts(q, "heic") || w_starts(q, "jxl")) return true;
    }
    return false;
}

// Pick one address out of a srcset ("a.jpg 480w, b.jpg 800w"). Prefers a width around 800-1200 pixels, else the closest.
static bool pick_srcset(const char *set, int target, char *out, int max) {
    int best_w = -1; char best[600]; best[0] = '\0';
    const char *q = set;
    while (*q) {
        while (*q && (w_is_space(*q) || *q == ',')) q++;
        const char *us = q;
        while (*q && !w_is_space(*q) && !(*q == ',' && q > us && q[-1] != ':' && false)) { if (*q == ',' && q > us && (q[1] == ' ' || q[1] == '\0')) break; q++; }
        int ul = (int)(q - us);
        while (*q == ' ') q++;
        int w = 0; bool isx = false; int xv = 1000;
        if (w_is_digit(*q)) {
            int v = 0; while (w_is_digit(*q)) { v = v * 10 + (*q - '0'); q++; }
            if (*q == 'w') { w = v; q++; }
            else { if (*q == '.') { q++; int f = 0, d = 100; while (w_is_digit(*q)) { f += (*q - '0') * d; d /= 10; q++; } xv = v * 1000 + f; } else xv = v * 1000; isx = true; if (*q == 'x') q++; }
        }
        while (*q && *q != ',') q++;
        if (ul <= 0 || ul >= 599) continue;
        int score_w = w ? w : (isx ? xv * 1000 / 1000 * 640 / 1000 : 0);
        if (!w && !isx) score_w = 0;
        bool better;
        if (best_w < 0) better = true;
        else {
            int db = best_w - target, dn = score_w - target;
            // prefer the smallest that reaches the target, else the biggest below it
            if (dn >= 0 && db < 0) better = true;
            else if (dn >= 0 && db >= 0) better = dn < db;
            else if (dn < 0 && db < 0) better = dn > db;
            else better = false;
        }
        if (better) { best_w = score_w; memcpy(best, us, (size_t)ul); best[ul] = '\0'; }
    }
    if (best_w < 0 || !best[0]) return false;
    int l = (int)strlen(best); if (l >= max) l = max - 1;
    memcpy(out, best, (size_t)l); out[l] = '\0';
    return true;
}

static bool is_tiny_placeholder(const char *src) {
    return src && w_starts(src, "data:") && (int)strlen(src) < 200;
}

// The address of the picture an <img> (or <input type=image>) shows, or NULL.
static struct wimage *image_for_node(struct page *p, struct node *n) {
    char pick[640];
    const char *src = NULL;
    if (n->tag == T_IMG && n->parent && n->parent->tag == T_PICTURE) {
        for (struct node *sib = n->parent->first; sib && sib != n; sib = sib->next) {
            if (sib->tag != T_SOURCE) continue;
            const char *ss = node_attr(sib, "srcset");
            if (!ss) ss = node_attr(sib, "data-srcset");
            if (!ss || type_unsupported(node_attr(sib, "type"))) continue;
            const char *ty = node_attr(sib, "type");
            if (ty && w_starts(ty, "image/webp")) { /* decoded if the browser can; try it */ }
            if (node_attr(sib, "media")) continue;
            if (pick_srcset(ss, 900, pick, sizeof(pick))) { src = pick; break; }
        }
    }
    if (!src) {
        const char *ss = node_attr(n, "srcset");
        if (!ss) ss = node_attr(n, "data-srcset");
        if (ss && pick_srcset(ss, 900, pick, sizeof(pick))) src = pick;
    }
    if (!src) {
        const char *s0 = node_attr(n, "src");
        const char *lazy = node_attr(n, "data-src");
        if (!lazy) lazy = node_attr(n, "data-lazy-src");
        if (!lazy) lazy = node_attr(n, "data-original");
        if ((!s0 || !*s0 || is_tiny_placeholder(s0)) && lazy && *lazy) s0 = lazy;
        src = s0;
    }
    if (!src || !*src) return NULL;
    char full[2048];
    web_url_resolve(p->base_url, src, full, sizeof(full));
    if (!full[0]) return NULL;
    return web_image_get(p, full);
}

// ---- List markers ----

static int roman(int v, bool upper, char *out, int max) {
    static const int vals[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *const lo[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    static const char *const up[] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
    int n = 0;
    if (v <= 0 || v >= 4000) return 0;
    for (int i = 0; i < 13; i++) while (v >= vals[i]) { const char *t = upper ? up[i] : lo[i]; for (; *t && n < max - 1; t++) out[n++] = *t; v -= vals[i]; }
    return n;
}

int list_marker_text(const struct style *s, int index, char *out, int max) {
    int n = 0;
    char tmp[16];
    switch (s->list_style) {
    case LS_DECIMAL: case LS_DECIMAL_ZERO: {
        int v = index, k = 0;
        bool neg = v < 0; if (neg) v = -v;
        do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 12);
        if (s->list_style == LS_DECIMAL_ZERO && k < 2) tmp[k++] = '0';
        if (neg) out[n++] = '-';
        while (k) out[n++] = tmp[--k];
        break; }
    case LS_LOWER_ALPHA: case LS_UPPER_ALPHA: {
        int v = index > 0 ? index : 1, k = 0;
        while (v > 0 && k < 8) { v--; tmp[k++] = (char)((s->list_style == LS_LOWER_ALPHA ? 'a' : 'A') + v % 26); v /= 26; }
        while (k) out[n++] = tmp[--k];
        break; }
    case LS_LOWER_ROMAN: n = roman(index, false, out, max - 4); break;
    case LS_UPPER_ROMAN: n = roman(index, true, out, max - 4); break;
    case LS_LOWER_GREEK: {
        int v = index > 0 ? (index - 1) % 24 : 0;
        uint32_t cp = 0x3B1 + (uint32_t)v + (v >= 17 ? 1 : 0);
        n = w_utf8_put(cp, out);
        break; }
    default: return 0;
    }
    if (n + 2 < max) { out[n++] = '.'; out[n++] = ' '; }
    out[n] = '\0';
    return n;
}

static struct box *build_input(struct bstate *bs, struct node *n, struct style *s) {
    struct page *p = bs->p;
    const char *type = node_input_type(n);
    bool inl = display_is_inline_level(s->display);
    int kind = RK_INPUT_TEXT;
    if (w_eq0(type, "hidden")) return NULL;
    if (w_eq0(type, "checkbox")) kind = RK_INPUT_CHECK;
    else if (w_eq0(type, "radio")) kind = RK_INPUT_RADIO;
    else if (w_eq0(type, "range")) kind = RK_RANGE;
    else if (w_eq0(type, "color")) kind = RK_COLOR;
    bool is_button = false;
    for (int i = 0; INPUT_BUTTONS[i]; i++) if (w_eq0(type, INPUT_BUTTONS[i])) is_button = true;
    if (w_eq0(type, "file")) is_button = true;
    if (w_eq0(type, "image")) {
        struct box *b = box_new(p, n, s, BT_REPLACED);
        b->replaced = RK_IMAGE; b->inline_level = inl;
        b->image = image_for_node(p, n);
        b->alt = node_attr(n, "alt");
        return b;
    }
    if (is_button) {
        const char *val = node_attr(n, "value");
        const char *label = val;
        if (w_eq0(type, "file")) label = "Choose File   No file chosen";
        else if (!label || !*label) label = w_eq0(type, "reset") ? "Reset" : (w_eq0(type, "submit") ? "Submit" : "");
        struct box *b = box_new(p, n, s, BT_BLOCK);
        b->inline_level = inl;
        b->center_v = true;
        int len = w_len(label);
        if (len) box_append(b, text_box(bs, s, n, w_strdup(&p->larena, label, len), len));
        b->ifc = true;
        return b;
    }
    struct box *b = box_new(p, n, s, BT_REPLACED);
    b->replaced = (uint8_t)kind;
    b->inline_level = inl;
    return b;
}

static struct box *build_element(struct bstate *bs, struct node *n) {
    struct page *p = bs->p;
    struct style *s = n->style;
    if (!s || s->display == D_NONE) return NULL;
    if (n->tag == T_NOSCRIPT && p->scripting) return NULL;
    if (n->tag == T_WBR || n->tag == T_SCRIPT || n->tag == T_STYLE) return NULL;
    struct box *b = NULL;
    bool inl = display_is_inline_level(s->display);
    int my_focus = bs->focus;
    if (node_is_focusable(n)) {
        int id = focus_register(p, n);
        n->focusable = id;
        my_focus = id;
    } else n->focusable = -1;
    struct bstate sub = *bs;
    sub.focus = my_focus;

    if (n->tag == T_BR) {
        b = box_new(p, n, s, BT_BR);
        b->inline_level = true;
        b->focusable = my_focus;
        return b;
    }
    if (n->tag == T_IMG || n->tag == T_SVG || n->tag == T_CANVAS || n->tag == T_PICTURE && false) {
        b = box_new(p, n, s, BT_REPLACED);
        b->replaced = n->tag == T_IMG ? RK_IMAGE : (n->tag == T_SVG ? RK_SVG : RK_BLANK);
        b->inline_level = inl || s->display == D_INLINE;
        if (s->display == D_BLOCK || s->display == D_FLEX || s->display == D_GRID || s->display == D_LIST_ITEM) b->inline_level = false;
        b->alt = node_attr(n, "alt");
        if (n->tag == T_IMG) b->image = image_for_node(p, n);
        if (n->tag == T_SVG) { int sw, sh; if (svg_node_size(n, &sw, &sh)) { b->intrinsic_w = sw; b->intrinsic_h = sh; } }
        set_flow_flags(b);
        b->focusable = my_focus;
        b->bfc = true;
        return b;
    }
    if (n->tag == T_INPUT) {
        b = build_input(&sub, n, s);
        if (!b) return NULL;
        set_flow_flags(b);
        b->focusable = my_focus;
        set_bfc(b);
        if (b->type == BT_BLOCK) wrap_inline_runs(&sub, b);
        return b;
    }
    if (n->tag == T_SELECT || n->tag == T_TEXTAREA) {
        b = box_new(p, n, s, BT_REPLACED);
        b->replaced = n->tag == T_SELECT ? RK_SELECT : RK_TEXTAREA;
        b->inline_level = inl || s->display == D_INLINE;
        set_flow_flags(b);
        b->focusable = my_focus;
        b->bfc = true;
        return b;
    }

    int d = s->display;
    if (d == D_INLINE) {
        b = box_new(p, n, s, BT_INLINE);
        b->inline_level = true;
    } else if (inl) {
        b = box_new(p, n, s, BT_BLOCK);
        b->inline_level = true;
    } else {
        b = box_new(p, n, s, BT_BLOCK);
        b->inline_level = false;
    }
    b->focusable = my_focus;
    set_flow_flags(b);
    if (d == D_LIST_ITEM) b->is_marker_host = true;
    build_children(&sub, b, n);

    // <summary> and the marker of a list item with the marker inside: a first word that looks like the marker
    if (n->tag == T_SUMMARY || (d == D_LIST_ITEM && s->list_pos_inside && s->list_style != LS_NONE)) {
        // markers are painted; summaries get a small arrow as text
        if (n->tag == T_SUMMARY && n->parent && n->parent->tag == T_DETAILS) {
            const char *arrow = n->parent->open ? "\xE2\x96\xBE " : "\xE2\x96\xB8 ";
            struct box *t = text_box(&sub, s, n, arrow, 4);
            t->next = b->first;
            if (!b->first) b->last = t;
            b->first = t; t->parent = b;
        }
    }

    // a link or span with blocks inside is itself a block
    if (b->type == BT_INLINE) {
        bool has_block = false;
        for (struct box *c = b->first; c; c = c->next) if (box_is_block_level(c)) { has_block = true; break; }
        if (has_block) { b->type = BT_BLOCK; b->inline_level = false; b->block_in_inline = true; }
    }

    if (b->type == BT_BLOCK) {
        switch (d) {
        case D_FLEX: case D_INLINE_FLEX: case D_GRID: case D_INLINE_GRID:
            make_items(&sub, b); break;
        case D_TABLE: case D_INLINE_TABLE:
            normalize_table(&sub, b); break;
        case D_TABLE_ROW:
            normalize_row(&sub, b); break;
        case D_TABLE_ROW_GROUP: case D_TABLE_HEADER_GROUP: case D_TABLE_FOOTER_GROUP:
            break;
        case D_TABLE_COLUMN: case D_TABLE_COLUMN_GROUP:
            break;
        default:
            wrap_inline_runs(&sub, b); break;
        }
    }
    set_bfc(b);
    return b;
}

static void fix_parents(struct box *b) {
    for (struct box *c = b->first; c; c = c->next) { c->parent = b; fix_parents(c); }
}

struct box *layout_build(struct page *p) {
    p->focus_count = 0;
    struct bstate bs;
    bs.p = p; bs.focus = -1;
    for (struct node *n = p->root; n; n = n->first ? n->first : NULL) { (void)n; break; }
    struct node *root = p->root;
    struct box *rb = NULL;
    if (root && root->style) {
        struct style *s = root->style;
        rb = box_new(p, root, s, BT_BLOCK);
        rb->bfc = true;
        build_children(&bs, rb, root);
        wrap_inline_runs(&bs, rb);
        // the root's display type decides nothing here: it is always a block
        fix_parents(rb);
        rb->parent = NULL;
    }
    p->root_box = rb;
    return rb;
}
