// web_layout.c - sizes and places boxes: blocks, floats, positioned boxes, replaced things.

#include "web_lay.h"

#define INF 0x3fffffff

// ---- Small helpers ----

int len_to_px(const struct len *l, int base, int def) {
    if (l->kind != L_VAL) return def;
    if ((l->pct || l->expr) && base < 0) {
        if (l->expr) return def;
        if (l->pct) return def;
    }
    return len_resolve(l, base < 0 ? 0 : base);
}

static bool len_is_px(const struct len *l) { return l->kind == L_VAL && l->pct == 0 && !l->expr; }
static bool len_definite(const struct len *l, int base) {
    if (l->kind != L_VAL) return false;
    if ((l->pct || l->expr) && base < 0) return false;
    return true;
}

int font_for(const struct style *s) { return wfont_pick(s->font_family, s->font_weight >= 600, s->font_italic != 0); }

int box_font_height(const struct style *s, int *ascent, int *descent) {
    int font = font_for(s);
    int a, d, g;
    wfont_vmetrics(font, s->font_size, &a, &d, &g);
    int h;
    if (s->line_height_pct > 0) h = (s->font_size * s->line_height_pct + 50) / 100;
    else if (s->line_height_px > 0) h = s->line_height_px;
    else h = a + d + g;
    if (ascent) *ascent = a;
    if (descent) *descent = d;
    return h;
}

bool box_is_container(const struct box *b) {
    if (b->type == BT_INLINE || b->type == BT_TEXT || b->type == BT_BR) return false;
    return b->style->position != POS_STATIC || b->style->has_transform;
}

static int mcollapse(int a, int b) {
    if (a >= 0 && b >= 0) return a > b ? a : b;
    if (a < 0 && b < 0) return a < b ? a : b;
    return a + b;
}

struct mm { int pos, neg; };
static void mm_add(struct mm *m, int v) { if (v > m->pos) m->pos = v; if (v < m->neg) m->neg = v; }
static int mm_val(const struct mm *m) { return m->pos + m->neg; }

// ---- Floats ----

void fl_add(struct fctx *f, int x, int y, int w, int h, bool left) {
    if (f->count == f->cap) {
        int nc = f->cap ? f->cap * 2 : 16;
        struct fl_item *ni = realloc(f->items, sizeof(struct fl_item) * (size_t)nc);
        if (!ni) return;
        f->items = ni; f->cap = nc;
    }
    f->items[f->count++] = (struct fl_item){ x, y, w, h, left };
    if (left) { if (y + h > f->clear_left) f->clear_left = y + h; }
    else { if (y + h > f->clear_right) f->clear_right = y + h; }
}

void fl_edges(const struct fctx *f, int y, int h, int lo, int hi, int *left, int *right) {
    int l = lo, r = hi;
    if (h < 1) h = 1;
    for (int i = 0; i < f->count; i++) {
        const struct fl_item *it = &f->items[i];
        if (it->y >= y + h || it->y + it->h <= y) continue;
        if (it->w == 0) continue;
        if (it->left) { if (it->x + it->w > l) l = it->x + it->w; }
        else { if (it->x < r) r = it->x; }
    }
    *left = l; *right = r;
}

int fl_next_edge(const struct fctx *f, int y) {
    int best = INF;
    for (int i = 0; i < f->count; i++) {
        int bot = f->items[i].y + f->items[i].h;
        if (bot > y && bot < best) best = bot;
    }
    return best;
}

int fl_clear_y(const struct fctx *f, int y, int clear) {
    if ((clear == CL_LEFT || clear == CL_BOTH) && f->clear_left > y) y = f->clear_left;
    if ((clear == CL_RIGHT || clear == CL_BOTH) && f->clear_right > y) y = f->clear_right;
    return y;
}

// ---- Edges ----

void lay_edges(struct box *b, int cb_w) {
    struct style *s = b->style;
    int base = cb_w < 0 ? 0 : cb_w;
    int *m = &b->margin.top, *pd = &b->pad.top, *bd = &b->border.top;
    // order: top, right, bottom, left (struct edges layout)
    for (int i = 0; i < 4; i++) {
        m[i] = s->margin[i].kind == L_VAL ? len_resolve(&s->margin[i], base) : 0;
        int pv = s->padding[i].kind == L_VAL ? len_resolve(&s->padding[i], base) : 0;
        pd[i] = pv < 0 ? 0 : pv;
        bd[i] = b->has_collapse_border ? b->collapse_border[i] : s->border_width[i];
    }
}

// ---- Replaced content ----

static void text_size(const struct style *s, const char *t, int len, int *w, int *h) {
    int font = font_for(s);
    *w = (wfont_text_width(font, s->font_size, t, len) + 32) / 64;
    int a, d;
    *h = box_font_height(s, &a, &d);
}

static int attr_int(const struct node *n, const char *name, int def) {
    const char *v = node_attr(n, name);
    if (!v) return def;
    while (w_is_space(*v)) v++;
    if (!w_is_digit(*v)) return def;
    int x = 0;
    while (w_is_digit(*v) && x < 100000) { x = x * 10 + (*v - '0'); v++; }
    return x;
}

