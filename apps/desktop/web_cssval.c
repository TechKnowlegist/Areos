// web_cssval.c - reading CSS values: lengths, colors, numbers, calc().

#include "web_css.h"

// ---- Splitting a value into pieces ----

void vp_init(struct vp *v, const char *s, int len) { v->s = s; v->len = len; v->pos = 0; }

static bool ident_start(int c) { return w_is_alpha(c) || c == '_' || c >= 0x80 || c == '\\'; }
static bool ident_char(int c) { return w_is_alnum(c) || c == '_' || c == '-' || c >= 0x80 || c == '\\'; }

static bool vp_scan(struct vp *v, struct vtoken *t, bool advance) {
    int p = v->pos;
    const char *s = v->s;
    while (p < v->len && (w_is_space(s[p]))) p++;
    t->s = NULL; t->n = 0; t->num = 0; t->unit = NULL; t->unit_len = 0; t->delim = 0;
    if (p >= v->len) { t->kind = V_END; if (advance) v->pos = p; return false; }
    int c = (unsigned char)s[p];
    int start = p;
    // numbers (with sign)
    bool starts_number = w_is_digit(c) || (c == '.' && p + 1 < v->len && w_is_digit(s[p + 1]))
        || ((c == '-' || c == '+') && p + 1 < v->len && (w_is_digit(s[p + 1]) || (s[p + 1] == '.' && p + 2 < v->len && w_is_digit(s[p + 2]))));
    if (starts_number) {
        int sign = 1;
        if (c == '-') { sign = -1; p++; } else if (c == '+') p++;
        long long whole = 0;
        while (p < v->len && w_is_digit(s[p])) { whole = whole * 10 + (s[p] - '0'); if (whole > 100000000) whole = 100000000; p++; }
        long long frac = 0; int fd = 0;
        if (p < v->len && s[p] == '.' && p + 1 < v->len && w_is_digit(s[p + 1])) {
            p++;
            while (p < v->len && w_is_digit(s[p])) { if (fd < 3) { frac = frac * 10 + (s[p] - '0'); fd++; } else if (fd == 3 && s[p] >= '5' && false) {} p++; }
        }
        while (fd < 3) { frac *= 10; fd++; }
        long long val = whole * 1000 + frac;
        // exponent
        if (p + 1 < v->len && (s[p] == 'e' || s[p] == 'E') && (w_is_digit(s[p + 1]) || ((s[p + 1] == '-' || s[p + 1] == '+') && p + 2 < v->len && w_is_digit(s[p + 2])))) {
            int q = p + 1, esign = 1, e = 0;
            if (s[q] == '-') { esign = -1; q++; } else if (s[q] == '+') q++;
            while (q < v->len && w_is_digit(s[q])) { e = e * 10 + (s[q] - '0'); if (e > 30) e = 30; q++; }
            p = q;
            for (int i = 0; i < e; i++) { if (esign > 0) val *= 10; else val /= 10; if (val > 1000000000LL) val = 1000000000LL; }
        }
        if (val > 2000000000LL) val = 2000000000LL;
        t->num = (int)(sign * val);
        if (p < v->len && s[p] == '%') { t->kind = V_PCT; p++; }
        else if (p < v->len && ident_start((unsigned char)s[p])) {
            int u = p;
            while (p < v->len && ident_char((unsigned char)s[p])) p++;
            t->kind = V_DIM; t->unit = s + u; t->unit_len = p - u;
        } else t->kind = V_NUM;
        if (advance) v->pos = p;
        return true;
    }
    if (ident_start(c) || (c == '-' && p + 1 < v->len && (ident_start((unsigned char)s[p + 1]) || s[p + 1] == '-'))) {
        while (p < v->len && (ident_char((unsigned char)s[p]) || (s[p] == '-'))) p++;
        t->s = s + start; t->n = p - start;
        if (p < v->len && s[p] == '(') {
            t->kind = V_FUNC;
            p++;
        } else t->kind = V_IDENT;
        if (advance) v->pos = p;
        return true;
    }
    if (c == '"' || c == '\'') {
        int q = p + 1;
        while (q < v->len && s[q] != c) { if (s[q] == '\\' && q + 1 < v->len) q++; q++; }
        t->kind = V_STR; t->s = s + p + 1; t->n = q - p - 1;
        p = q < v->len ? q + 1 : q;
        if (advance) v->pos = p;
        return true;
    }
    if (c == '#') {
        p++;
        int h = p;
        while (p < v->len && ident_char((unsigned char)s[p])) p++;
        t->kind = V_HASH; t->s = s + h; t->n = p - h;
        if (advance) v->pos = p;
        return true;
    }
    p++;
    if (c == ',') t->kind = V_COMMA;
    else if (c == '/') t->kind = V_SLASH;
    else { t->kind = V_DELIM; t->delim = c; }
    if (advance) v->pos = p;
    return true;
}

