// web_css.c - reading style sheets, and working out the style of every element.

#include "web_css.h"

// ---- Scanning helpers ----

static void skip_ws(const char *s, int len, int *i) {
    for (;;) {
        while (*i < len && w_is_space(s[*i])) (*i)++;
        if (*i + 1 < len && s[*i] == '/' && s[*i + 1] == '*') {
            *i += 2;
            while (*i + 1 < len && !(s[*i] == '*' && s[*i + 1] == '/')) (*i)++;
            *i = *i + 2 > len ? len : *i + 2;
            continue;
        }
        if (*i + 3 < len && s[*i] == '<' && s[*i + 1] == '!' && s[*i + 2] == '-' && s[*i + 3] == '-') { *i += 4; continue; }
        if (*i + 2 < len && s[*i] == '-' && s[*i + 1] == '-' && s[*i + 2] == '>') { *i += 3; continue; }
        break;
    }
}

static int skip_string(const char *s, int len, int i) {
    char q = s[i++];
    while (i < len && s[i] != q) { if (s[i] == '\\') i++; i++; }
    return i < len ? i + 1 : len;
}

// From `{`: the index just after the matching `}`.
static int block_end(const char *s, int len, int i) {
    int depth = 0;
    for (; i < len; i++) {
        char c = s[i];
        if (c == '"' || c == '\'') { i = skip_string(s, len, i) - 1; continue; }
        if (c == '/' && i + 1 < len && s[i + 1] == '*') { i += 2; while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/')) i++; i++; continue; }
        if (c == '\\') { i++; continue; }
        if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth <= 0) return i + 1; }
    }
    return len;
}