static void replaced_natural(struct lay *L, struct box *b, int *nw, int *nh, bool *has_ratio) {
    struct node *n = b->node;
    struct style *s = b->style;
    *nw = *nh = 0; *has_ratio = false;
    switch (b->replaced) {
    case RK_IMAGE: {
        struct wimage *im = b->image;
        if (im && im->state == IMG_READY) { *nw = im->natural_w; *nh = im->natural_h; *has_ratio = *nw > 0 && *nh > 0; return; }
        int aw = attr_int(n, "width", 0), ah = attr_int(n, "height", 0);
        if (aw && ah) { *nw = aw; *nh = ah; *has_ratio = true; return; }
        if (im && im->state == IMG_FAILED && b->alt && *b->alt) {
            int w, h; text_size(s, b->alt, w_len(b->alt), &w, &h);
            *nw = w; *nh = h; return;
        }
        *nw = aw; *nh = ah;
        return;
    }
    case RK_SVG: {
        int w = 0, h = 0;
        if (b->intrinsic_w > 0) { *nw = b->intrinsic_w; *nh = b->intrinsic_h; *has_ratio = true; return; }
        w = attr_int(n, "width", 0); h = attr_int(n, "height", 0);
        *nw = w ? w : 300; *nh = h ? h : 150;
        *has_ratio = true;
        return;
    }
    case RK_BLANK: {
        *nw = attr_int(n, "width", 300); *nh = attr_int(n, "height", 150); *has_ratio = true; return;
    }
    case RK_INPUT_CHECK: case RK_INPUT_RADIO: *nw = 13; *nh = 13; return;
    case RK_INPUT_TEXT: {
        int font = font_for(s);
        int cw = (wfont_advance(font, s->font_size, '0') + 32) / 64;
        int size = attr_int(n, "size", 20);
        if (size <= 0) size = 20;
        *nw = cw * size + 0;
        int a, d; *nh = box_font_height(s, &a, &d);
        const char *type = node_input_type(n);
        if (w_eq0(type, "number")) *nw += 15;
        if (w_eq0(type, "date") || w_eq0(type, "time") || w_eq0(type, "datetime-local")) *nw = cw * 14;
        return;
    }
    case RK_RANGE: *nw = 129; *nh = 16; return;
    case RK_COLOR: *nw = 50; *nh = 27; return;
    case RK_TEXTAREA: {
        int font = font_for(s);
        int cols = attr_int(n, "cols", 20), rows = attr_int(n, "rows", 2);
        if (cols <= 0) cols = 20; if (rows <= 0) rows = 2;
        int cw = (wfont_advance(font, s->font_size, '0') + 32) / 64;
        int a, d; int lh = box_font_height(s, &a, &d);
        *nw = cw * cols + 15; *nh = lh * rows;
        return;
    }
    case RK_SELECT: {
        int mx = 0;
        int count = 0;
        for (struct node *c = n->first; c; c = c->next) {
            if (c->type != N_ELEMENT) continue;
            struct node *o = c;
            for (int pass = 0; pass < 2; pass++) {
                if (pass == 0 && o->tag != T_OPTGROUP) { pass = 1; }
                if (pass == 0) {
                    for (struct node *oo = o->first; oo; oo = oo->next) {
                        if (oo->type == N_ELEMENT && oo->tag == T_OPTION) { char tb[200]; int tl = node_text(oo, tb, 200); int w, h; text_size(s, tb, tl, &w, &h); if (w > mx) mx = w; count++; }
                    }
                    break;
                } else if (o->tag == T_OPTION) { char tb[200]; int tl = node_text(o, tb, 200); int w, h; text_size(s, tb, tl, &w, &h); if (w > mx) mx = w; count++; break; }
            }
        }
        int a, d; int lh = box_font_height(s, &a, &d);
        int size = attr_int(n, "size", 1);
        *nw = mx + 24; *nh = size > 1 ? lh * size : lh;
        return;
    }
    }
}

void lay_replaced_size(struct lay *L, struct box *b, int cb_w, int cb_h, int *w, int *h) {
    struct style *s = b->style;
    int nw, nh; bool ratio;
    replaced_natural(L, b, &nw, &nh, &ratio);
    int pw = (s->box_sizing == 1 ? 0 : b->pad.left + b->pad.right + b->border.left + b->border.right);
    int ph = (s->box_sizing == 1 ? 0 : b->pad.top + b->pad.bottom + b->border.top + b->border.bottom);
    if (s->box_sizing == 2) { pw = b->pad.left + b->pad.right; ph = b->pad.top + b->pad.bottom; }
    bool wdef = len_definite(&s->width, cb_w), hdef = len_definite(&s->height, cb_h);
    int ew = 0, eh = 0;
    if (wdef) { ew = len_to_px(&s->width, cb_w, 0) ; if (s->box_sizing == 1) ew -= (b->pad.left + b->pad.right + b->border.left + b->border.right); }
    if (hdef) { eh = len_to_px(&s->height, cb_h, 0); if (s->box_sizing == 1) eh -= (b->pad.top + b->pad.bottom + b->border.top + b->border.bottom); }
    (void)pw; (void)ph;
    if (ew < 0) ew = 0; if (eh < 0) eh = 0;
    int rw = nw, rh = nh;
    // CSS aspect-ratio overrides the picture's own ratio
    int ar_w = s->aspect_w, ar_h = s->aspect_h;
    bool use_ratio = ratio && nw > 0 && nh > 0;
    if (ar_w > 0 && ar_h > 0) { use_ratio = true; }
    int rat_w = ar_w > 0 ? ar_w : nw, rat_h = ar_h > 0 ? ar_h : nh;
    if (wdef && hdef) { rw = ew; rh = eh; }
    else if (wdef) { rw = ew; rh = use_ratio ? (int)((long long)ew * rat_h / (rat_w ? rat_w : 1)) : (nh ? nh : 0); if (!use_ratio && nw == 0) rh = nh; }
    else if (hdef) { rh = eh; rw = use_ratio ? (int)((long long)eh * rat_w / (rat_h ? rat_h : 1)) : nw; }
    else {
        rw = nw; rh = nh;
        if (ar_w > 0 && ar_h > 0 && nw == 0) { rw = cb_w > 0 ? cb_w : 0; rh = (int)((long long)rw * ar_h / ar_w); }
    }
    // min / max
    int maxw = len_definite(&s->max_width, cb_w) && s->max_width.kind == L_VAL ? len_to_px(&s->max_width, cb_w, INF) : INF;
    int minw = len_definite(&s->min_width, cb_w) ? len_to_px(&s->min_width, cb_w, 0) : 0;
    int maxh = len_definite(&s->max_height, cb_h) && s->max_height.kind == L_VAL ? len_to_px(&s->max_height, cb_h, INF) : INF;
    int minh = len_definite(&s->min_height, cb_h) ? len_to_px(&s->min_height, cb_h, 0) : 0;
    if (s->box_sizing == 1) {
        int ex = b->pad.left + b->pad.right + b->border.left + b->border.right, ey = b->pad.top + b->pad.bottom + b->border.top + b->border.bottom;
        if (maxw != INF) maxw -= ex; minw -= ex; if (maxh != INF) maxh -= ey; minh -= ey;
    }
    int ow = rw, oh = rh;
    if (rw > maxw) { rw = maxw; if (!hdef && use_ratio && ow) rh = (int)((long long)oh * rw / ow); }
    if (rh > maxh) { rh = maxh; if (!wdef && use_ratio && oh) rw = (int)((long long)ow * rh / oh); }
    if (rw < minw) { int w0 = rw; rw = minw; if (!hdef && use_ratio && w0 > 0) rh = (int)((long long)rh * rw / w0); }
    if (rh < minh) { int h0 = rh; rh = minh; if (!wdef && use_ratio && h0 > 0) rw = (int)((long long)rw * rh / h0); }
    if (rw < 0) rw = 0; if (rh < 0) rh = 0;
    *w = rw; *h = rh;
}