bool vp_next(struct vp *v, struct vtoken *t) { return vp_scan(v, t, true); }
bool vp_peek(struct vp *v, struct vtoken *t) { return vp_scan(v, t, false); }

void vp_func_args(struct vp *v, struct vp *inside) {
    int depth = 1, p = v->pos;
    const char *s = v->s;
    int start = p;
    while (p < v->len) {
        char c = s[p];
        if (c == '"' || c == '\'') {
            int q = p + 1;
            while (q < v->len && s[q] != c) { if (s[q] == '\\' && q + 1 < v->len) q++; q++; }
            p = q + 1;
            continue;
        }
        if (c == '(') depth++;
        else if (c == ')') { depth--; if (depth == 0) break; }
        p++;
    }
    if (p > v->len) p = v->len;
    inside->s = s + start;
    inside->len = p - start;
    inside->pos = 0;
    v->pos = p < v->len ? p + 1 : p;
}

bool tok_is(const struct vtoken *t, const char *ident) {
    return (t->kind == V_IDENT || t->kind == V_FUNC) && w_eq(t->s, t->n, ident);
}

int tok_keyword(const struct vtoken *t, const char *const *names) {
    if (t->kind != V_IDENT) return -1;
    for (int i = 0; names[i]; i++) if (w_eq(t->s, t->n, names[i])) return i;
    return -1;
}

// ---- Lengths ----

static struct len len_px(int px) { struct len l = { px, 0, L_VAL, NULL }; return l; }

static struct lexpr *new_expr(const struct css_ctx *cx, int op) {
    struct lexpr *e = w_alloc(&cx->page->larena, sizeof(struct lexpr));
    if (e) e->op = (uint8_t)op;
    return e;
}

static struct len len_from_expr(struct lexpr *e) {
    struct len l = { 0, 0, L_VAL, e };
    return l;
}

static bool len_is_plain(const struct len *l) { return l->kind == L_VAL && !l->expr; }

static struct lexpr *leaf_expr(const struct css_ctx *cx, const struct len *l) {
    struct lexpr *e = new_expr(cx, LE_LEN);
    if (e) e->leaf = *l;
    return e;
}

static struct len len_combine(const struct css_ctx *cx, int op, struct len a, struct len b) {
    if (len_is_plain(&a) && len_is_plain(&b) && (op == LE_ADD || op == LE_SUB)) {
        int sign = op == LE_ADD ? 1 : -1;
        struct len r = { a.px + sign * b.px, a.pct + sign * b.pct, L_VAL, NULL };
        return r;
    }
    struct lexpr *e = new_expr(cx, op);
    if (!e) return a;
    e->a = leaf_expr(cx, &a);
    e->b = leaf_expr(cx, &b);
    return len_from_expr(e);
}

static struct len len_scale(const struct css_ctx *cx, struct len a, int milli, bool divide) {
    if (len_is_plain(&a)) {
        struct len r = a;
        if (divide) {
            if (milli == 0) return a;
            r.px = (int)((long long)a.px * 1000 / milli);
            r.pct = (int)((long long)a.pct * 1000 / milli);
        } else {
            r.px = (int)((long long)a.px * milli / 1000);
            r.pct = (int)((long long)a.pct * milli / 1000);
        }
        return r;
    }
    struct lexpr *e = new_expr(cx, divide ? LE_DIV : LE_MUL);
    if (!e) return a;
    e->a = leaf_expr(cx, &a);
    e->num = milli;
    return len_from_expr(e);
}

int len_resolve(const struct len *l, int base) {
    if (l->kind != L_VAL) return 0;
    if (!l->expr) return l->px + (int)((long long)base * l->pct / 10000);
    struct lexpr *e = l->expr;
    switch (e->op) {
    case LE_LEN: return len_resolve(&e->leaf, base);
    default: break;
    }
    struct len la = { 0, 0, L_VAL, NULL };
    (void)la;
    int a = 0, b = 0, c = 0;
    // children are stored as lexprs: evaluate each
    struct len tmp;
    if (e->a) { tmp.px = 0; tmp.pct = 0; tmp.kind = L_VAL; tmp.expr = e->a; a = len_resolve(&tmp, base); }
    if (e->b) { tmp.expr = e->b; b = len_resolve(&tmp, base); }
    if (e->c) { tmp.expr = e->c; c = len_resolve(&tmp, base); }
    switch (e->op) {
    case LE_ADD: return a + b;
    case LE_SUB: return a - b;
    case LE_MUL: return (int)((long long)a * e->num / 1000);
    case LE_DIV: return e->num ? (int)((long long)a * 1000 / e->num) : a;
    case LE_MIN: return a < b ? a : b;
    case LE_MAX: return a > b ? a : b;
    case LE_CLAMP: { int lo = a, hi = c, val = b; if (val < lo) val = lo; if (val > hi) val = hi < lo ? lo : hi; return val; }
    }
    return 0;
}