// The end of a prelude: index of the first top-level `;` or `{` (or len).
static int prelude_end(const char *s, int len, int i) {
    int paren = 0;
    for (; i < len; i++) {
        char c = s[i];
        if (c == '"' || c == '\'') { i = skip_string(s, len, i) - 1; continue; }
        if (c == '/' && i + 1 < len && s[i + 1] == '*') { i += 2; while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/')) i++; i++; continue; }
        if (c == '\\') { i++; continue; }
        if (c == '(' || c == '[') paren++;
        else if (c == ')' || c == ']') { if (paren > 0) paren--; }
        else if (paren == 0 && (c == ';' || c == '{' || c == '}')) return i;
    }
    return len;
}

static void trim(const char *s, int *start, int *end) {
    while (*start < *end && w_is_space(s[*start])) (*start)++;
    while (*end > *start && w_is_space(s[*end - 1])) (*end)--;
}

// ---- Declarations ----


// Rewrite url(...) so every address is complete (relative ones are resolved against `base`).
static bool url_is_absolute(const char *u, int n) {
    if (n && (u[0] == '#')) return true;
    if (n >= 2 && u[0] == '/' && u[1] == '/') return false;
    for (int i = 0; i < n; i++) {
        if (u[i] == ':') return i > 0;
        if (!(w_is_alnum(u[i]) || u[i] == '+' || u[i] == '-' || u[i] == '.')) return false;
    }
    return false;
}

static const char *resolve_css_urls(struct page *p, struct w_arena *ar, const char *text, int len, const char *base, int *out_len) {
    *out_len = len;
    if (!base || !*base) return text;
    bool any = false;
    for (int i = 0; i + 4 <= len; i++) if (w_lower((unsigned char)text[i]) == 'u' && w_eq(text + i, 4, "url(")) { any = true; break; }
    if (!any) return text;
    int cap = len + 256, n = 0;
    char *out = malloc((size_t)cap);
    if (!out) return text;
    int i = 0;
    while (i < len) {
        if (i + 4 <= len && w_eq(text + i, 4, "url(") && (i == 0 || !(w_is_alnum(text[i - 1]) || text[i - 1] == '-' || text[i - 1] == '_'))) {
            int j = i + 4;
            while (j < len && w_is_space(text[j])) j++;
            char q = 0;
            if (j < len && (text[j] == '"' || text[j] == '\'')) q = text[j++];
            int us = j;
            while (j < len && (q ? text[j] != q : (text[j] != ')' && !w_is_space(text[j])))) { if (text[j] == '\\' && j + 1 < len) j++; j++; }
            int ue = j;
            if (q && j < len) j++;
            while (j < len && w_is_space(text[j])) j++;
            if (j < len && text[j] == ')' && ue - us < 1000) {
                char rel[1024], full[1100];
                memcpy(rel, text + us, (size_t)(ue - us)); rel[ue - us] = '\0';
                const char *use = rel;
                if (ue - us > 0 && !url_is_absolute(rel, ue - us)) { web_url_resolve(base, rel, full, sizeof(full)); use = full; }
                int ul = (int)strlen(use);
                if (n + ul + 16 > cap) { cap = (n + ul + 16) * 2; out = realloc(out, (size_t)cap); if (!out) return text; }
                memcpy(out + n, "url(\"", 5); n += 5;
                for (int k = 0; k < ul; k++) { if (use[k] == '"' || use[k] == '\\' || use[k] == '\n') out[n++] = '\\'; out[n++] = use[k]; }
                out[n++] = '"'; out[n++] = ')';
                i = j + 1;
                continue;
            }
        }
        if (n + 2 > cap) { cap *= 2; out = realloc(out, (size_t)cap); if (!out) return text; }
        out[n++] = text[i++];
    }
    char *res = w_strdup(ar, out, n);
    free(out);
    *out_len = n;
    return res;
}

void css_parse_declarations(struct page *p, const char *text, int length, struct decl **out, int *count, bool inline_style) {
    (void)inline_style;
    int cap = 4;
    for (int i = 0; i < length; i++) if (text[i] == ';') cap++;
    struct decl *list = w_alloc(inline_style ? &p->larena : &p->arena, sizeof(struct decl) * (size_t)cap);
    int n = 0;
    int i = 0;
    while (i < length && n < cap) {
        skip_ws(text, length, &i);
        if (i >= length) break;
        int start = i;
        // find the end of this declaration (top-level `;`), skipping nested blocks
        int paren = 0, end = i;
        bool has_block = false;
        for (; end < length; end++) {
            char c = text[end];
            if (c == '"' || c == '\'') { end = skip_string(text, length, end) - 1; continue; }
            if (c == '/' && end + 1 < length && text[end + 1] == '*') { end += 2; while (end + 1 < length && !(text[end] == '*' && text[end + 1] == '/')) end++; end++; continue; }
            if (c == '\\') { end++; continue; }
            if (c == '(' || c == '[') paren++;
            else if (c == ')' || c == ']') { if (paren > 0) paren--; }
            else if (c == '{' && paren == 0) { has_block = true; end = block_end(text, length, end) - 1; }
            else if (c == ';' && paren == 0) break;
        }
        i = end + 1;
        if (has_block) {
            // a nested rule (or an odd custom property): not supported
            // but keep a custom property whose value is a block? no.
            continue;
        }
        int colon = -1;
        for (int k = start; k < end; k++) if (text[k] == ':') { colon = k; break; }
        if (colon < 0) continue;
        int ns = start, ne = colon;
        trim(text, &ns, &ne);
        int vs = colon + 1, ve = end;
        trim(text, &vs, &ve);
        if (ne <= ns || ve <= vs) {
            // empty custom property values are allowed but useless
            continue;
        }
        bool important = false;
        // strip comments inside value tail: look for !important at the end
        {
            int k = ve;
            while (k > vs && w_is_space(text[k - 1])) k--;
            if (k - vs >= 10 && w_eq(text + k - 9, 9, "important")) {
                int q = k - 9;
                while (q > vs && w_is_space(text[q - 1])) q--;
                if (q > vs && text[q - 1] == '!') {
                    important = true;
                    ve = q - 1;
                    trim(text, &vs, &ve);
                    if (ve <= vs) continue;
                }
            }
        }
        struct decl *d = &list[n];
        memset(d, 0, sizeof(*d));
        d->important = important;
        d->value = text + vs;
        d->length = ve - vs;
        if (ne - ns >= 2 && text[ns] == '-' && text[ns + 1] == '-') {
            d->prop = P_CUSTOM;
            d->custom = text + ns;
            d->custom_len = ne - ns;
        } else {
            int id = css_prop_lookup(text + ns, ne - ns);
            if (!id) continue;
            d->prop = (uint16_t)id;
        }
        n++;
    }
    *out = list;
    *count = n;
}

// ---- Media queries and @supports ----

static int media_epoch;

static int mq_eval_condition(struct page *p, const char *s, int len);

static bool mq_feature(struct page *p, const char *s, int len) {
    // s is the inside of (...)
    int i = 0;
    skip_ws(s, len, &i);
    int ns = i;
    while (i < len && (w_is_alnum(s[i]) || s[i] == '-' || s[i] == '_')) i++;
    int ne = i;
    skip_ws(s, len, &i);
    // range syntax: (width >= 600px), (400px <= width)
    if (ne == ns || (i < len && (s[i] == '<' || s[i] == '>' || s[i] == '='))) {
        // find the feature name and operators
        const char *names[] = { "width", "height", "aspect-ratio", NULL };
        // try "<name> <op> <value>" and "<value> <op> <name> [<op> <value>]"
        int val[3]; int ops[2]; int nv = 0, no = 0; int feat = -1;
        int k = 0;
        while (k < len && nv < 3) {
            skip_ws(s, len, &k);
            if (k >= len) break;
            if (s[k] == '<' || s[k] == '>' || s[k] == '=') {
                int op = 0;
                if (s[k] == '<') op = 1; else if (s[k] == '>') op = 2; else op = 3;
                k++;
                if (k < len && s[k] == '=') { op += 2; k++; }
                if (no < 2) ops[no++] = op;
                continue;
            }
            int ws = k;
            while (k < len && !w_is_space(s[k]) && s[k] != '<' && s[k] != '>' && s[k] != '=') k++;
            int found = -1;
            for (int q = 0; names[q]; q++) if (w_eq(s + ws, k - ws, names[q])) found = q;
            if (found >= 0) { feat = found; val[nv++] = -1; continue; }
            struct vp v; vp_init(&v, s + ws, k - ws);
            struct css_ctx cx; memset(&cx, 0, sizeof(cx)); cx.page = p; cx.font_px = 16; cx.root_px = 16; cx.vw = p->viewport_w; cx.vh = p->viewport_h;
            struct len l;
            if (!css_parse_len(&v, &cx, &l, LF_NUMBER_OK)) return false;
            val[nv++] = l.px;
        }
        if (feat < 0) return false;
        int cur = feat == 0 ? p->viewport_w : p->viewport_h;
        // feature on the left: name op value
        if (nv == 2 && val[0] == -1) {
            int v = val[1], op = ops[0];
            switch (op) { case 1: return cur < v; case 2: return cur > v; case 3: return cur == v; case 4: return cur <= v; case 5: return cur >= v; }
            return false;
        }
        if (nv == 2 && val[1] == -1) {
            int v = val[0], op = ops[0];
            switch (op) { case 1: return v < cur; case 2: return v > cur; case 3: return v == cur; case 4: return v <= cur; case 5: return v >= cur; }
            return false;
        }
        if (nv == 3 && val[1] == -1 && no == 2) {
            bool a_ok = false, b_ok = false;
            int v1 = val[0], v2 = val[2];
            switch (ops[0]) { case 1: a_ok = v1 < cur; break; case 2: a_ok = v1 > cur; break; case 4: a_ok = v1 <= cur; break; case 5: a_ok = v1 >= cur; break; }
            switch (ops[1]) { case 1: b_ok = cur < v2; break; case 2: b_ok = cur > v2; break; case 4: b_ok = cur <= v2; break; case 5: b_ok = cur >= v2; break; }
            return a_ok && b_ok;
        }
        return false;
    }
    char name[40]; int nl = ne - ns; if (nl > 39) nl = 39;
    for (int k = 0; k < nl; k++) name[k] = (char)w_lower((unsigned char)s[ns + k]);
    name[nl] = '\0';
    if (i >= len || s[i] != ':') {
        // boolean context: (color), (hover), (grid)...
        if (!strcmp(name, "color") || !strcmp(name, "hover") || !strcmp(name, "any-hover") || !strcmp(name, "width") || !strcmp(name, "height")) return true;
        return false;
    }
    i++;
    int vs = i, ve = len;
    trim(s, &vs, &ve);
    const char *val = s + vs; int vl = ve - vs;
    struct css_ctx cx; memset(&cx, 0, sizeof(cx)); cx.page = p; cx.font_px = 16; cx.root_px = 16; cx.vw = p->viewport_w; cx.vh = p->viewport_h;
    if (!strcmp(name, "prefers-color-scheme")) return w_eq(val, vl, p->dark ? "dark" : "light");
    if (!strcmp(name, "prefers-reduced-motion")) return w_eq(val, vl, "no-preference") || w_eq(val, vl, "reduce");
    if (!strcmp(name, "prefers-contrast")) return w_eq(val, vl, "no-preference");
    if (!strcmp(name, "prefers-reduced-data") || !strcmp(name, "prefers-reduced-transparency")) return w_eq(val, vl, "no-preference");
    if (!strcmp(name, "hover") || !strcmp(name, "any-hover")) return w_eq(val, vl, "hover");
    if (!strcmp(name, "pointer") || !strcmp(name, "any-pointer")) return w_eq(val, vl, "fine");
    if (!strcmp(name, "orientation")) return w_eq(val, vl, p->viewport_w >= p->viewport_h ? "landscape" : "portrait");
    if (!strcmp(name, "display-mode")) return w_eq(val, vl, "browser");
    if (!strcmp(name, "scripting")) return w_eq(val, vl, p->scripting ? "enabled" : "none");
    if (!strcmp(name, "color-gamut")) return w_eq(val, vl, "srgb");
    if (!strcmp(name, "forced-colors") || !strcmp(name, "inverted-colors")) return w_eq(val, vl, "none");
    if (!strcmp(name, "color")) { struct vp v; vp_init(&v, val, vl); int n; return css_parse_number(&v, &cx, &n) ? n <= 8000 && n >= 1000 : false; }
    if (!strcmp(name, "update")) return w_eq(val, vl, "fast");
    if (!strcmp(name, "min-resolution") || !strcmp(name, "max-resolution") || !strcmp(name, "-webkit-min-device-pixel-ratio") || !strcmp(name, "-webkit-max-device-pixel-ratio")
        || !strcmp(name, "resolution") || !strcmp(name, "-webkit-device-pixel-ratio")) {
        struct vp v; vp_init(&v, val, vl);
        struct vtoken t;
        if (!vp_next(&v, &t)) return false;
        int dppx = 1000;
        if (t.kind == V_DIM) {
            if (w_eq(t.unit, t.unit_len, "dpi")) dppx = t.num * 1000 / 96000 * 1000 / 1000;
            else if (w_eq(t.unit, t.unit_len, "dppx") || w_eq(t.unit, t.unit_len, "x")) dppx = t.num;
            else if (w_eq(t.unit, t.unit_len, "dpcm")) dppx = t.num * 254 / 9600;
            else return false;
        } else if (t.kind == V_NUM) dppx = t.num;
        else return false;
        if (!strcmp(name, "resolution") || !strcmp(name, "-webkit-device-pixel-ratio")) return dppx == 1000;
        if (name[0] == 'm' && name[1] == 'i') return 1000 >= dppx;
        return 1000 <= dppx;
    }
    // sizes
    struct vp v; vp_init(&v, val, vl);
    struct len l;
    int cur;
    bool is_min = strncmp(name, "min-", 4) == 0, is_max = strncmp(name, "max-", 4) == 0;
    const char *base = is_min || is_max ? name + 4 : name;
    if (!strcmp(base, "width")) cur = p->viewport_w;
    else if (!strcmp(base, "height")) cur = p->viewport_h;
    else if (!strcmp(base, "device-width")) cur = p->viewport_w;
    else if (!strcmp(base, "device-height")) cur = p->viewport_h;
    else if (!strcmp(base, "aspect-ratio") || !strcmp(base, "device-aspect-ratio")) {
        struct vtoken a, slash, b;
        if (!vp_next(&v, &a) || a.kind != V_NUM) return false;
        int bn = 1000;
        if (vp_peek(&v, &slash) && slash.kind == V_SLASH) { vp_next(&v, &slash); if (vp_next(&v, &b) && b.kind == V_NUM) bn = b.num; }
        long long lhs = (long long)p->viewport_w * bn, rhs = (long long)p->viewport_h * a.num;
        if (is_min) return lhs >= rhs;
        if (is_max) return lhs <= rhs;
        return lhs == rhs;
    }
    else return false;
    if (!css_parse_len(&v, &cx, &l, LF_NUMBER_OK)) return false;
    if (is_min) return cur >= l.px;
    if (is_max) return cur <= l.px;
    return cur == l.px;
}

// One media query (no commas): [not|only] type [and (feature)]*
static bool mq_one(struct page *p, const char *s, int len) {
    int i = 0;
    bool negate = false;
    bool result = true;
    bool any = false;
    skip_ws(s, len, &i);
    for (;;) {
        skip_ws(s, len, &i);
        if (i >= len) break;
        if (s[i] == '(') {
            int depth = 1, st = i + 1, k = i + 1;
            while (k < len && depth > 0) { if (s[k] == '(') depth++; else if (s[k] == ')') depth--; k++; }
            int inner_len = k - 1 - st;
            // nested conditions: (not (...)), ((a) and (b))
            bool val;
            int q = st; skip_ws(s, st + inner_len, &q);
            if (q < st + inner_len && (s[q] == '(' || (st + inner_len - q >= 3 && w_eq(s + q, 3, "not")))) val = mq_eval_condition(p, s + st, inner_len) != 0;
            else val = mq_feature(p, s + st, inner_len);
            result = result && val;
            any = true;
            i = k;
            continue;
        }
        int ws = i;
        while (i < len && !w_is_space(s[i]) && s[i] != '(') i++;
        int wl = i - ws;
        if (w_eq(s + ws, wl, "not")) { negate = !negate; continue; }
        if (w_eq(s + ws, wl, "only") || w_eq(s + ws, wl, "and")) continue;
        if (w_eq(s + ws, wl, "all") || w_eq(s + ws, wl, "screen")) { any = true; continue; }
        if (w_eq(s + ws, wl, "print") || w_eq(s + ws, wl, "speech") || w_eq(s + ws, wl, "tv") || w_eq(s + ws, wl, "projection") || w_eq(s + ws, wl, "handheld")
            || w_eq(s + ws, wl, "braille") || w_eq(s + ws, wl, "embossed") || w_eq(s + ws, wl, "aural") || w_eq(s + ws, wl, "tty")) { result = false; any = true; continue; }
        if (w_eq(s + ws, wl, "or")) { continue; }
        result = false; any = true;
    }
    if (!any) return true;
    return negate ? !result : result;
}

static int mq_eval_condition(struct page *p, const char *s, int len) {
    // comma-separated list: any one may match
    int start = 0, paren = 0;
    bool any_item = false;
    for (int i = 0; i <= len; i++) {
        if (i < len) { if (s[i] == '(') paren++; else if (s[i] == ')') paren--; }
        if (i == len || (s[i] == ',' && paren == 0)) {
            int a = start, b = i;
            trim(s, &a, &b);
            if (b > a) { any_item = true; if (mq_one(p, s + a, b - a)) return 1; }
            else if (i == len && !any_item) return 1;
            start = i + 1;
        }
    }
    return any_item ? 0 : 1;
}

static bool supports_eval(const char *s, int len);

static bool supports_term(const char *s, int len, int *i) {
    skip_ws(s, len, i);
    if (*i >= len) return false;
    if (len - *i >= 3 && w_eq(s + *i, 3, "not") && !w_is_alnum(s[*i + 3 > len - 1 ? len - 1 : *i + 3])) {
        *i += 3;
        return !supports_term(s, len, i);
    }
    // selector(...), font-tech(...), etc: assume yes for selector(), no for others
    int fs = *i;
    while (*i < len && (w_is_alnum(s[*i]) || s[*i] == '-')) (*i)++;
    bool fn = *i > fs;
    bool fn_selector = fn && w_eq(s + fs, *i - fs, "selector");
    if (*i < len && s[*i] == '(') {
        int depth = 1, st = *i + 1, k = *i + 1;
        while (k < len && depth > 0) { if (s[k] == '(') depth++; else if (s[k] == ')') depth--; k++; }
        int inner = k - 1 - st;
        *i = k;
        if (fn) return fn_selector;
        // (decl) or (condition)
        int q = st; skip_ws(s, st + inner, &q);
        if (q < st + inner && (s[q] == '(' || w_eq(s + q, 3 <= st + inner - q ? 3 : st + inner - q, "not"))) return supports_eval(s + st, inner);
        int colon = -1;
        for (int c = st; c < st + inner; c++) if (s[c] == ':') { colon = c; break; }
        if (colon < 0) return false;
        int a = st, b = colon;
        trim(s, &a, &b);
        if (b - a >= 2 && s[a] == '-' && s[a + 1] == '-') return true;
        return css_supports_prop(s + a, b - a);
    }
    return false;
}

static bool supports_eval(const char *s, int len) {
    int i = 0;
    bool val = supports_term(s, len, &i);
    for (;;) {
        skip_ws(s, len, &i);
        if (i >= len) break;
        int ws = i;
        while (i < len && w_is_alpha(s[i])) i++;
        bool is_and = w_eq(s + ws, i - ws, "and"), is_or = w_eq(s + ws, i - ws, "or");
        if (!is_and && !is_or) break;
        bool rhs = supports_term(s, len, &i);
        val = is_and ? (val && rhs) : (val || rhs);
    }
    return val;
}

static bool media_matches(struct page *p, struct media_cond *m) {
    if (!m) return true;
    if (m->cache_epoch == media_epoch) return m->cache_value;
    bool v = m->parent ? media_matches(p, m->parent) : true;
    if (v) v = m->supports ? supports_eval(m->text, m->length) : mq_eval_condition(p, m->text, m->length) != 0;
    m->cache_epoch = media_epoch;
    m->cache_value = v;
    return v;
}

// ---- Layers ----

#define MAX_LAYERS 64
static const char *layer_names[MAX_LAYERS];
static int layer_count;

static struct sheet *ua_sheet_cache;
static bool ua_sheet_dark;
static struct page *ua_sheet_page;
static int ua_sheet_gen = -1;

void css_reset(struct page *p) { (void)p; layer_count = 0; media_epoch++; ua_sheet_cache = NULL; ua_sheet_page = NULL; }

static int layer_find(struct page *p, const char *name, int len, const char *outer, int outer_len) {
    char full[160];
    int n = 0;
    if (outer && outer_len) { memcpy(full, outer, (size_t)outer_len); n = outer_len; full[n++] = '.'; }
    if (n + len > 158) len = 158 - n;
    memcpy(full + n, name, (size_t)len); n += len;
    full[n] = '\0';
    for (int i = 0; i < layer_count; i++) if (strcmp(layer_names[i], full) == 0) return i + 1;
    if (layer_count >= MAX_LAYERS) return MAX_LAYERS;
    layer_names[layer_count] = w_strdup(&p->arena, full, n);
    return ++layer_count;
}

// ---- Sheets ----

struct sheet *css_sheet_new(struct page *p) {
    struct sheet *sh = w_alloc(&p->arena, sizeof(struct sheet));
    sh->page = p;
    return sh;
}

static void sheet_add(struct page *p, struct sheet *sh, struct rule *r, struct sheet *imp, struct media_cond *imc) {
    if (sh->count == sh->cap) {
        int nc = sh->cap ? sh->cap * 2 : 32;
        struct sheet_item *ni = w_alloc(&p->arena, sizeof(struct sheet_item) * (size_t)nc);
        if (sh->count) memcpy(ni, sh->items, sizeof(struct sheet_item) * (size_t)sh->count);
        sh->items = ni; sh->cap = nc;
    }
    sh->items[sh->count].rule = r;
    sh->items[sh->count].import = imp;
    sh->items[sh->count].import_media = imc;
    sh->count++;
}

static struct media_cond *new_media(struct page *p, struct media_cond *parent, const char *text, int len, bool supports) {
    struct media_cond *m = w_alloc(&p->arena, sizeof(struct media_cond));
    m->text = w_strdup(&p->arena, text, len);
    m->length = len;
    m->parent = parent;
    m->supports = supports;
    m->cache_epoch = -1;
    return m;
}

static void parse_rules(struct page *p, struct sheet *sh, const char *s, int len, struct media_cond *media, int layer,
                        const char *layer_path, int layer_path_len, int depth);

static void parse_style_rule(struct page *p, struct sheet *sh, const char *s, int pre_s, int pre_e, int body_s, int body_e,
                             struct media_cond *media, int layer) {
    int a = pre_s, b = pre_e;
    trim(s, &a, &b);
    if (b <= a) return;
    struct selector **sels; int nsel;
    css_parse_selector_list(p, s + a, b - a, &sels, &nsel);
    if (nsel == 0) return;
    struct rule *r = w_alloc(&p->arena, sizeof(struct rule));
    r->sels = sels; r->nsel = nsel;
    css_parse_declarations(p, s + body_s, body_e - body_s, &r->decls, &r->ndecls, false);
    r->media = media;
    r->layer = layer;
    if (r->ndecls == 0) return;
    sheet_add(p, sh, r, NULL, NULL);
}

static void parse_rules(struct page *p, struct sheet *sh, const char *s, int len, struct media_cond *media, int layer,
                        const char *layer_path, int layer_path_len, int depth) {
    int i = 0;
    if (depth > 8) return;
    while (i < len) {
        skip_ws(s, len, &i);
        if (i >= len) break;
        if (s[i] == '}') { i++; continue; }
        if (s[i] == '@') {
            int ns = ++i;
            while (i < len && (w_is_alnum(s[i]) || s[i] == '-' || s[i] == '_')) i++;
            int nl = i - ns;
            int pe = prelude_end(s, len, i);
            int ps = i, pend = pe;
            trim(s, &ps, &pend);
            bool has_block = pe < len && s[pe] == '{';
            int be = has_block ? block_end(s, len, pe) : pe + 1;
            if (be > len) be = len;
            if (w_eq(s + ns, nl, "import")) {
                // @import url("x.css") layer(x) supports(...) screen;
                struct vp v; vp_init(&v, s + ps, pend - ps);
                struct vtoken t;
                const char *url = NULL; int ulen = 0;
                if (vp_next(&v, &t)) {
                    if (t.kind == V_STR) { url = t.s; ulen = t.n; }
                    else if (t.kind == V_FUNC && w_eq(t.s, t.n, "url")) {
                        struct vp args; vp_func_args(&v, &args);
                        const char *u = args.s; int n = args.len, k = 0;
                        while (k < n && w_is_space(u[k])) k++;
                        while (n > k && w_is_space(u[n - 1])) n--;
                        if (n - k >= 2 && (u[k] == '"' || u[k] == '\'')) { k++; n--; }
                        url = u + k; ulen = n - k;
                    }
                }
                if (url && depth < 4) {
                    // media is whatever follows (skip layer()/supports())
                    int mstart = v.pos;
                    int mend = pend - ps;
                    const char *mt = s + ps + mstart;
                    int ml = mend - mstart;
                    // strip layer(...) / supports(...) groups
                    char mbuf[200]; int mn = 0;
                    for (int k = 0; k < ml && mn < 199;) {
                        if ((ml - k >= 6 && w_eq(mt + k, 6, "layer(")) || (ml - k >= 9 && w_eq(mt + k, 9, "supports("))) {
                            int dd = 0;
                            while (k < ml) { if (mt[k] == '(') dd++; else if (mt[k] == ')') { dd--; if (dd == 0) { k++; break; } } k++; }
                            continue;
                        }
                        mbuf[mn++] = mt[k++];
                    }
                    mbuf[mn] = '\0';
                    int a = 0, b = mn; trim(mbuf, &a, &b);
                    struct media_cond *imc = media;
                    if (b > a) imc = new_media(p, media, mbuf + a, b - a, false);
                    struct sheet *imp = css_sheet_new(p);
                    imp->order = sh->order;
                    char full[1024];
                    char rel[1024];
                    if (ulen > 1000) ulen = 1000;
                    memcpy(rel, url, (size_t)ulen); rel[ulen] = '\0';
                    web_url_resolve(sh->base_url ? sh->base_url : p->base_url, rel, full, sizeof(full));
                    imp->base_url = w_strdup(&p->arena, full, w_len(full));
                    sheet_add(p, sh, NULL, imp, imc);
                    web_queue_resource(p, imp->base_url, RES_IMPORT, imp);
                }
            } else if (w_eq(s + ns, nl, "media")) {
                if (has_block) {
                    struct media_cond *m = new_media(p, media, s + ps, pend - ps, false);
                    parse_rules(p, sh, s + pe + 1, be - pe - 2 > 0 ? be - pe - 2 : 0, m, layer, layer_path, layer_path_len, depth + 1);
                }
            } else if (w_eq(s + ns, nl, "supports")) {
                if (has_block) {
                    struct media_cond *m = new_media(p, media, s + ps, pend - ps, true);
                    parse_rules(p, sh, s + pe + 1, be - pe - 2 > 0 ? be - pe - 2 : 0, m, layer, layer_path, layer_path_len, depth + 1);
                }
            } else if (w_eq(s + ns, nl, "container") || w_eq(s + ns, nl, "scope") || w_eq(s + ns, nl, "starting-style")) {
                // container queries are answered "yes" (the page is laid out as if the container were wide)
                if (has_block && !w_eq(s + ns, nl, "starting-style"))
                    parse_rules(p, sh, s + pe + 1, be - pe - 2 > 0 ? be - pe - 2 : 0, media, layer, layer_path, layer_path_len, depth + 1);
            } else if (w_eq(s + ns, nl, "layer")) {
                if (has_block) {
                    int name_len = pend - ps;
                    int lid = layer;
                    const char *np = layer_path; int npl = layer_path_len;
                    char path[160];
                    if (name_len > 0) {
                        lid = layer_find(p, s + ps, name_len, layer_path, layer_path_len);
                        int n = 0;
                        if (layer_path_len) { memcpy(path, layer_path, (size_t)layer_path_len); n = layer_path_len; path[n++] = '.'; }
                        if (n + name_len < 158) { memcpy(path + n, s + ps, (size_t)name_len); n += name_len; }
                        path[n] = '\0';
                        np = layer_names[lid - 1]; npl = w_len(np);
                    } else {
                        lid = layer_find(p, "~anon", 5, layer_path, layer_path_len);
                    }
                    parse_rules(p, sh, s + pe + 1, be - pe - 2 > 0 ? be - pe - 2 : 0, media, lid, np, npl, depth + 1);
                } else {
                    // @layer a, b, c;
                    int start = ps;
                    for (int k = ps; k <= pend; k++) {
                        if (k == pend || s[k] == ',') {
                            int a = start, b = k; trim(s, &a, &b);
                            if (b > a) layer_find(p, s + a, b - a, layer_path, layer_path_len);
                            start = k + 1;
                        }
                    }
                }
            }
            // @font-face, @keyframes, @page, @namespace, @charset, @property, @counter-style ...: ignored
            i = be;
            continue;
        }
        // style rule
        int pe = prelude_end(s, len, i);
        if (pe >= len) break;
        if (s[pe] == ';') { i = pe + 1; continue; }
        if (s[pe] == '}') { i = pe + 1; continue; }
        int be = block_end(s, len, pe);
        parse_style_rule(p, sh, s, i, pe, pe + 1, be - 1 > pe + 1 ? be - 1 : pe + 1, media, layer);
        i = be;
    }
}

void css_sheet_fill(struct page *p, struct sheet *sh, const char *text, int length, const char *base_url) {
    sh->page = p;
    if (base_url) sh->base_url = base_url;
    sh->count = 0;
    // skip a UTF-8 byte order mark
    if (length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) { text += 3; length -= 3; }
    if (!sh->user_agent) text = resolve_css_urls(p, &p->arena, text, length, sh->base_url ? sh->base_url : p->base_url, &length);
    parse_rules(p, sh, text, length, NULL, 0, NULL, 0, 0);
    sh->loaded = true;
    media_epoch++;
}

struct sheet *css_parse_sheet(struct page *p, const char *text, int length, const char *base_url) {
    struct sheet *sh = css_sheet_new(p);
    css_sheet_fill(p, sh, text, length, base_url);
    return sh;
}

// ---- The built-in style sheet ----

static const char ua_base[] =
    "html{display:block}"
    "head,link,meta,script,style,title,base,template,area,datalist,param,source,track,[hidden],iframe,video,audio,object,embed,map,dialog:not([open]),frame,frameset,noframes{display:none}"
    "address,article,aside,blockquote,body,center,dd,details,dir,div,dl,dt,fieldset,figcaption,figure,footer,form,header,hgroup,hr,legend,main,menu,nav,ol,optgroup,p,pre,section,summary,ul,h1,h2,h3,h4,h5,h6,listing,xmp,plaintext{display:block}"
    "li{display:list-item}"
    "body{margin:8px}"
    "p,dl,figure,blockquote,pre,listing,xmp{margin-top:1em;margin-bottom:1em}"
    "blockquote,figure{margin-left:40px;margin-right:40px}"
    "center{text-align:center}"
    "h1{font-size:2em;margin-top:.67em;margin-bottom:.67em;font-weight:bold}"
    "h2{font-size:1.5em;margin-top:.83em;margin-bottom:.83em;font-weight:bold}"
    "h3{font-size:1.17em;margin-top:1em;margin-bottom:1em;font-weight:bold}"
    "h4{font-size:1em;margin-top:1.33em;margin-bottom:1.33em;font-weight:bold}"
    "h5{font-size:.83em;margin-top:1.67em;margin-bottom:1.67em;font-weight:bold}"
    "h6{font-size:.67em;margin-top:2.33em;margin-bottom:2.33em;font-weight:bold}"
    "ul,ol,menu,dir{margin-top:1em;margin-bottom:1em;padding-left:40px}"
    "ul{list-style-type:disc}ol{list-style-type:decimal}"
    "ul ul,ol ul{list-style-type:circle}ul ul ul,ol ul ul,ul ol ul,ol ol ul{list-style-type:square}"
    "ul ul,ul ol,ol ul,ol ol{margin-top:0;margin-bottom:0}"
    "dd{margin-left:40px}"
    "pre,listing,xmp,plaintext{white-space:pre;font-family:monospace;font-size:13px}"
    "code,kbd,samp,tt{font-family:monospace;font-size:13px}"
    "b,strong,th{font-weight:bold}"
    "i,em,cite,var,dfn,address{font-style:italic}"
    "small{font-size:smaller}big{font-size:larger}"
    "sub{vertical-align:sub;font-size:smaller}sup{vertical-align:super;font-size:smaller}"
    "u,ins{text-decoration:underline}s,strike,del{text-decoration:line-through}"
    "mark{background-color:#ff0;color:#000}"
    "abbr[title]{text-decoration:underline dotted}"
    "a:link{color:#0000ee;text-decoration:underline}a:visited{color:#551a8b}"
    "img{display:inline}"
    "hr{border:1px inset;margin:.5em auto;color:#808080;overflow:hidden}"
    "table{display:table;border-collapse:separate;border-spacing:2px;box-sizing:border-box;text-indent:0}"
    "caption{display:table-caption;text-align:center}"
    "colgroup{display:table-column-group}col{display:table-column}"
    "thead{display:table-header-group}tbody{display:table-row-group}tfoot{display:table-footer-group}"
    "tr{display:table-row}td,th{display:table-cell;padding:1px;vertical-align:inherit}th{text-align:center}tr,tbody,thead,tfoot{vertical-align:middle}"
    "fieldset{border:2px groove #c0c0c0;padding:.35em .75em .625em;margin:0 2px;min-inline-size:min-content}"
    "legend{padding:0 2px}"
    "summary{display:block}"
    "q:before{content:open-quote}q:after{content:close-quote}"
    "input,textarea,select,button{font-family:sans-serif;font-size:13.3333px;text-align:start;text-indent:0;text-transform:none}"
    "button,input[type=button],input[type=submit],input[type=reset]{display:inline-block;padding:1px 6px;border:1px solid #767676;border-radius:3px;text-align:center;white-space:pre}"
    "input,textarea,select{border:1px solid #767676;border-radius:2px;padding:1px 2px;display:inline-block}"
    "input[type=checkbox],input[type=radio]{border:0;padding:0;margin:3px 3px 3px 4px}"
    "input[type=hidden]{display:none}"
    "textarea{font-family:monospace;white-space:pre-wrap;padding:2px}"
    "select{padding:0 2px}"
    "svg{display:inline}"
    "ruby{display:inline}rt{font-size:50%}";

static const char ua_light[] =
    "html{color:#000}"
    "button,input[type=button],input[type=submit],input[type=reset]{background-color:#efefef;color:#000;border-color:#767676}"
    "input,textarea,select{background-color:#fff;color:#000}"
    "input[type=checkbox],input[type=radio]{background-color:transparent}"
    "hr{border-color:#9a9a9a}";

static const char ua_dark[] =
    "html{color:#e8e8e8}"
    "a:link{color:#8ab4f8}a:visited{color:#c58af9}"
    "mark{background-color:#a68a00;color:#fff}"
    "button,input[type=button],input[type=submit],input[type=reset]{background-color:#333;color:#eee;border-color:#6b6b6b}"
    "input,textarea,select{background-color:#1e1e1e;color:#eee;border-color:#6b6b6b}"
    "input[type=checkbox],input[type=radio]{background-color:transparent}"
    "hr{border-color:#555}fieldset{border-color:#555}";

static struct sheet *ua_sheet(struct page *p, bool dark) {
    if (ua_sheet_cache && ua_sheet_page == p && ua_sheet_dark == dark && ua_sheet_gen == p->generation) return ua_sheet_cache;
    int nb = (int)sizeof(ua_base) - 1, nt = dark ? (int)sizeof(ua_dark) - 1 : (int)sizeof(ua_light) - 1;
    char *buf = w_alloc(&p->arena, (size_t)(nb + nt + 1));
    memcpy(buf, ua_base, (size_t)nb);
    memcpy(buf + nb, dark ? ua_dark : ua_light, (size_t)nt);
    struct sheet *sh = css_sheet_new(p);
    sh->user_agent = true;
    css_sheet_fill(p, sh, buf, nb + nt, NULL);
    ua_sheet_cache = sh; ua_sheet_dark = dark; ua_sheet_page = p; ua_sheet_gen = p->generation;
    return sh;
}

// ---- The rule index ----

struct ientry {
    struct rule *rule;
    struct selector *sel;
    uint32_t spec;
    uint8_t pseudo;         // 0, 1 ::before, 2 ::after
    const char *key;
    int key_len;
    struct ientry *next;
};

#define BUCKETS 1024
struct rindex {
    struct ientry *id[BUCKETS], *cls[BUCKETS], *tag[BUCKETS];
    struct ientry *uni;
    uint32_t order;
    bool saw_author;
};

static unsigned hstr(const char *s, int n, bool fold) {
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= (unsigned char)(fold ? w_lower((unsigned char)s[i]) : s[i]); h *= 16777619u; }
    return h;
}