// ---- Intrinsic widths ----

static int outer_margin_w(struct box *c, int base) {
    struct style *s = c->style;
    int l = s->margin[3].kind == L_VAL && !s->margin[3].pct && !s->margin[3].expr ? s->margin[3].px : 0;
    int r = s->margin[1].kind == L_VAL && !s->margin[1].pct && !s->margin[1].expr ? s->margin[1].px : 0;
    (void)base;
    return l + r;
}

void lay_intrinsic(struct lay *L, struct box *b) {
    if (b->intr_done) return;
    b->intr_done = true;
    struct style *s = b->style;
    lay_edges(b, 0);
    int ex = b->pad.left + b->pad.right + b->border.left + b->border.right;
    int mn = 0, mx = 0;
    if (b->type == BT_REPLACED) {
        int w, h;
        lay_replaced_size(L, b, -1, -1, &w, &h);
        mn = mx = w;
        if (s->width.kind == L_VAL && s->width.pct) {
            // a percentage width: the picture can shrink to nothing
            int nw, nh; bool r; replaced_natural(L, b, &nw, &nh, &r);
            mn = 0; mx = nw;
        } else if (s->max_width.kind == L_VAL && s->max_width.pct) { mn = 0; }
    } else if (b->type == BT_TEXT || b->type == BT_BR || b->type == BT_INLINE) {
        intr_inline(L, b, &mn, &mx);
    } else if (s->display == D_TABLE || s->display == D_INLINE_TABLE) {
        intr_table(L, b, &mn, &mx);
    } else if (s->display == D_FLEX || s->display == D_INLINE_FLEX) {
        intr_flex(L, b, &mn, &mx);
    } else if (s->display == D_GRID || s->display == D_INLINE_GRID) {
        intr_grid(L, b, &mn, &mx);
    } else if (b->ifc) {
        intr_inline(L, b, &mn, &mx);
    } else {
        for (struct box *c = b->first; c; c = c->next) {
            if (c->out_of_flow && !c->is_float) continue;
            lay_intrinsic(L, c);
            int om = outer_margin_w(c, 0);
            if (c->is_float) {
                // floats sit side by side if they fit: their widths add up in max-content
                mx += c->max_content + om;
                if (c->min_content + om > mn) mn = c->min_content + om;
                continue;
            }
            if (c->min_content + om > mn) mn = c->min_content + om;
            if (c->max_content + om > mx) mx = c->max_content + om;
        }
        // floats beside block content: adjust (rare): max is at least the widest float
    }
    // a width given in pixels decides
    if (b->type != BT_REPLACED && len_is_px(&s->width)) {
        int w = s->width.px;
        if (s->box_sizing == 1) w -= ex; else if (s->box_sizing == 2) w -= b->pad.left + b->pad.right;
        if (w < 0) w = 0;
        mn = mx = w;
    }
    // min-width / max-width in pixels
    if (len_is_px(&s->max_width)) {
        int w = s->max_width.px;
        if (s->box_sizing == 1) w -= ex;
        if (mx > w) mx = w;
        if (mn > w) mn = w;
    }
    if (len_is_px(&s->min_width)) {
        int w = s->min_width.px;
        if (s->box_sizing == 1) w -= ex;
        if (mx < w) mx = w;
        if (mn < w) mn = w;
    }
    if (mn < 0) mn = 0; if (mx < mn) mx = mn;
    b->min_content = mn + ex;
    b->max_content = mx + ex;
    if (b->type == BT_TEXT || b->type == BT_BR || b->type == BT_INLINE) { b->min_content = mn; b->max_content = mx; }
}

// ---- Baselines ----