static const struct { const char *unit; int num, den; } fixed_units[] = {
    { "px", 1, 1 }, { "pt", 4, 3 }, { "pc", 16, 1 }, { "in", 96, 1 }, { "cm", 9600, 254 }, { "mm", 960, 254 },
    { "q", 240, 254 },
};

// A number-or-length-or-function in a calc(): returns a len with is_number set when it is a bare number.
struct cval { struct len len; bool is_number; int num; };

static bool calc_sum(struct vp *v, const struct css_ctx *cx, struct cval *out, int depth);

static bool unit_to_len(const struct css_ctx *cx, int num, const char *unit, int unit_len, struct len *out) {
    long long n = num;
    for (unsigned i = 0; i < sizeof(fixed_units) / sizeof(fixed_units[0]); i++) {
        if (w_eq(unit, unit_len, fixed_units[i].unit)) {
            *out = len_px((int)((n * fixed_units[i].num / fixed_units[i].den + (n >= 0 ? 500 : -500)) / 1000));
            return true;
        }
    }
    int base = 0; bool known = true;
    if (w_eq(unit, unit_len, "em")) base = cx->font_px;
    else if (w_eq(unit, unit_len, "rem")) base = cx->root_px;
    else if (w_eq(unit, unit_len, "ex") || w_eq(unit, unit_len, "ch")) { base = cx->font_px / 2; }
    else if (w_eq(unit, unit_len, "lh")) base = cx->font_px * 115 / 100;
    else if (w_eq(unit, unit_len, "vw") || w_eq(unit, unit_len, "svw") || w_eq(unit, unit_len, "lvw") || w_eq(unit, unit_len, "dvw")) base = cx->vw / 100 > 0 ? 0 : 0, n = n * cx->vw / 100, base = 1;
    else if (w_eq(unit, unit_len, "vh") || w_eq(unit, unit_len, "svh") || w_eq(unit, unit_len, "lvh") || w_eq(unit, unit_len, "dvh")) n = n * cx->vh / 100, base = 1;
    else if (w_eq(unit, unit_len, "vmin")) n = n * (cx->vw < cx->vh ? cx->vw : cx->vh) / 100, base = 1;
    else if (w_eq(unit, unit_len, "vmax")) n = n * (cx->vw > cx->vh ? cx->vw : cx->vh) / 100, base = 1;
    else if (w_eq(unit, unit_len, "dppx") || w_eq(unit, unit_len, "x")) known = false;
    else known = false;
    if (!known) return false;
    // `n` is milli-units; for vw etc. base == 1 means "n is already in milli-pixels"
    if (w_eq(unit, unit_len, "em") || w_eq(unit, unit_len, "rem") || w_eq(unit, unit_len, "ex") || w_eq(unit, unit_len, "ch") || w_eq(unit, unit_len, "lh"))
        *out = len_px((int)((n * base + (n >= 0 ? 500 : -500)) / 1000));
    else
        *out = len_px((int)((n + (n >= 0 ? 500 : -500)) / 1000));
    return true;
}

// ---- calc() and friends ----