static void index_rule(struct page *p, struct rindex *ix, struct rule *r, struct sheet *sh) {
    r->order = ix->order++;
    r->sheet_index = sh->user_agent ? 0 : 1;
    for (int i = 0; i < r->nsel; i++) {
        struct ientry *e = w_alloc(&p->larena, sizeof(struct ientry));
        e->rule = r; e->sel = r->sels[i];
        e->spec = (uint32_t)css_selector_specificity(e->sel);
        e->pseudo = (uint8_t)css_selector_pseudo_element(e->sel);
        const char *key; int kind;
        css_selector_key(e->sel, &key, &kind);
        e->key = key; e->key_len = key ? w_len(key) : 0;
        struct ientry **head;
        if (kind == 1) head = &ix->id[hstr(key, e->key_len, false) % BUCKETS];
        else if (kind == 2) head = &ix->cls[hstr(key, e->key_len, false) % BUCKETS];
        else if (kind == 3) head = &ix->tag[hstr(key, e->key_len, true) % BUCKETS];
        else head = &ix->uni;
        e->next = *head; *head = e;
    }
}

static void index_sheet(struct page *p, struct rindex *ix, struct sheet *sh, int depth) {
    if (depth > 6) return;
    for (int i = 0; i < sh->count; i++) {
        struct sheet_item *it = &sh->items[i];
        if (it->rule) index_rule(p, ix, it->rule, sh);
        else if (it->import && media_matches(p, it->import_media)) index_sheet(p, ix, it->import, depth + 1);
    }
}