int lay_baseline(const struct box *b) {
    if (b->type == BT_REPLACED) return -1;
    if (b->style->overflow_x != OV_VISIBLE && b->type == BT_BLOCK && b->inline_level) return -1;
    if (b->ifc) {
        if (b->line_count > 0) {
            const struct line *ln = &b->lines[b->line_count - 1];
            return b->border.top + b->pad.top + ln->y + ln->baseline;
        }
        return -1;
    }
    for (const struct box *c = b->last; c; ) {
        // last in-flow child with a baseline
        if (c->out_of_flow) { c = NULL; for (const struct box *k = b->first; k; k = k->next) { if (!k->out_of_flow) c = k; } if (!c) return -1; }
        int bl = lay_baseline(c);
        if (bl >= 0) return c->y + bl;
        const struct box *prev = NULL;
        for (const struct box *k = b->first; k && k != c; k = k->next) if (!k->out_of_flow) prev = k;
        c = prev;
        if (!c) return -1;
    }
    return -1;
}

// ---- Laying out one box ----

struct flowres { int content_h; int escaped; };

static int top_margin_eff(struct lay *L, struct box *b, int cb_w);

static bool no_top_gap(struct box *b, int cb_w) {
    struct style *s = b->style;
    if (s->border_width[0] != 0) return false;
    if (s->padding[0].kind == L_VAL && len_resolve(&s->padding[0], cb_w < 0 ? 0 : cb_w) != 0) return false;
    return true;
}

static struct box *first_inflow(struct box *b) {
    for (struct box *c = b->first; c; c = c->next) if (!c->out_of_flow) return c;
    return NULL;
}

static int top_margin_eff(struct lay *L, struct box *b, int cb_w) {
    lay_edges(b, cb_w);
    b->collapse_first = false;
    int m = b->margin.top;
    struct style *s = b->style;
    if (b->type == BT_BLOCK && !b->bfc && !b->ifc && b->first && no_top_gap(b, cb_w)
        && s->display != D_TABLE_ROW && s->display != D_TABLE_CELL && s->display != D_FLEX && s->display != D_GRID) {
        struct box *c = first_inflow(b);
        if (c && box_is_block_level(c) && c->type == BT_BLOCK && !(c->style->clear)) {
            int cm = top_margin_eff(L, c, cb_w);
            b->collapse_first = true;
            m = mcollapse(m, cm);
        }
    }
    b->mt_eff = m;
    return m;
}

static void register_abs(struct lay *L, struct box *a, struct box *host, int sx, int sy) {
    a->sx = sx; a->sy = sy; a->shost = host;
    a->abs_next = NULL;
    struct box *cont = NULL;
    if (a->style->position == POS_FIXED) {
        cont = L->p->root_box;       // laid out against the viewport
        // fixed boxes are kept in the page's own list
        a->abs_next = NULL;
        if (L->p->fixed_last) L->p->fixed_last->abs_next = a; else L->p->fixed_first = a;
        L->p->fixed_last = a;
        return;
    }
    if (L->abs_depth > 0) cont = L->abs_stack[L->abs_depth - 1];
    else cont = L->p->root_box;
    if (cont->abs_last) cont->abs_last->abs_next = a; else cont->abs_first = a;
    cont->abs_last = a;
}

// Where is the content box of `host`, measured in the border box of `cont`?
static void offset_in_container(struct box *host, struct box *cont, int *ox, int *oy) {
    int x = host->border.left + host->pad.left, y = host->border.top + host->pad.top;
    for (struct box *k = host; k && k != cont; k = k->parent) {
        if (k != host) { x += k->border.left + k->pad.left; y += k->border.top + k->pad.top; }
        // k's own position inside its parent
        if (k->parent) { x += k->x; y += k->y; }
        else break;
    }
    *ox = x; *oy = y;
}

static void place_float(struct lay *L, struct box *c, struct fctx *f, int cox, int coy, int cw, int cur_y, int cb_h) {
    struct lin in;
    memset(&in, 0, sizeof(in));
    in.cb_w = cw; in.cb_h = cb_h; in.avail_w = cw; in.forced_w = -1; in.forced_h = -1; in.shrink = true;
    in.f = NULL; in.fx = 0; in.fy = 0;
    lay_box(L, c, &in);
    int ow = c->w + c->margin.left + c->margin.right;
    int oh = c->h + c->margin.top + c->margin.bottom;
    int y = coy + cur_y;
    bool left = c->style->floating == FL_LEFT;
    // not above an earlier float
    for (int i = 0; i < f->count; i++) if (f->items[i].y > y) y = f->items[i].y;
    if (c->style->clear) y = fl_clear_y(f, y, c->style->clear);
    int guard = 0;
    for (;;) {
        int l, r;
        fl_edges(f, y, oh, cox, cox + cw, &l, &r);
        if (r - l >= ow || guard++ > 200) {
            int x = left ? l : r - ow;
            if (!left && r - l < ow) x = cox + cw - ow;
            fl_add(f, x, y, ow, oh, left);
            c->x = (x - cox) + c->margin.left;      // relative to the content origin; the caller adds the parent's offsets
            c->y = (y - coy) + c->margin.top;
            return;
        }
        int nx = fl_next_edge(f, y);
        if (nx >= INF) {
            int x = left ? cox : cox + cw - ow;
            fl_add(f, x, y, ow, oh, left);
            c->x = (x - cox) + c->margin.left; c->y = (y - coy) + c->margin.top;
            return;
        }
        y = nx;
    }
}