static bool calc_atom(struct vp *v, const struct css_ctx *cx, struct cval *out, int depth) {
    struct vtoken t;
    if (!vp_next(v, &t)) return false;
    memset(out, 0, sizeof(*out));
    out->len.kind = L_VAL;
    if (t.kind == V_NUM) { out->is_number = true; out->num = t.num; return true; }
    if (t.kind == V_PCT) { out->len.pct = t.num / 10; return true; }
    if (t.kind == V_DIM) return unit_to_len(cx, t.num, t.unit, t.unit_len, &out->len);
    if (t.kind == V_DELIM && t.delim == '(') {
        // parenthesised: read to the matching )
        struct vp inner;
        vp_func_args(v, &inner);
        return calc_sum(&inner, cx, out, depth + 1);
    }
    if (t.kind == V_FUNC) {
        struct vp args;
        vp_func_args(v, &args);
        if (depth > 12) return false;
        if (w_eq(t.s, t.n, "calc") || t.n == 0 || w_eq(t.s, t.n, "-webkit-calc") || w_eq(t.s, t.n, "-moz-calc")) return calc_sum(&args, cx, out, depth + 1);
        bool is_min = w_eq(t.s, t.n, "min"), is_max = w_eq(t.s, t.n, "max"), is_clamp = w_eq(t.s, t.n, "clamp");
        if (is_min || is_max || is_clamp) {
            struct cval vals[3];
            int n = 0;
            for (;;) {
                // split at the top-level comma
                int depth_p = 0, p = args.pos, start = p;
                while (p < args.len) {
                    if (args.s[p] == '(') depth_p++;
                    else if (args.s[p] == ')') depth_p--;
                    else if (args.s[p] == ',' && depth_p == 0) break;
                    p++;
                }
                struct vp part;
                vp_init(&part, args.s + start, p - start);
                if (n >= 3) return false;
                if (!calc_sum(&part, cx, &vals[n], depth + 1)) {
                    // 'none' in clamp(): treat as missing
                    return false;
                }
                n++;
                if (p >= args.len) break;
                args.pos = p + 1;
            }
            if ((is_clamp && n != 3) || (!is_clamp && n < 1)) return false;
            // Combine: all plain -> compute now (percent parts compared as zero is wrong, so only fold when pct are all 0)
            bool foldable = true;
            for (int i = 0; i < n; i++) if (vals[i].is_number || vals[i].len.pct != 0 || vals[i].len.expr) foldable = false;
            if (n >= 1 && vals[0].is_number) { // min(1, 2) of plain numbers
                int r = vals[0].num;
                for (int i = 1; i < n; i++) { if (is_min && vals[i].num < r) r = vals[i].num; if (is_max && vals[i].num > r) r = vals[i].num; }
                out->is_number = true; out->num = r;
                return true;
            }
            if (foldable) {
                int r;
                if (is_clamp) { r = vals[1].len.px; if (r < vals[0].len.px) r = vals[0].len.px; if (r > vals[2].len.px) r = W_MAX(vals[2].len.px, vals[0].len.px); }
                else { r = vals[0].len.px; for (int i = 1; i < n; i++) { if (is_min && vals[i].len.px < r) r = vals[i].len.px; if (is_max && vals[i].len.px > r) r = vals[i].len.px; } }
                out->len = len_px(r);
                return true;
            }
            struct lexpr *e = new_expr(cx, is_clamp ? LE_CLAMP : (is_min ? LE_MIN : LE_MAX));
            if (!e) return false;
            // chain more than 2 args for min/max
            struct lexpr *acc = leaf_expr(cx, &vals[0].len);
            if (is_clamp) {
                e->a = acc; e->b = leaf_expr(cx, &vals[1].len); e->c = leaf_expr(cx, &vals[2].len);
                out->len = len_from_expr(e);
                return true;
            }
            for (int i = 1; i < n; i++) {
                struct lexpr *x = new_expr(cx, is_min ? LE_MIN : LE_MAX);
                x->a = acc; x->b = leaf_expr(cx, &vals[i].len);
                acc = x;
            }
            out->len = len_from_expr(acc);
            return true;
        }
        // other math functions (round, abs, sign, pow, sqrt, ...) aren't supported
        if (w_eq(t.s, t.n, "abs")) { struct cval x; if (!calc_sum(&args, cx, &x, depth + 1)) return false; if (x.is_number) { out->is_number = true; out->num = x.num < 0 ? -x.num : x.num; return true; } if (x.len.px < 0 && !x.len.pct && !x.len.expr) { out->len = len_px(-x.len.px); return true; } *out = x; return true; }
        return false;
    }
    return false;
}

static bool calc_product(struct vp *v, const struct css_ctx *cx, struct cval *out, int depth) {
    if (!calc_atom(v, cx, out, depth)) return false;
    for (;;) {
        struct vtoken t;
        if (!vp_peek(v, &t)) return true;
        if (t.kind == V_SLASH || (t.kind == V_DELIM && t.delim == '*')) {
            vp_next(v, &t);
            bool divide = t.kind == V_SLASH;
            struct cval b;
            if (!calc_atom(v, cx, &b, depth)) return false;
            if (out->is_number && b.is_number) {
                if (divide) out->num = b.num ? (int)((long long)out->num * 1000 / b.num) : out->num;
                else out->num = (int)((long long)out->num * b.num / 1000);
            } else if (b.is_number) {
                out->len = len_scale(cx, out->len, b.num, divide);
            } else if (out->is_number && !divide) {
                struct len l = b.len;
                int n = out->num;
                out->is_number = false;
                out->len = len_scale(cx, l, n, false);
            } else return false;
        } else return true;
    }
}

static bool calc_sum(struct vp *v, const struct css_ctx *cx, struct cval *out, int depth) {
    if (!calc_product(v, cx, out, depth)) return false;
    for (;;) {
        struct vtoken t;
        if (!vp_peek(v, &t)) return true;
        // "+" and "-" need whitespace around them; the tokenizer reads "-5px" as a number, so look at raw characters
        int p = v->pos;
        while (p < v->len && w_is_space(v->s[p])) p++;
        if (p >= v->len) return true;
        char c = v->s[p];
        if ((c == '+' || c == '-') && p > 0) {
            v->pos = p + 1;
            struct cval b;
            if (!calc_product(v, cx, &b, depth)) return false;
            int op = c == '+' ? LE_ADD : LE_SUB;
            if (out->is_number && b.is_number) out->num += (c == '+' ? 1 : -1) * b.num;
            else if (!out->is_number && !b.is_number) out->len = len_combine(cx, op, out->len, b.len);
            else return false;
        } else return true;
    }
}