// ---- Style of one element ----

struct ent {
    uint64_t key;
    const struct decl *decls;
    int ndecls;
    bool imp;
};

#define MAX_ENTS 2048
static struct ent ents[MAX_ENTS];
static struct ent ents_pseudo[2][512];
static int ent_count, pseudo_count[2];

static uint64_t make_key(int tier, int layer_rank, uint32_t spec, uint32_t order) {
    return ((uint64_t)tier << 56) | ((uint64_t)(layer_rank & 255) << 48) | ((uint64_t)(spec & 0xFFFFFF) << 24) | (order & 0xFFFFFF);
}

static void add_ent(struct ent *arr, int *count, int max, const struct rule *r, const struct decl *decls, int nd, uint32_t spec, uint32_t order, int tier_kind) {
    // tier_kind: 0 UA, 1 author, 2 inline, 3 presentational hint
    bool has_n = false, has_i = false;
    for (int i = 0; i < nd; i++) { if (decls[i].important) has_i = true; else has_n = true; }
    int layer = r ? r->layer : 0;
    // layers: unlayered styles beat any layer (for normal declarations)
    int rank_n = layer == 0 ? 255 : layer;
    int rank_i = layer == 0 ? 0 : 255 - layer;
    if (has_n && *count < max) {
        int tier = tier_kind == 0 ? 0 : (tier_kind == 3 ? 1 : (tier_kind == 1 ? 2 : 3));
        arr[*count].key = make_key(tier, tier_kind == 0 || tier_kind == 3 ? 0 : rank_n, spec, order);
        arr[*count].decls = decls; arr[*count].ndecls = nd; arr[*count].imp = false;
        (*count)++;
    }
    if (has_i && *count < max) {
        int tier = tier_kind == 0 ? 4 : (tier_kind == 1 ? 5 : (tier_kind == 2 ? 6 : 4));
        arr[*count].key = make_key(tier, tier_kind == 0 ? 0 : rank_i, spec, order);
        arr[*count].decls = decls; arr[*count].ndecls = nd; arr[*count].imp = true;
        (*count)++;
    }
}