// The children of a block, one under the other.
static void flow_children(struct lay *L, struct box *b, const struct lin *in, struct fctx *f, int cox, int coy, int cw, int ch_def,
                          bool bottom_collapsible, struct flowres *res) {
    struct style *s = b->style;
    int cursor = 0;
    struct mm pending = { 0, 0 };
    bool first = true;
    int offx = b->border.left + b->pad.left, offy = b->border.top + b->pad.top;
    for (struct box *c = b->first; c; c = c->next) {
        if (c->out_of_flow) {
            int y_now = cursor + mm_val(&pending);
            if (c->is_float) {
                place_float(L, c, f, cox, coy, cw, y_now, ch_def);
                c->x += offx; c->y += offy;
            } else {
                register_abs(L, c, b, 0, y_now);
            }
            continue;
        }
        int mt;
        if (first && b->collapse_first) mt = 0;
        else mt = top_margin_eff(L, c, cw);
        if (first && b->collapse_first) top_margin_eff(L, c, cw);   // still resolve its edges
        int mb_own = c->margin.bottom;
        // clearance
        int y;
        struct mm p2 = pending;
        mm_add(&p2, mt);
        y = cursor + mm_val(&p2);
        if (c->style->clear) {
            int yc = fl_clear_y(f, coy + y, c->style->clear) - coy;
            if (yc > y) { y = yc; }
        }
        // empty boxes: margins pass straight through
        bool empty = c->type == BT_BLOCK && !c->first && !c->ifc && c->style->height.kind == L_AUTO && !c->bfc
            && c->style->min_height.kind != L_VAL && c->pad.top == 0 && c->pad.bottom == 0 && c->border.top == 0 && c->border.bottom == 0
            && c->style->display != D_TABLE_CELL && !c->style->aspect_w;
        if (empty) {
            mm_add(&pending, mt);
            mm_add(&pending, c->margin.bottom);
            c->x = offx + c->margin.left; c->y = offy + cursor + mm_val(&pending) - c->margin.bottom;
            c->w = cw - c->margin.left - c->margin.right; c->h = 0;
            if (c->w < 0) c->w = 0;
            first = false;
            continue;
        }
        struct lin ci;
        memset(&ci, 0, sizeof(ci));
        ci.cb_w = cw; ci.cb_h = ch_def; ci.avail_w = cw; ci.forced_w = -1; ci.forced_h = -1; ci.f = f;
        ci.fx = cox; ci.fy = coy + y;
        int cwidth = cw;
        // a block that makes its own formatting context may not overlap floats beside it
        if (c->bfc && !c->is_float && f->count > 0 && c->type == BT_BLOCK && !c->inline_level) {
            int l, r;
            fl_edges(f, coy + y, 1, cox, cox + cw, &l, &r);
            if (l > cox || r < cox + cw) {
                // too narrow for a block with a set width: below the floats
                struct style *cs = c->style;
                int need = cs->width.kind == L_VAL && !cs->width.pct && !cs->width.expr ? cs->width.px + c->margin.left + c->margin.right : 0;
                if (need > r - l) {
                    int ny = fl_next_edge(f, coy + y);
                    int guard = 0;
                    while (ny < INF && guard++ < 100) {
                        fl_edges(f, ny, 1, cox, cox + cw, &l, &r);
                        if (r - l >= need) break;
                        ny = fl_next_edge(f, ny);
                    }
                    if (ny < INF) { y = ny - coy; ci.fy = coy + y; fl_edges(f, ny, 1, cox, cox + cw, &l, &r); }
                }
                ci.fx = l; ci.cb_w = r - l; ci.avail_w = r - l;
                cwidth = r - l;
                // percentages stay relative to the real containing block
            }
        }
        (void)cwidth;
        lay_box(L, c, &ci);
        c->x = offx + (ci.fx - cox) + c->margin.left;
        c->y = offy + y;
        cursor = y + c->h;
        pending.pos = pending.neg = 0;
        int eb = mcollapse(mb_own, c->escaped_bottom);
        mm_add(&pending, eb);
        first = false;
    }
    if (bottom_collapsible) {
        res->content_h = cursor;
        res->escaped = mm_val(&pending);
    } else {
        res->content_h = cursor + mm_val(&pending);
        res->escaped = 0;
    }
    (void)s; (void)in;
}

static int apply_minmax(int v, int mn, int mx) { if (mx >= 0 && v > mx) v = mx; if (v < mn) v = mn; return v; }