static bool eval_function_token(struct vp *v, const struct vtoken *t, const struct css_ctx *cx, struct cval *r) {
    struct vp args;
    vp_func_args(v, &args);
    char *tmp = malloc((size_t)args.len + (size_t)t->n + 4);
    if (!tmp) return false;
    int n = 0;
    for (int i = 0; i < t->n; i++) tmp[n++] = t->s[i];
    tmp[n++] = '(';
    for (int i = 0; i < args.len; i++) tmp[n++] = args.s[i];
    tmp[n++] = ')';
    struct vp w;
    vp_init(&w, tmp, n);
    bool ok = calc_atom(&w, cx, r, 0);
    free(tmp);
    return ok;
}

bool css_parse_number(struct vp *v, const struct css_ctx *cx, int *milli) {
    struct vtoken t;
    if (!vp_next(v, &t)) return false;
    if (t.kind == V_NUM) { *milli = t.num; return true; }
    if (t.kind == V_PCT) { *milli = t.num / 100; return true; }
    if (t.kind == V_FUNC) {
        struct cval r;
        if (!eval_function_token(v, &t, cx, &r)) return false;
        if (r.is_number) { *milli = r.num; return true; }
    }
    return false;
}

bool css_parse_len_tok(struct vp *v, const struct vtoken *t, const struct css_ctx *cx, struct len *out, int flags) {
    out->kind = L_VAL; out->expr = NULL; out->px = 0; out->pct = 0;
    if (t->kind == V_NUM) {
        if (t->num == 0) return true;
        if (flags & LF_NUMBER_OK) { out->px = (t->num + (t->num >= 0 ? 500 : -500)) / 1000; return true; }
        return false;
    }
    if (t->kind == V_PCT) {
        if (flags & LF_NO_PCT) return false;
        if ((flags & LF_NONNEG) && t->num < 0) return false;
        out->pct = t->num / 10;
        return true;
    }
    if (t->kind == V_DIM) {
        if (!unit_to_len(cx, t->num, t->unit, t->unit_len, out)) return false;
        if ((flags & LF_NONNEG) && out->px < 0) return false;
        return true;
    }
    if (t->kind == V_FUNC) {
        struct cval r;
        if (!eval_function_token(v, t, cx, &r)) return false;
        if (r.is_number) {
            if (r.num == 0 || (flags & LF_NUMBER_OK)) { *out = len_px((r.num + 500) / 1000); return true; }
            return false;
        }
        *out = r.len;
        return true;
    }
    return false;
}

bool css_parse_len(struct vp *v, const struct css_ctx *cx, struct len *out, int flags) {
    struct vtoken t;
    if (!vp_next(v, &t)) return false;
    return css_parse_len_tok(v, &t, cx, out, flags);
}

bool css_parse_angle(struct vp *v, int *tenths) {
    struct vtoken t;
    if (!vp_next(v, &t)) return false;
    if (t.kind == V_NUM && t.num == 0) { *tenths = 0; return true; }
    if (t.kind != V_DIM) return false;
    if (w_eq(t.unit, t.unit_len, "deg")) { *tenths = t.num / 100; return true; }
    if (w_eq(t.unit, t.unit_len, "turn")) { *tenths = (int)((long long)t.num * 3600 / 1000); return true; }
    if (w_eq(t.unit, t.unit_len, "grad")) { *tenths = (int)((long long)t.num * 9 / 1000); return true; }
    if (w_eq(t.unit, t.unit_len, "rad")) { *tenths = (int)((long long)t.num * 573 / 1000); return true; }
    return false;
}

// ---- Colors ----

static const struct { const char *name; uint32_t rgb; } named_colors[] = {
#include "colors.inc"
};