static void sort_ents(struct ent *a, int n) {
    // shell sort, descending by key
    for (int gap = n / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < n; i++) {
            struct ent t = a[i];
            int j = i;
            while (j >= gap && a[j - gap].key < t.key) { a[j] = a[j - gap]; j -= gap; }
            a[j] = t;
        }
    }
}

static void gather_bucket(struct page *p, struct node *n, struct ientry *head, const char *key, int key_len, bool fold) {
    for (struct ientry *e = head; e; e = e->next) {
        if (key) {
            if (e->key_len != key_len) continue;
            if (fold ? !w_eq(key, key_len, e->key) : memcmp(e->key, key, (size_t)key_len) != 0) continue;
        }
        if (!media_matches(p, e->rule->media)) continue;
        if (!css_selector_matches(e->sel, n)) continue;
        int tier = e->rule->sheet_index == 0 ? 0 : 1;
        if (e->pseudo == 0) add_ent(ents, &ent_count, MAX_ENTS, e->rule, e->rule->decls, e->rule->ndecls, e->spec, e->rule->order, tier);
        else add_ent(ents_pseudo[e->pseudo - 1], &pseudo_count[e->pseudo - 1], 512, e->rule, e->rule->decls, e->rule->ndecls, e->spec, e->rule->order, tier);
    }
}