void lay_box(struct lay *L, struct box *b, const struct lin *in) {
    struct style *s = b->style;
    int cb_w = in->cb_w;
    int d = s->display;
    lay_edges(b, cb_w);
    int ph = b->pad.left + b->pad.right, bh = b->border.left + b->border.right;
    int pv = b->pad.top + b->pad.bottom, bv = b->border.top + b->border.bottom;
    int extra_h = ph + bh, extra_v = pv + bv;
    bool replaced = b->type == BT_REPLACED;
    bool is_table = d == D_TABLE || d == D_INLINE_TABLE;
    bool auto_ml = s->margin[3].kind == L_AUTO, auto_mr = s->margin[1].kind == L_AUTO;
    bool auto_mt = s->margin[0].kind == L_AUTO, auto_mb = s->margin[2].kind == L_AUTO;
    (void)auto_mt; (void)auto_mb;

    // --- width ---
    int bw;
    int content_w;
    int ml = b->margin.left, mr = b->margin.right;
    int rw = 0, rh = 0;
    if (replaced) {
        lay_replaced_size(L, b, cb_w, in->cb_h, &rw, &rh);
        bw = rw + extra_h;
        if (s->box_sizing == 1 && (s->width.kind == L_VAL)) { /* already content-adjusted in lay_replaced_size */ }
    } else if (in->forced_w >= 0) {
        bw = in->forced_w;
    } else {
        const struct len *wl = &s->width;
        bool wdef = wl->kind == L_VAL && !((wl->pct || wl->expr) && cb_w < 0);
        int fit = 0;
        bool shrink = in->shrink || b->float_shrink_hint;
        if (wl->kind == L_MIN_CONTENT || wl->kind == L_MAX_CONTENT || wl->kind == L_FIT_CONTENT) {
            lay_intrinsic(L, b);
            int avail = (in->avail_w >= 0 ? in->avail_w : cb_w) - ml - mr;
            if (wl->kind == L_MIN_CONTENT) fit = b->min_content;
            else if (wl->kind == L_MAX_CONTENT) fit = b->max_content;
            else fit = b->max_content < avail ? b->max_content : (b->min_content > avail ? b->min_content : avail);
            bw = fit;
        } else if (wdef) {
            int w = len_to_px(wl, cb_w, 0);
            if (s->box_sizing == 0) w += extra_h; else if (s->box_sizing == 2) w += bh;
            bw = w;
        } else if (shrink || is_table) {
            lay_intrinsic(L, b);
            int avail = (in->avail_w >= 0 ? in->avail_w : cb_w) - ml - mr;
            if (avail < 0) avail = 0;
            int mn = b->min_content, mx = b->max_content;
            bw = mx < avail ? mx : avail;
            if (bw < mn) bw = mn;
        } else {
            bw = cb_w - ml - mr;
            if (bw < 0) bw = 0;
        }
        // min / max
        int minw = 0, maxw = -1;
        if (s->min_width.kind == L_VAL && !((s->min_width.pct || s->min_width.expr) && cb_w < 0)) {
            minw = len_to_px(&s->min_width, cb_w, 0);
            if (s->box_sizing == 0) minw += extra_h; else if (s->box_sizing == 2) minw += bh;
        } else if (s->min_width.kind == L_MIN_CONTENT) { lay_intrinsic(L, b); minw = b->min_content; }
        if (s->max_width.kind == L_VAL && !((s->max_width.pct || s->max_width.expr) && cb_w < 0)) {
            maxw = len_to_px(&s->max_width, cb_w, 0);
            if (s->box_sizing == 0) maxw += extra_h; else if (s->box_sizing == 2) maxw += bh;
        } else if (s->max_width.kind == L_MAX_CONTENT) { lay_intrinsic(L, b); maxw = b->max_content; }
        else if (s->max_width.kind == L_MIN_CONTENT) { lay_intrinsic(L, b); maxw = b->min_content; }
        else if (s->max_width.kind == L_FIT_CONTENT) { lay_intrinsic(L, b); int avail = cb_w - ml - mr; maxw = b->max_content < avail ? b->max_content : (b->min_content > avail ? b->min_content : avail); }
        bw = apply_minmax(bw, minw, maxw);
        if (bw < extra_h) bw = extra_h;
        // auto margins
        if (!in->shrink && !b->out_of_flow && in->forced_w < 0 && !b->inline_level) {
            int room = cb_w - bw - (auto_ml ? 0 : ml) - (auto_mr ? 0 : mr);
            if (auto_ml && auto_mr) { int h = room > 0 ? room / 2 : 0; ml = h; mr = room > 0 ? room - h : 0; }
            else if (auto_ml) { ml = room > 0 ? room : 0; }
            else if (auto_mr) { mr = room > 0 ? room : 0; }
        }
    }
    if (replaced && !(in->forced_w < 0)) { bw = in->forced_w; }
    if (in->forced_w >= 0 && replaced) bw = in->forced_w;
    b->margin.left = ml; b->margin.right = mr;
    content_w = bw - extra_h;
    if (content_w < 0) content_w = 0;
    b->w = bw;
    b->content_w = content_w;

    // --- contents ---
    int content_h = 0;
    b->escaped_bottom = 0;
    b->abs_first = b->abs_last = NULL;
    b->lines = NULL; b->line_count = 0; b->frags = NULL; b->frag_count = 0;
    bool container = box_is_container(b);
    if (container && L->abs_depth < 60) L->abs_stack[L->abs_depth++] = b; else container = false;

    // height information for children's percentages
    int ch_def = -1;
    {
        const struct len *hl = &s->height;
        if (in->forced_h >= 0) ch_def = in->forced_h - extra_v;
        else if (hl->kind == L_VAL && !((hl->pct || hl->expr) && in->cb_h < 0)) {
            int h = len_to_px(hl, in->cb_h, 0);
            if (s->box_sizing == 0) ch_def = h; else ch_def = h - extra_v + (s->box_sizing == 2 ? bv : 0) - (s->box_sizing == 2 ? 0 : 0);
            if (s->box_sizing == 2) ch_def = h - pv;
        }
        if (ch_def < -1) ch_def = 0;
    }

    struct fctx local;
    struct fctx *f = in->f;
    int cox, coy;
    bool own_bfc = b->bfc || !f || b->parent == NULL;
    if (own_bfc) {
        memset(&local, 0, sizeof(local));
        f = &local;
        cox = 0; coy = 0;
    } else {
        cox = in->fx + ml + b->border.left + b->pad.left;
        coy = in->fy + b->border.top + b->pad.top;
    }
    if (replaced) {
        content_h = rh;
    } else if (is_table) {
        lay_table(L, b, in, content_w);
        // lay_table sets b->content_w/b->w to the final table width and returns height through content_h field
        content_h = b->content_h;
        bw = b->w; content_w = b->content_w;
    } else if (d == D_FLEX || d == D_INLINE_FLEX) {
        lay_flex(L, b, in, content_w, &content_h);
    } else if (d == D_GRID || d == D_INLINE_GRID) {
        lay_grid(L, b, in, content_w, &content_h);
    } else if (b->ifc) {
        lay_inline(L, b, in, f, content_w, cox, coy, &content_h);
    } else if (b->first) {
        bool bottom_coll = false;
        if (!b->bfc && b->type == BT_BLOCK && extra_v - b->border.top - b->pad.top == 0 && s->height.kind == L_AUTO
            && (s->min_height.kind != L_VAL || len_to_px(&s->min_height, in->cb_h, 0) == 0) && in->forced_h < 0 && !s->aspect_w
            && d != D_TABLE_CELL && d != D_TABLE_ROW) bottom_coll = true;
        struct flowres fr;
        flow_children(L, b, in, f, cox, coy, content_w, ch_def, bottom_coll, &fr);
        content_h = fr.content_h;
        b->escaped_bottom = fr.escaped;
    }
    // a context that owns its floats grows to hold them
    if (own_bfc && !replaced) {
        if (f->clear_left > content_h && !(b->out_of_flow && !b->is_float && false)) content_h = f->clear_left > f->clear_right ? f->clear_left : content_h;
        if (f->clear_right > content_h) content_h = f->clear_right;
        if (f->clear_left > content_h) content_h = f->clear_left;
    }

    // --- height ---
    int bhgt;
    {
        const struct len *hl = &s->height;
        bool hdef = hl->kind == L_VAL && !((hl->pct || hl->expr) && in->cb_h < 0);
        if (in->forced_h >= 0) bhgt = in->forced_h;
        else if (replaced) bhgt = content_h + extra_v;
        else if (hdef) {
            int h = len_to_px(hl, in->cb_h, 0);
            if (s->box_sizing == 0) h += extra_v; else if (s->box_sizing == 2) h += bv;
            bhgt = h;
        } else if (hl->kind == L_MIN_CONTENT || hl->kind == L_MAX_CONTENT || hl->kind == L_FIT_CONTENT) bhgt = content_h + extra_v;
        else {
            bhgt = content_h + extra_v;
            if (s->aspect_w > 0 && s->aspect_h > 0) {
                int rhh = (int)((long long)content_w * s->aspect_h / s->aspect_w) + extra_v;
                if (rhh > bhgt) bhgt = rhh;
            }
        }
        if (in->forced_h < 0) {
            int minh = 0, maxh = -1;
            if (s->min_height.kind == L_VAL && !((s->min_height.pct || s->min_height.expr) && in->cb_h < 0)) {
                minh = len_to_px(&s->min_height, in->cb_h, 0);
                if (s->box_sizing == 0) minh += extra_v; else if (s->box_sizing == 2) minh += bv;
            }
            if (s->max_height.kind == L_VAL && !((s->max_height.pct || s->max_height.expr) && in->cb_h < 0)) {
                maxh = len_to_px(&s->max_height, in->cb_h, 0);
                if (s->box_sizing == 0) maxh += extra_v; else if (s->box_sizing == 2) maxh += bv;
            }
            bhgt = apply_minmax(bhgt, minh, maxh);
        }
        if (bhgt < extra_v) bhgt = extra_v;
    }
    b->h = bhgt;
    b->content_h = bhgt - extra_v;
    if (b->content_h < 0) b->content_h = 0;
    b->w = bw; b->content_w = bw - extra_h; if (b->content_w < 0) b->content_w = 0;
    // a box with a set height keeps its children's last margin inside
    if (bhgt != content_h + extra_v && b->escaped_bottom) {
        // the height was fixed after the fact: margin can't escape; it was not included, add nothing
        b->escaped_bottom = 0;
    }

    // --- out-of-flow children ---
    if (container) {
        L->abs_depth--;
        lay_abs_children(L, b);
    }
    // --- relative offset and translate ---
    b->rel_x = b->rel_y = 0;
    if (s->position == POS_RELATIVE) {
        if (s->inset[3].kind == L_VAL) b->rel_x = len_to_px(&s->inset[3], cb_w, 0);
        else if (s->inset[1].kind == L_VAL) b->rel_x = -len_to_px(&s->inset[1], cb_w, 0);
        if (s->inset[0].kind == L_VAL) b->rel_y = len_to_px(&s->inset[0], in->cb_h, 0);
        else if (s->inset[2].kind == L_VAL) b->rel_y = -len_to_px(&s->inset[2], in->cb_h, 0);
    }
    if (s->has_transform) {
        b->rel_x += len_to_px(&s->translate_x, b->w, 0);
        b->rel_y += len_to_px(&s->translate_y, b->h, 0);
    }
    if (own_bfc && local.items) free(local.items);
}