static uint32_t clamp_channel(int v) { return (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

static uint32_t hsl_to_rgb(int h_milli, int s_milli, int l_milli, int a) {
    // h in degrees x1000, s and l as 0..1000
    long long h = h_milli % 360000; if (h < 0) h += 360000;
    int s = s_milli < 0 ? 0 : (s_milli > 1000 ? 1000 : s_milli);
    int l = l_milli < 0 ? 0 : (l_milli > 1000 ? 1000 : l_milli);
    int c = (1000 - (2 * l - 1000 < 0 ? 1000 - 2 * l : 2 * l - 1000)) * s / 1000;
    int sector = (int)(h / 60000);
    int f = (int)((h % 60000) / 60);              // 0..999
    int x = (sector % 2 == 0) ? c * f / 1000 : c * (1000 - f) / 1000;
    int m = l - c / 2;
    int r = 0, g = 0, b = 0;
    switch (sector) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
    }
    r += m; g += m; b += m;
    return RGBA(clamp_channel((r * 255 + 500) / 1000), clamp_channel((g * 255 + 500) / 1000), clamp_channel((b * 255 + 500) / 1000), clamp_channel(a));
}

static bool channel_value(struct vp *v, const struct css_ctx *cx, int scale_pct_100, int *out_milli, bool *is_none) {
    // returns the value x 1000 in the channel's own scale (percent mapped to scale_pct_100)
    struct vtoken t;
    *is_none = false;
    if (!vp_next(v, &t)) return false;
    if (t.kind == V_NUM) { *out_milli = t.num; return true; }
    if (t.kind == V_PCT) { *out_milli = (int)((long long)t.num * scale_pct_100 / 100000); return true; }
    if (t.kind == V_IDENT && w_eq(t.s, t.n, "none")) { *is_none = true; *out_milli = 0; return true; }
    if (t.kind == V_DIM && (w_eq(t.unit, t.unit_len, "deg") || w_eq(t.unit, t.unit_len, "turn") || w_eq(t.unit, t.unit_len, "rad"))) {
        if (w_eq(t.unit, t.unit_len, "turn")) *out_milli = t.num * 360;
        else if (w_eq(t.unit, t.unit_len, "rad")) *out_milli = (int)((long long)t.num * 57296 / 1000);
        else *out_milli = t.num;
        return true;
    }
    if (t.kind == V_FUNC) {
        struct vp args;
        vp_func_args(v, &args);
        char tmp[200];
        int n = 0;
        for (int i = 0; i < t.n && n < 30; i++) tmp[n++] = t.s[i];
        tmp[n++] = '(';
        for (int i = 0; i < args.len && n < 190; i++) tmp[n++] = args.s[i];
        tmp[n++] = ')';
        struct vp w;
        vp_init(&w, tmp, n);
        struct cval r;
        if (!calc_atom(&w, cx, &r, 0)) return false;
        if (r.is_number) { *out_milli = r.num; return true; }
        if (!r.len.expr && r.len.px == 0 && r.len.pct != 0) { *out_milli = (int)((long long)r.len.pct * 10 * scale_pct_100 / 100000); return true; }
        return false;
    }
    return false;
}

static bool color_from_function(struct vp *v, const struct vtoken *t, const struct css_ctx *cx, uint32_t *out);

static uint32_t mix_colors(uint32_t c1, int p1, uint32_t c2, int p2) {
    // premultiplied mix in sRGB
    int a1 = (int)(c1 >> 24), a2 = (int)(c2 >> 24);
    int total = p1 + p2;
    if (total <= 0) return 0;
    long long wa1 = (long long)a1 * p1, wa2 = (long long)a2 * p2;
    long long A = wa1 + wa2;
    if (A == 0) return 0;
    int r = (int)((((c1 >> 16) & 255) * wa1 + ((c2 >> 16) & 255) * wa2) / A);
    int g = (int)((((c1 >> 8) & 255) * wa1 + ((c2 >> 8) & 255) * wa2) / A);
    int b = (int)(((c1 & 255) * wa1 + (c2 & 255) * wa2) / A);
    int a = (int)(A / total);
    return RGBA(r, g, b, a);
}

static bool parse_color_stop_pct(struct vp *v, const struct css_ctx *cx, uint32_t *color, int *pct, bool *has_pct) {
    *has_pct = false;
    if (!css_parse_color(v, cx, color)) return false;
    struct vtoken t;
    if (vp_peek(v, &t) && t.kind == V_PCT) { vp_next(v, &t); *pct = t.num / 10; *has_pct = true; }
    return true;
}

static bool color_from_function(struct vp *v, const struct vtoken *t, const struct css_ctx *cx, uint32_t *out) {
    struct vp args;
    vp_func_args(v, &args);
    const char *f = t->s;
    int fn = t->n;
    bool legacy_comma = false;
    for (int i = 0; i < args.len; i++) if (args.s[i] == ',') { legacy_comma = true; break; }
    if (w_eq(f, fn, "rgb") || w_eq(f, fn, "rgba")) {
        int c[3]; bool none;
        for (int i = 0; i < 3; i++) {
            if (!channel_value(&args, cx, 255000, &c[i], &none)) return false;
            struct vtoken s; if (i < 2 && legacy_comma && vp_peek(&args, &s) && s.kind == V_COMMA) vp_next(&args, &s);
        }
        int a = 255;
        struct vtoken s;
        if (vp_peek(&args, &s) && (s.kind == V_SLASH || s.kind == V_COMMA)) {
            vp_next(&args, &s);
            int am;
            if (!channel_value(&args, cx, 1000, &am, &none)) return false;
            a = (am * 255 + 500) / 1000;
        }
        *out = RGBA(clamp_channel((c[0] + 500) / 1000), clamp_channel((c[1] + 500) / 1000), clamp_channel((c[2] + 500) / 1000), clamp_channel(a));
        return true;
    }
    if (w_eq(f, fn, "hsl") || w_eq(f, fn, "hsla") || w_eq(f, fn, "hwb")) {
        int h, s_, l_; bool none;
        if (!channel_value(&args, cx, 360000, &h, &none)) return false;
        struct vtoken s;
        if (legacy_comma && vp_peek(&args, &s) && s.kind == V_COMMA) vp_next(&args, &s);
        if (!channel_value(&args, cx, 1000, &s_, &none)) return false;
        if (legacy_comma && vp_peek(&args, &s) && s.kind == V_COMMA) vp_next(&args, &s);
        if (!channel_value(&args, cx, 1000, &l_, &none)) return false;
        int a = 255;
        if (vp_peek(&args, &s) && (s.kind == V_SLASH || s.kind == V_COMMA)) {
            vp_next(&args, &s);
            int am;
            if (!channel_value(&args, cx, 1000, &am, &none)) return false;
            a = (am * 255 + 500) / 1000;
        }
        if (w_eq(f, fn, "hwb")) {
            // whiteness s_, blackness l_ (0..1000 as percentages)
            int wh = s_, bl = l_;
            if (wh + bl >= 1000) { int gray = wh * 255 / (wh + bl); *out = RGBA(gray, gray, gray, clamp_channel(a)); return true; }
            uint32_t base = hsl_to_rgb(h, 1000, 500, 255);
            int r = (int)((base >> 16) & 255), g = (int)((base >> 8) & 255), b = (int)(base & 255);
            r = r * (1000 - wh - bl) / 1000 + 255 * wh / 1000;
            g = g * (1000 - wh - bl) / 1000 + 255 * wh / 1000;
            b = b * (1000 - wh - bl) / 1000 + 255 * wh / 1000;
            *out = RGBA(clamp_channel(r), clamp_channel(g), clamp_channel(b), clamp_channel(a));
            return true;
        }
        // s and l as percentages: 50% -> 500 (already scaled by 1000/100)
        *out = hsl_to_rgb(h, s_, l_, a);
        return true;
    }
    bool oklch = w_eq(f, fn, "oklch"), oklab = w_eq(f, fn, "oklab"), lch = w_eq(f, fn, "lch"), lab = w_eq(f, fn, "lab");
    if (oklch || oklab || lch || lab) {
        int c1, c2, c3; bool none;
        int l_scale = (oklch || oklab) ? 1000 : 100000;
        int ab_scale = oklab ? 400 : (oklch ? 400 : (lab ? 125000 : 150000));
        if (!channel_value(&args, cx, l_scale, &c1, &none)) return false;
        if (!channel_value(&args, cx, ab_scale, &c2, &none)) return false;
        if (!channel_value(&args, cx, (oklab || lab) ? ab_scale : 360000, &c3, &none)) return false;
        int a = 255;
        struct vtoken s;
        if (vp_peek(&args, &s) && s.kind == V_SLASH) {
            vp_next(&args, &s);
            int am;
            if (!channel_value(&args, cx, 1000, &am, &none)) return false;
            a = (am * 255 + 500) / 1000;
        }
        uint32_t rgb;
        if (oklch) rgb = web_oklch_to_rgb(c1, c2, c3);
        else if (oklab) rgb = web_oklab_to_rgb(c1, c2, c3);
        else if (lch) rgb = web_lch_to_rgb(c1, c2, c3);
        else rgb = web_lab_to_rgb(c1, c2, c3);
        *out = (rgb & 0x00FFFFFFu) | ((uint32_t)clamp_channel(a) << 24);
        return true;
    }
    if (w_eq(f, fn, "color-mix")) {
        // color-mix(in srgb, <color> [<pct>], <color> [<pct>])
        struct vtoken s;
        if (!vp_next(&args, &s) || !tok_is(&s, "in")) return false;
        // skip the color space words up to the comma
        while (vp_next(&args, &s) && s.kind != V_COMMA) { if (s.kind == V_FUNC) { struct vp skip; vp_func_args(&args, &skip); } }
        uint32_t c1, c2; int p1 = -1, p2 = -1;
        bool h1, h2;
        // color then optional percentage; percentage may also come first
        struct vtoken pk;
        if (vp_peek(&args, &pk) && pk.kind == V_PCT) { vp_next(&args, &pk); p1 = pk.num / 10; h1 = true; if (!css_parse_color(&args, cx, &c1)) return false; }
        else { int pp = 0; if (!parse_color_stop_pct(&args, cx, &c1, &pp, &h1)) return false; if (h1) p1 = pp; }
        if (vp_peek(&args, &s) && s.kind == V_COMMA) vp_next(&args, &s);
        if (vp_peek(&args, &pk) && pk.kind == V_PCT) { vp_next(&args, &pk); p2 = pk.num / 10; h2 = true; if (!css_parse_color(&args, cx, &c2)) return false; }
        else { int pp = 0; if (!parse_color_stop_pct(&args, cx, &c2, &pp, &h2)) return false; if (h2) p2 = pp; }
        if (p1 < 0 && p2 < 0) { p1 = 5000; p2 = 5000; }
        else if (p1 < 0) p1 = 10000 - p2;
        else if (p2 < 0) p2 = 10000 - p1;
        if (p1 + p2 != 10000 && p1 + p2 > 0) {
            // normalise; if the total is below 100% the result is partly see-through
            int total = p1 + p2;
            if (total < 10000) { *out = mix_colors(c1, p1, c2, p2); int a = (int)(*out >> 24); a = a * total / 10000; *out = (*out & 0x00FFFFFFu) | ((uint32_t)a << 24); return true; }
            p1 = p1 * 10000 / total; p2 = 10000 - p1;
        }
        *out = mix_colors(c1, p1, c2, p2);
        return true;
    }
    if (w_eq(f, fn, "light-dark")) {
        uint32_t a, b;
        if (!css_parse_color(&args, cx, &a)) return false;
        struct vtoken s;
        if (vp_peek(&args, &s) && s.kind == V_COMMA) vp_next(&args, &s);
        if (!css_parse_color(&args, cx, &b)) return false;
        *out = cx->dark ? b : a;
        return true;
    }
    if (w_eq(f, fn, "color")) {
        struct vtoken s;
        if (!vp_next(&args, &s) || s.kind != V_IDENT) return false;
        int c[3]; bool none;
        for (int i = 0; i < 3; i++) if (!channel_value(&args, cx, 1000, &c[i], &none)) return false;
        int a = 255;
        if (vp_peek(&args, &s) && s.kind == V_SLASH) { vp_next(&args, &s); int am; if (!channel_value(&args, cx, 1000, &am, &none)) return false; a = (am * 255 + 500) / 1000; }
        *out = RGBA(clamp_channel((c[0] * 255 + 500) / 1000), clamp_channel((c[1] * 255 + 500) / 1000), clamp_channel((c[2] * 255 + 500) / 1000), clamp_channel(a));
        return true;
    }
    return false;
}

bool css_parse_color_tok(struct vp *v, const struct vtoken *t, const struct css_ctx *cx, uint32_t *out) {
    if (t->kind == V_HASH) {
        int n = t->n;
        if (n != 3 && n != 4 && n != 6 && n != 8) return false;
        uint32_t d[8];
        for (int i = 0; i < n; i++) { int h = w_hex(t->s[i]); if (h < 0) return false; d[i] = (uint32_t)h; }
        if (n <= 4) *out = RGBA(d[0] * 17, d[1] * 17, d[2] * 17, n == 4 ? d[3] * 17 : 255);
        else *out = RGBA(d[0] * 16 + d[1], d[2] * 16 + d[3], d[4] * 16 + d[5], n == 8 ? d[6] * 16 + d[7] : 255);
        return true;
    }
    if (t->kind == V_IDENT) {
        if (w_eq(t->s, t->n, "transparent")) { *out = 0; return true; }
        if (w_eq(t->s, t->n, "currentcolor")) { *out = cx->cur_color; return true; }
        // binary search the sorted names
        int lo = 0, hi = (int)(sizeof(named_colors) / sizeof(named_colors[0])) - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const char *name = named_colors[mid].name;
            int cmp = 0;
            int i = 0;
            for (; i < t->n; i++) {
                int a = w_lower((unsigned char)t->s[i]), b = (unsigned char)name[i];
                if (b == 0) { cmp = 1; break; }
                if (a != b) { cmp = a < b ? -1 : 1; break; }
            }
            if (cmp == 0 && name[i] != '\0') cmp = -1;
            if (cmp == 0) { *out = 0xFF000000u | named_colors[mid].rgb; return true; }
            if (cmp < 0) hi = mid - 1; else lo = mid + 1;
        }
        // system colors
        if (w_eq(t->s, t->n, "canvas") || w_eq(t->s, t->n, "field") || w_eq(t->s, t->n, "buttonface")) { *out = cx->dark ? 0xFF1E1E1E : 0xFFFFFFFF; return true; }
        if (w_eq(t->s, t->n, "canvastext") || w_eq(t->s, t->n, "fieldtext") || w_eq(t->s, t->n, "buttontext") || w_eq(t->s, t->n, "windowtext")) { *out = cx->dark ? 0xFFE6EDF3 : 0xFF000000; return true; }
        if (w_eq(t->s, t->n, "linktext")) { *out = 0xFF0000EE; return true; }
        if (w_eq(t->s, t->n, "highlight")) { *out = 0xFF3390FF; return true; }
        return false;
    }
    if (t->kind == V_FUNC) return color_from_function(v, t, cx, out);
    return false;
}

bool css_parse_color(struct vp *v, const struct css_ctx *cx, uint32_t *out) {
    struct vtoken t;
    if (!vp_next(v, &t)) return false;
    return css_parse_color_tok(v, &t, cx, out);
}