static bool is_font_prop(int prop) {
    switch (prop) {
    case P_FONT: case P_FONT_SIZE: case P_FONT_FAMILY: case P_FONT_WEIGHT: case P_FONT_STYLE: case P_LINE_HEIGHT: case P_COLOR: return true;
    default: return false;
    }
}

struct cascade_env {
    struct page *page;
    int root_px;
    int vw, vh;
    struct style *initial;
};

static void inherit_from(struct style *s, const struct style *par) {
    s->visibility = par->visibility; s->text_align = par->text_align; s->white_space = par->white_space;
    s->text_transform = par->text_transform; s->list_style = par->list_style; s->list_pos_inside = par->list_pos_inside;
    s->text_decoration = par->text_decoration; s->word_break = par->word_break; s->font_italic = par->font_italic;
    s->font_family = par->font_family; s->font_weight = par->font_weight; s->font_size = par->font_size;
    s->line_height_pct = par->line_height_pct; s->line_height_px = par->line_height_px;
    s->letter_spacing = par->letter_spacing; s->word_spacing = par->word_spacing; s->text_indent = par->text_indent;
    s->tab_size = par->tab_size; s->color = par->color; s->border_collapse = par->border_collapse;
    s->spacing_h = par->spacing_h; s->spacing_v = par->spacing_v; s->pointer_events_none = par->pointer_events_none;
    s->user_select_none = par->user_select_none; s->svg_fill = par->svg_fill; s->svg_stroke = par->svg_stroke;
    s->svg_fill_set = par->svg_fill_set; s->svg_stroke_set = par->svg_stroke_set; s->svg_stroke_width = par->svg_stroke_width;
    s->svg_fill_ref = par->svg_fill_ref; s->svg_stroke_ref = par->svg_stroke_ref; s->fill_opacity = par->fill_opacity;
    s->stroke_opacity = par->stroke_opacity; s->svg_linecap = par->svg_linecap; s->svg_linejoin = par->svg_linejoin;
    s->vars = par->vars; s->text_overflow_ellipsis = 0;
}

struct style *css_inherit_style(struct page *p, const struct style *parent, int display) {
    struct style *s = w_alloc(&p->larena, sizeof(struct style));
    *s = *css_default_style();
    if (parent) inherit_from(s, parent);
    for (int i = 0; i < 4; i++) s->border_width[i] = 0;
    s->display = (uint8_t)display;
    return s;
}

static struct style *compute(struct cascade_env *env, struct ent *list, int count, const struct style *parent, bool pseudo) {
    struct page *p = env->page;
    struct style *s = w_alloc(&p->larena, sizeof(struct style));
    *s = *css_default_style();
    if (parent) inherit_from(s, parent);
    s->is_pseudo = pseudo;
    unsigned char set[P_COUNT + 1];
    memset(set, 0, sizeof(set));
    sort_ents(list, count);
    const struct custom_prop *parent_vars = parent ? parent->vars : NULL;
    // pass 0: custom properties
    for (int e = 0; e < count; e++) {
        for (int k = list[e].ndecls - 1; k >= 0; k--) {
            const struct decl *d = &list[e].decls[k];
            if (d->prop != P_CUSTOM || d->important != list[e].imp) continue;
            bool dup = false;
            for (const struct custom_prop *v = s->vars; v && v != parent_vars; v = v->next)
                if ((int)strlen(v->name) == d->custom_len && memcmp(v->name, d->custom, (size_t)d->custom_len) == 0) { dup = true; break; }
            if (dup) continue;
            struct custom_prop *cp = w_alloc(&p->larena, sizeof(struct custom_prop));
            cp->name = w_strdup(&p->larena, d->custom, d->custom_len);
            cp->value = d->value; cp->length = d->length;
            cp->next = s->vars; s->vars = cp;
        }
    }
    struct css_ctx cx;
    memset(&cx, 0, sizeof(cx));
    cx.page = p; cx.root_px = env->root_px; cx.vw = env->vw; cx.vh = env->vh; cx.dark = p->dark;
    cx.font_px = s->font_size; cx.cur_color = s->color;
    for (int pass = 0; pass < 2; pass++) {
        for (int e = 0; e < count; e++) {
            for (int k = list[e].ndecls - 1; k >= 0; k--) {
                const struct decl *d = &list[e].decls[k];
                if (d->prop == P_CUSTOM || d->important != list[e].imp) continue;
                bool fp = is_font_prop(d->prop);
                if ((pass == 0) != fp) continue;
                if (!css_prop_is_shorthand(d->prop) && set[d->prop]) continue;
                cx.font_px = s->font_size; cx.cur_color = s->color;
                css_apply_decl(s, parent, &cx, d, set, env->initial);
            }
        }
    }
    return s;
}