// ---- Out-of-flow boxes ----

static void lay_one_abs(struct lay *L, struct box *a, struct box *cont, int cbw, int cbh, bool viewport) {
    struct style *s = a->style;
    lay_edges(a, cbw);
    const struct len *left = &s->inset[3], *right = &s->inset[1], *top = &s->inset[0], *bottom = &s->inset[2];
    bool has_l = left->kind == L_VAL, has_r = right->kind == L_VAL, has_t = top->kind == L_VAL, has_b = bottom->kind == L_VAL;
    int l = has_l ? len_to_px(left, cbw, 0) : 0, r = has_r ? len_to_px(right, cbw, 0) : 0;
    int t = has_t ? len_to_px(top, cbh, 0) : 0, bt = has_b ? len_to_px(bottom, cbh, 0) : 0;
    bool auto_ml = s->margin[3].kind == L_AUTO, auto_mr = s->margin[1].kind == L_AUTO;
    struct lin in;
    memset(&in, 0, sizeof(in));
    in.cb_w = cbw; in.cb_h = cbh; in.avail_w = cbw; in.forced_w = -1; in.forced_h = -1; in.shrink = true;
    int extra_h = a->pad.left + a->pad.right + a->border.left + a->border.right;
    bool wauto = s->width.kind == L_AUTO && a->type != BT_REPLACED;
    if (wauto && has_l && has_r) {
        in.forced_w = cbw - l - r - a->margin.left - a->margin.right;
        if (in.forced_w < extra_h) in.forced_w = extra_h;
        // min/max still apply
        in.shrink = false;
    } else {
        in.avail_w = cbw - (has_l ? l : 0) - (has_r ? r : 0);
        if (in.avail_w < 0) in.avail_w = 0;
        in.avail_w += a->margin.left + a->margin.right;       // lay_box subtracts margins again
    }
    int extra_v = a->pad.top + a->pad.bottom + a->border.top + a->border.bottom;
    if (s->height.kind == L_AUTO && has_t && has_b && a->type != BT_REPLACED) {
        in.forced_h = cbh - t - bt - a->margin.top - a->margin.bottom;
        if (in.forced_h < extra_v) in.forced_h = extra_v;
    }
    lay_box(L, a, &in);
    int ml = a->margin.left, mr = a->margin.right;
    // horizontal position
    int x;
    if (has_l) {
        x = l + ml;
        if (has_r && !wauto) {
            if (auto_ml || auto_mr) {
                int room = cbw - l - r - a->w;
                if (auto_ml && auto_mr) { x = l + room / 2; }
                else if (auto_ml) { x = l + room - mr; }
            }
        }
    } else if (has_r) x = cbw - r - a->w - mr;
    else {
        // static position
        int ox, oy;
        offset_in_container(a->shost, cont, &ox, &oy);
        x = ox + a->sx + ml;
        if (auto_ml && auto_mr) { x = (cbw - a->w) / 2; }
        (void)oy;
    }
    int y;
    int mt = a->margin.top, mb = a->margin.bottom;
    if (has_t) {
        y = t + mt;
        if (has_b && s->height.kind != L_AUTO && (s->margin[0].kind == L_AUTO || s->margin[2].kind == L_AUTO)) {
            int room = cbh - t - bt - a->h;
            if (s->margin[0].kind == L_AUTO && s->margin[2].kind == L_AUTO) y = t + room / 2;
        }
    } else if (has_b) y = cbh - bt - a->h - mb;
    else {
        int ox, oy;
        offset_in_container(a->shost, cont, &ox, &oy);
        y = oy + a->sy + mt;
    }
    // x,y here are relative to the container's padding box; store relative to its border box
    a->x = x + (viewport ? 0 : cont->border.left);
    a->y = y + (viewport ? 0 : cont->border.top);
}