// Fix up values once the cascade is done.
static void finalize(struct style *s, const struct node *n, const struct style *parent) {
    for (int i = 0; i < 4; i++) {
        if (s->border_style[i] == BS_NONE || s->border_style[i] == BS_HIDDEN) s->border_width[i] = 0;
        if (s->border_color[i] == COLOR_CURRENT) s->border_color[i] = s->color;
        if (s->border_width[i] > 0 && s->border_width[i] < 1) s->border_width[i] = 1;
    }
    if (s->deco_color == COLOR_CURRENT) s->deco_color = s->color;
    for (int i = 0; i < s->shadow_count; i++) if (s->shadows[i].color == COLOR_CURRENT) s->shadows[i].color = s->color;
    for (int i = 0; i < s->bg_count; i++) {
        struct gradient *g = s->bg[i].gradient;
        if (g) for (int k = 0; k < g->stop_count; k++) if (g->stops[k].color == COLOR_CURRENT) g->stops[k].color = s->color;
    }
    if (s->display != D_NONE && s->display != D_CONTENTS) {
        bool oof = s->position == POS_ABSOLUTE || s->position == POS_FIXED;
        if (oof) s->floating = FL_NONE;
        if (oof || s->floating != FL_NONE || (n && n->parent == NULL)) {
            switch (s->display) {
            case D_INLINE: case D_INLINE_BLOCK: case D_TABLE_ROW_GROUP: case D_TABLE_ROW: case D_TABLE_CELL: case D_TABLE_CAPTION:
                if (s->display == D_INLINE || s->display == D_INLINE_BLOCK) s->display = D_BLOCK;
                break;
            case D_INLINE_FLEX: s->display = D_FLEX; break;
            case D_INLINE_GRID: s->display = D_GRID; break;
            case D_INLINE_TABLE: s->display = D_TABLE; break;
            }
        }
    }
    if (s->overflow_x == OV_VISIBLE && s->overflow_y != OV_VISIBLE && s->overflow_y != OV_CLIP) s->overflow_x = OV_AUTO;
    if (s->overflow_y == OV_VISIBLE && s->overflow_x != OV_VISIBLE && s->overflow_x != OV_CLIP) s->overflow_y = OV_AUTO;
    (void)parent;
}

static int snprintf_hack(char *out, int max, const char *fmt, ...);

// HTML attributes that mean style (bgcolor, align ...), as low-priority declarations.
static char pres_buf[1024];

static int pres_decls(struct page *p, struct node *n, struct decl *out, int max, char *pool, int *pool_used) {
    int cnt = 0;
    (void)p;
    #define ADDD(propid, ...) do { if (cnt < max) { int off = *pool_used; int w = snprintf_hack(pool + off, 160, __VA_ARGS__); *pool_used += w + 1; \
        out[cnt].prop = (propid); out[cnt].important = false; out[cnt].value = pool + off; out[cnt].length = w; out[cnt].custom = NULL; out[cnt].custom_len = 0; cnt++; } } while (0)
    const char *v;
    if (!n || n->type != N_ELEMENT) return 0;
    if (n->in_svg) {
        static const struct { const char *attr; int prop; } sv[] = {
            { "fill", P_FILL }, { "stroke", P_STROKE }, { "stroke-width", P_STROKE_WIDTH }, { "fill-opacity", P_FILL_OPACITY },
            { "stroke-opacity", P_STROKE_OPACITY }, { "stroke-linecap", P_STROKE_LINECAP }, { "stroke-linejoin", P_STROKE_LINEJOIN },
            { "color", P_COLOR }, { "visibility", P_VISIBILITY } };
        for (unsigned i = 0; i < sizeof(sv) / sizeof(sv[0]); i++) {
            const char *av = node_attr(n, sv[i].attr);
            if (av && *av) { char c[150]; int l = w_len(av); if (l > 140) l = 140; memcpy(c, av, (size_t)l); c[l] = 0; ADDD(sv[i].prop, "%s", c); }
        }
    }
    if ((v = node_attr(n, "bgcolor"))) { char c[40]; int l = w_len(v); if (l > 38) l = 38; memcpy(c, v, (size_t)l); c[l] = 0;
        bool hex6 = l == 6; for (int i = 0; i < l; i++) if (w_hex(c[i]) < 0) hex6 = false;
        if (hex6) ADDD(P_BACKGROUND_COLOR, "#%s", c); else ADDD(P_BACKGROUND_COLOR, "%s", c); }
    if (n->tag == T_BODY && (v = node_attr(n, "text"))) { char c[40]; int l = w_len(v); if (l > 38) l = 38; memcpy(c, v, (size_t)l); c[l] = 0;
        bool hex6 = l == 6; for (int i = 0; i < l; i++) if (w_hex(c[i]) < 0) hex6 = false;
        if (hex6) ADDD(P_COLOR, "#%s", c); else ADDD(P_COLOR, "%s", c); }
    if (n->tag == T_FONT) {
        if ((v = node_attr(n, "color"))) { char c[40]; int l = w_len(v); if (l > 38) l = 38; memcpy(c, v, (size_t)l); c[l] = 0;
            bool hex6 = l == 6; for (int i = 0; i < l; i++) if (w_hex(c[i]) < 0) hex6 = false;
            if (hex6) ADDD(P_COLOR, "#%s", c); else ADDD(P_COLOR, "%s", c); }
        if ((v = node_attr(n, "face"))) { char c[100]; int l = w_len(v); if (l > 90) l = 90; memcpy(c, v, (size_t)l); c[l] = 0; ADDD(P_FONT_FAMILY, "%s", c); }
        if ((v = node_attr(n, "size"))) {
            int sz = 3; bool rel = false, neg = false;
            if (*v == '+') { rel = true; v++; } else if (*v == '-') { rel = true; neg = true; v++; }
            if (w_is_digit(*v)) sz = *v - '0';
            if (rel) sz = neg ? 3 - sz : 3 + sz;
            if (sz < 1) sz = 1; if (sz > 7) sz = 7;
            static const char *const names[] = { "x-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large" };
            ADDD(P_FONT_SIZE, "%s", names[sz]);
        }
    }
    if ((v = node_attr(n, "align"))) {
        if (n->tag == T_IMG || n->tag == T_TABLE) {
            if (w_eq0(v, "left")) ADDD(P_FLOAT, "left");
            else if (w_eq0(v, "right")) ADDD(P_FLOAT, "right");
            else if (w_eq0(v, "center") && n->tag == T_TABLE) { ADDD(P_MARGIN_LEFT, "auto"); ADDD(P_MARGIN_RIGHT, "auto"); }
            else if (n->tag == T_IMG && (w_eq0(v, "top") || w_eq0(v, "middle") || w_eq0(v, "bottom"))) ADDD(P_VERTICAL_ALIGN, "%s", v);
        } else if (n->tag == T_DIV || n->tag == T_P || n->tag == T_TD || n->tag == T_TH || n->tag == T_TR || (n->tag >= T_H1 && n->tag <= T_H6) || n->tag == T_CENTER || n->tag == T_SECTION) {
            if (w_eq0(v, "left") || w_eq0(v, "right") || w_eq0(v, "center") || w_eq0(v, "justify")) ADDD(P_TEXT_ALIGN, "%s", v);
        }
    }
    if ((v = node_attr(n, "valign")) && (n->tag == T_TD || n->tag == T_TH || n->tag == T_TR)) {
        if (w_eq0(v, "top") || w_eq0(v, "middle") || w_eq0(v, "bottom") || w_eq0(v, "baseline")) ADDD(P_VERTICAL_ALIGN, "%s", v);
    }
    if ((n->tag == T_TD || n->tag == T_TH || n->tag == T_TABLE || n->tag == T_IMG || n->tag == T_HR || n->tag == T_COL || n->tag == T_SVG) ) {
        if ((v = node_attr(n, "width"))) { char c[24]; int l = 0; while (w_is_digit(v[l]) && l < 10) { c[l] = v[l]; l++; }
            if (l) { if (v[l] == '%') ADDD(P_WIDTH, "%.*s%%", l, c); else ADDD(P_WIDTH, "%.*spx", l, c); } }
        if ((v = node_attr(n, "height"))) { char c[24]; int l = 0; while (w_is_digit(v[l]) && l < 10) { c[l] = v[l]; l++; }
            if (l) { if (v[l] == '%') ADDD(P_HEIGHT, "%.*s%%", l, c); else ADDD(P_HEIGHT, "%.*spx", l, c); } }
    }
    if (n->tag == T_TABLE) {
        if ((v = node_attr(n, "border"))) { int b = 0; while (w_is_digit(*v)) { b = b * 10 + (*v - '0'); v++; } if (b > 0) { ADDD(P_BORDER_WIDTH, "%dpx", b); ADDD(P_BORDER_STYLE, "outset"); ADDD(P_BORDER_COLOR, "#808080"); } }
        if ((v = node_attr(n, "cellspacing"))) { int b = 0; while (w_is_digit(*v)) { b = b * 10 + (*v - '0'); v++; } ADDD(P_BORDER_SPACING, "%dpx", b); }
    }
    if (n->tag == T_TD || n->tag == T_TH) {
        struct node *t = n->parent;
        while (t && t->tag != T_TABLE) t = t->parent;
        if (t) {
            if ((v = node_attr(t, "cellpadding"))) { int b = 0; while (w_is_digit(*v)) { b = b * 10 + (*v - '0'); v++; } ADDD(P_PADDING, "%dpx", b); }
            if ((v = node_attr(t, "border"))) { int b = 0; while (w_is_digit(*v)) { b = b * 10 + (*v - '0'); v++; } if (b > 0) { ADDD(P_BORDER_WIDTH, "1px"); ADDD(P_BORDER_STYLE, "inset"); ADDD(P_BORDER_COLOR, "#808080"); } }
        }
        if (node_has_attr(n, "nowrap")) ADDD(P_WHITE_SPACE, "nowrap");
    }
    #undef ADDD
    return cnt;
}

// snprintf is not available everywhere; this handles the few formats used above (%s, %d, %.*s).
static int snprintf_hack(char *out, int max, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = 0;
    for (; *fmt && n < max - 1; fmt++) {
        if (*fmt != '%') { out[n++] = *fmt; continue; }
        fmt++;
        if (*fmt == '%') { out[n++] = '%'; continue; }
        if (*fmt == 's') { const char *s = __builtin_va_arg(ap, const char *); while (*s && n < max - 1) out[n++] = *s++; continue; }
        if (*fmt == 'd') {
            int v = __builtin_va_arg(ap, int);
            char tmp[16]; int t = 0;
            if (v < 0) { out[n++] = '-'; v = -v; }
            if (v == 0) tmp[t++] = '0';
            while (v > 0) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
            while (t > 0 && n < max - 1) out[n++] = tmp[--t];
            continue;
        }
        if (*fmt == '.' && fmt[1] == '*' && fmt[2] == 's') {
            int l = __builtin_va_arg(ap, int);
            const char *s = __builtin_va_arg(ap, const char *);
            for (int i = 0; i < l && n < max - 1; i++) out[n++] = s[i];
            fmt += 2;
            continue;
        }
    }
    out[n] = '\0';
    __builtin_va_end(ap);
    return n;
}

static void style_node(struct cascade_env *env, struct rindex *ix, struct node *n, const struct style *parent, struct sheet *ua) {
    struct page *p = env->page;
    (void)ua;
    ent_count = 0; pseudo_count[0] = pseudo_count[1] = 0;
    if (n->id) gather_bucket(p, n, ix->id[hstr(n->id, w_len(n->id), false) % BUCKETS], n->id, w_len(n->id), false);
    if (n->cls) {
        const char *c = n->cls;
        while (*c) {
            while (*c && w_is_space(*c)) c++;
            const char *st = c;
            while (*c && !w_is_space(*c)) c++;
            if (c > st) gather_bucket(p, n, ix->cls[hstr(st, (int)(c - st), false) % BUCKETS], st, (int)(c - st), false);
        }
    }
    if (n->name) gather_bucket(p, n, ix->tag[hstr(n->name, w_len(n->name), true) % BUCKETS], n->name, w_len(n->name), true);
    gather_bucket(p, n, ix->uni, NULL, 0, false);
    // presentational hints
    struct decl hint[16];
    char pool[2048]; int pool_used = 0;
    int nh = pres_decls(p, n, hint, 16, pool, &pool_used);
    struct decl *hint_copy = NULL;
    if (nh) {
        hint_copy = w_alloc(&p->larena, sizeof(struct decl) * (size_t)nh);
        char *pcopy = w_alloc(&p->larena, (size_t)pool_used + 1);
        memcpy(pcopy, pool, (size_t)pool_used);
        for (int i = 0; i < nh; i++) { hint_copy[i] = hint[i]; hint_copy[i].value = pcopy + (hint[i].value - pool); }
        add_ent(ents, &ent_count, MAX_ENTS, NULL, hint_copy, nh, 0, 0, 3);
    }
    // inline style
    const char *st = node_attr(n, "style");
    if (st) {
        struct decl *id; int nd;
        int stl; const char *st2 = resolve_css_urls(p, &p->larena, st, w_len(st), p->base_url, &stl);
        css_parse_declarations(p, st2, stl, &id, &nd, true);
        if (nd) add_ent(ents, &ent_count, MAX_ENTS, NULL, id, nd, 0xFFFFFF, 0xFFFFFF, 2);
    }
    n->style = compute(env, ents, ent_count, parent, false);
    finalize(n->style, n, parent);
    n->before = n->after = NULL;
    for (int w = 0; w < 2; w++) {
        if (pseudo_count[w] == 0) continue;
        struct style *ps = compute(env, ents_pseudo[w], pseudo_count[w], n->style, true);
        if (ps->display == D_INLINE && ps->content_kind == 0) { /* not generated */ }
        if (ps->content_kind == 0 || ps->display == D_NONE) continue;
        finalize(ps, NULL, n->style);
        if (w == 0) n->before = ps; else n->after = ps;
    }
}

static void style_tree(struct cascade_env *env, struct rindex *ix, struct node *n, const struct style *parent, struct sheet *ua, int depth) {
    for (struct node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEMENT) continue;
        // ent arrays are static: styling a child clobbers them, but each node is finished before its children start
        style_node(env, ix, c, parent, ua);
        if (c->style->display == D_NONE) {
            // still style the subtree lightly? Skipped: hidden elements make no boxes.
            continue;
        }
        if (depth < 380) style_tree(env, ix, c, c->style, ua, depth + 1);
    }
}

static bool tree_has_style_attr(struct node *n, int depth) {
    for (struct node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEMENT) continue;
        if (node_attr(c, "style") || c->tag == T_STYLE) return true;
        if (depth < 200 && tree_has_style_attr(c, depth + 1)) return true;
    }
    return false;
}

void style_compute_all(struct page *p) {
    media_epoch++;
    struct rindex *ix = w_alloc(&p->larena, sizeof(struct rindex));
    int author = 0;
    for (struct sheet *sh = p->sheets; sh; sh = sh->next) if (sh->count) author++;
    p->plain = author == 0 && !tree_has_style_attr(p->root, 0);
    bool dark_ua = p->dark && p->plain;
    struct sheet *ua = ua_sheet(p, dark_ua);
    index_sheet(p, ix, ua, 0);
    for (struct sheet *sh = p->sheets; sh; sh = sh->next) index_sheet(p, ix, sh, 0);
    css_node_prepare(p->root);
    static struct style initial_copy;
    initial_copy = *css_default_style();
    struct cascade_env env;
    env.page = p; env.root_px = 16; env.vw = p->viewport_w; env.vh = p->viewport_h; env.initial = &initial_copy;
    style_node(&env, ix, p->root, NULL, ua);
    env.root_px = p->root->style->font_size > 0 ? p->root->style->font_size : 16;
    style_tree(&env, ix, p->root, p->root->style, ua, 0);
    // canvas color: the root's background, else the body's
    p->canvas_from_body = false;
    uint32_t canvas = p->root->style->bg_color;
    if ((canvas >> 24) == 0 && p->body && p->body->style && p->root->style->bg_count == 0) {
        canvas = p->body->style->bg_color;
        if ((canvas >> 24) != 0) p->canvas_from_body = true;
    }
    if ((canvas >> 24) == 0) canvas = dark_ua ? 0xFF121212u : 0xFFFFFFFFu;
    else if ((canvas >> 24) != 0xFF) {
        // blend over white
        uint32_t a = canvas >> 24, base = dark_ua ? 0x12 : 0xFF;
        uint32_t r = (((canvas >> 16) & 255) * a + base * (255 - a)) / 255, g = (((canvas >> 8) & 255) * a + base * (255 - a)) / 255, b = ((canvas & 255) * a + base * (255 - a)) / 255;
        canvas = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    p->canvas_color = canvas;
    p->canvas_set = true;
}

void css_style_page(struct page *p) { style_compute_all(p); }