void lay_abs_children(struct lay *L, struct box *b) {
    int cbw = b->w - b->border.left - b->border.right;
    int cbh = b->h - b->border.top - b->border.bottom;
    for (struct box *a = b->abs_first; a; ) {
        struct box *nx = a->abs_next;
        lay_one_abs(L, a, b, cbw, cbh, false);
        a = nx;
    }
}

// ---- Page level ----

static void extent(struct box *b, int ax, int ay, int *maxw, int *maxh, int depth) {
    int x = ax + b->x, y = ay + b->y;
    if (b->type != BT_TEXT && b->type != BT_INLINE) {
        if (x + b->w > *maxw) *maxw = x + b->w;
        if (y + b->h > *maxh) *maxh = y + b->h;
    }
    if (depth > 300) return;
    if (b->style->overflow_x != OV_VISIBLE && b->parent) return;      // clipped: content doesn't make the page bigger
    for (struct box *c = b->first; c; c = c->next) {
        if (c->type == BT_TEXT || c->type == BT_INLINE) continue;
        int cx = x, cy = y;
        if (b->type == BT_BLOCK) { }
        extent(c, cx, cy, maxw, maxh, depth + 1);
    }
    for (struct box *a = b->abs_first; a; a = a->abs_next) {
        // positioned relative to b's border box
        extent(a, x - a->x + a->x, y - a->y + a->y, maxw, maxh, depth + 1);
    }
}

void layout_page(struct page *p, int width, int height) {
    struct lay L;
    memset(&L, 0, sizeof(L));
    L.p = p; L.vw = width; L.vh = height;
    p->viewport_w = width; p->viewport_h = height;
    p->fixed_first = p->fixed_last = NULL;
    struct box *rb = layout_build(p);
    if (!rb) { p->content_w = width; p->content_h = height; return; }
    struct lin in;
    memset(&in, 0, sizeof(in));
    in.cb_w = width; in.cb_h = height; in.avail_w = width; in.forced_w = -1; in.forced_h = -1; in.shrink = false; in.f = NULL;
    L.abs_stack[0] = rb; L.abs_depth = 1;
    // the html element fills the window width, with no margins of its own mattering
    rb->style->margin[1].kind = rb->style->margin[3].kind = L_VAL;
    lay_box(&L, rb, &in);
    rb->x = 0; rb->y = 0;
    L.abs_depth = 0;
    // fixed boxes: against the window
    for (struct box *a = p->fixed_first; a; ) {
        struct box *nx = a->abs_next;
        lay_one_abs(&L, a, rb, width, height, true);
        a = nx;
    }
    int mw = rb->w, mh = rb->h;
    extent(rb, 0, 0, &mw, &mh, 0);
    p->content_w = mw > width ? mw : width;
    p->content_h = mh;
}

// Used by inline layout to hand an out-of-flow box to its container.
void web_register_abs(struct lay *L, struct box *a, struct box *host, int sx, int sy) { register_abs(L, a, host, sx, sy); }
