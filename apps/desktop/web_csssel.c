// web_csssel.c - CSS selectors: reading them, and testing them against elements.

#include "web_css.h"

enum {
    PS_FIRST_CHILD = 1 << 0, PS_LAST_CHILD = 1 << 1, PS_ONLY_CHILD = 1 << 2, PS_FIRST_OF_TYPE = 1 << 3,
    PS_LAST_OF_TYPE = 1 << 4, PS_ONLY_OF_TYPE = 1 << 5, PS_ROOT = 1 << 6, PS_EMPTY = 1 << 7, PS_CHECKED = 1 << 8,
    PS_DISABLED = 1 << 9, PS_ENABLED = 1 << 10, PS_LINK = 1 << 11, PS_REQUIRED = 1 << 12, PS_OPTIONAL = 1 << 13,
    PS_NEVER = 1 << 14, PS_READ_ONLY = 1 << 15, PS_READ_WRITE = 1 << 16, PS_PLACEHOLDER = 1 << 17, PS_OPEN = 1 << 18,
    PS_DEFAULT = 1 << 19, PS_ALWAYS = 1 << 20, PS_INDETERMINATE = 1 << 21, PS_NOT_PLACEHOLDER = 1 << 22,
};

struct attr_cond { const char *name; const char *value; int vlen; uint8_t op; bool ci; };
struct nth { int a, b; bool of_type, from_last; };

struct compound {
    const char *tag; int tag_len; uint8_t tagid;
    const char *id; int id_len;
    const char **cls; int *cls_len; int ncls;
    struct attr_cond *attrs; int nattrs;
    uint32_t pseudo;
    struct nth nth[2]; int nnth;
    struct selector **not_sels; int nnot;
    struct selector **is_sels; int nis;
    struct selector **has_sels; int nhas;
    uint8_t comb;
};

struct selector {
    struct compound *c;
    int n;
    int spec;
    uint8_t pseudo_elem;
};

static bool is_ident_char(int c) { return w_is_alnum(c) || c == '-' || c == '_' || c >= 0x80 || c == '\\'; }

static int read_ident(const char *s, int len, int i, int *out_len) {
    int j = i;
    while (j < len && is_ident_char((unsigned char)s[j])) {
        if (s[j] == '\\' && j + 1 < len) j++;
        j++;
    }
    *out_len = j - i;
    return j;
}

// Copy an identifier, removing backslash escapes.
static const char *copy_ident(struct page *p, const char *s, int n, int *out_n) {
    char *d = w_alloc(&p->arena, (size_t)n + 1);
    int o = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            // hex escape (\31 ) and plain escapes
            if (w_hex(s[i]) >= 0) {
                uint32_t cp = 0; int k = 0;
                while (i < n && k < 6 && w_hex(s[i]) >= 0) { cp = cp * 16 + (uint32_t)w_hex(s[i]); i++; k++; }
                if (i < n && w_is_space(s[i])) i++;
                i--;
                o += w_utf8_put(cp, d + o);
                continue;
            }
        }
        d[o++] = s[i];
    }
    d[o] = '\0';
    *out_n = o;
    return d;
}

static struct selector *parse_complex(struct page *p, const char *s, int len, int *used, bool *forgive, int depth);

static void parse_selector_args(struct page *p, const char *s, int len, struct selector ***out, int *count, int depth) {
    // comma separated complex selectors
    struct selector **list = NULL;
    int n = 0, cap = 0;
    int i = 0;
    while (i < len) {
        int u = 0;
        bool f = false;
        struct selector *sel = parse_complex(p, s + i, len - i, &u, &f, depth + 1);
        if (sel) {
            if (n == cap) {
                int nc = cap ? cap * 2 : 4;
                struct selector **nl = w_alloc(&p->arena, sizeof(struct selector *) * (size_t)nc);
                for (int k = 0; k < n; k++) nl[k] = list[k];
                list = nl; cap = nc;
            }
            list[n++] = sel;
        }
        i += u;
        while (i < len && w_is_space(s[i])) i++;
        if (i < len && s[i] == ',') i++;
        else if (i < len) break;
    }
    *out = list;
    *count = n;
}

static bool parse_nth(const char *s, int len, struct nth *nth) {
    // "odd", "even", "n", "2n+1", "-n+3", "5"
    int i = 0;
    while (i < len && w_is_space(s[i])) i++;
    int e = len;
    while (e > i && w_is_space(s[e - 1])) e--;
    char buf[40];
    int n = 0;
    for (int k = i; k < e && n < 39; k++) if (!w_is_space(s[k])) buf[n++] = (char)w_lower((unsigned char)s[k]);
    buf[n] = '\0';
    if (w_eq0(buf, "odd")) { nth->a = 2; nth->b = 1; return true; }
    if (w_eq0(buf, "even")) { nth->a = 2; nth->b = 0; return true; }
    int a = 0, b = 0, k = 0, sign = 1;
    bool has_n = false;
    // find 'n'
    int npos = -1;
    for (int q = 0; q < n; q++) if (buf[q] == 'n') { npos = q; break; }
    if (npos < 0) {
        if (n == 0) return false;
        if (buf[0] == '-') { sign = -1; k = 1; } else if (buf[0] == '+') k = 1;
        int v = 0; bool any = false;
        for (; k < n && w_is_digit(buf[k]); k++) { v = v * 10 + (buf[k] - '0'); any = true; }
        if (!any || k != n) return false;
        nth->a = 0; nth->b = sign * v;
        return true;
    }
    has_n = true;
    // a part
    k = 0;
    if (buf[0] == '-') { sign = -1; k = 1; } else if (buf[0] == '+') k = 1;
    int v = 0; bool any = false;
    for (; k < npos && w_is_digit(buf[k]); k++) { v = v * 10 + (buf[k] - '0'); any = true; }
    if (k != npos) return false;
    a = any ? sign * v : sign;
    k = npos + 1;
    if (k < n) {
        int s2 = 1;
        if (buf[k] == '-') { s2 = -1; k++; } else if (buf[k] == '+') k++;
        int bv = 0; bool bany = false;
        for (; k < n && w_is_digit(buf[k]); k++) { bv = bv * 10 + (buf[k] - '0'); bany = true; }
        if (!bany || k != n) return false;
        b = s2 * bv;
    }
    (void)has_n;
    nth->a = a; nth->b = b;
    return true;
}

static struct selector *parse_complex(struct page *p, const char *s, int len, int *used, bool *forgive, int depth) {
    (void)forgive;
    if (depth > 8) { *used = len; return NULL; }
    struct compound tmp[24];
    int n = 0;
    int i = 0;
    uint8_t pending_comb = 0;
    int spec_ids = 0, spec_cls = 0, spec_tags = 0;
    int pseudo_elem = 0;
    bool bad = false;
    while (i < len) {
        // skip whitespace, noting a descendant combinator
        int ws_start = i;
        while (i < len && w_is_space(s[i])) i++;
        bool had_space = i > ws_start;
        if (i >= len) break;
        char c = s[i];
        if (c == ',' || c == ')') break;
        if (c == '>' || c == '+' || c == '~') {
            if (n == 0) { bad = true; }
            pending_comb = (uint8_t)c;
            i++;
            while (i < len && w_is_space(s[i])) i++;
            continue;
        }
        if (had_space && n > 0 && !pending_comb) pending_comb = ' ';
        // a compound
        if (n >= 24) { bad = true; break; }
        struct compound *cp = &tmp[n];
        memset(cp, 0, sizeof(*cp));
        cp->comb = n == 0 ? 0 : (pending_comb ? pending_comb : ' ');
        pending_comb = 0;
        cp->tagid = 255;
        const char *cls_tmp[16]; int cls_len_tmp[16]; int ncls = 0;
        struct attr_cond attr_tmp[8]; int nattr = 0;
        bool any = false;
        while (i < len) {
            c = s[i];
            if (w_is_space(c) || c == ',' || c == '>' || c == '+' || c == '~' || c == ')') break;
            any = true;
            if (c == '*') { i++; continue; }
            if (c == '&') { i++; continue; }
            if (c == '#') {
                int l; int j = read_ident(s, len, i + 1, &l);
                cp->id = copy_ident(p, s + i + 1, l, &cp->id_len);
                spec_ids++;
                i = j;
                continue;
            }
            if (c == '.') {
                int l; int j = read_ident(s, len, i + 1, &l);
                if (ncls < 16) { cls_tmp[ncls] = copy_ident(p, s + i + 1, l, &cls_len_tmp[ncls]); ncls++; }
                spec_cls++;
                i = j;
                continue;
            }
            if (c == '[') {
                int j = i + 1;
                int depth_b = 1;
                while (j < len && depth_b > 0) {
                    if (s[j] == '"' || s[j] == '\'') { char q = s[j++]; while (j < len && s[j] != q) j++; }
                    else if (s[j] == '[') depth_b++;
                    else if (s[j] == ']') depth_b--;
                    j++;
                }
                int inner_end = j - 1;
                // name
                int k = i + 1;
                while (k < inner_end && w_is_space(s[k])) k++;
                int nl; int ne = read_ident(s, inner_end, k, &nl);
                struct attr_cond ac;
                memset(&ac, 0, sizeof(ac));
                int dummy;
                char *lower = (char *)copy_ident(p, s + k, nl, &dummy);
                for (int q = 0; lower[q]; q++) lower[q] = (char)w_lower((unsigned char)lower[q]);
                ac.name = lower;
                k = ne;
                while (k < inner_end && w_is_space(s[k])) k++;
                if (k < inner_end) {
                    int op = 1;
                    if (s[k] == '~') { op = 2; k++; } else if (s[k] == '|') { op = 3; k++; } else if (s[k] == '^') { op = 4; k++; }
                    else if (s[k] == '$') { op = 5; k++; } else if (s[k] == '*') { op = 6; k++; }
                    if (k < inner_end && s[k] == '=') k++;
                    while (k < inner_end && w_is_space(s[k])) k++;
                    int vs, ve;
                    if (k < inner_end && (s[k] == '"' || s[k] == '\'')) {
                        char q = s[k]; vs = k + 1; k++;
                        while (k < inner_end && s[k] != q) k++;
                        ve = k; if (k < inner_end) k++;
                    } else {
                        vs = k;
                        while (k < inner_end && !w_is_space(s[k])) k++;
                        ve = k;
                    }
                    ac.op = (uint8_t)op;
                    ac.value = copy_ident(p, s + vs, ve - vs, &ac.vlen);
                    while (k < inner_end && w_is_space(s[k])) k++;
                    if (k < inner_end && (s[k] == 'i' || s[k] == 'I')) ac.ci = true;
                }
                if (nattr < 8) attr_tmp[nattr++] = ac;
                spec_cls++;
                i = j;
                continue;
            }
            if (c == ':') {
                bool dbl = i + 1 < len && s[i + 1] == ':';
                int start = i + (dbl ? 2 : 1);
                int l; int j = read_ident(s, len, start, &l);
                const char *name = s + start;
                int args_start = -1, args_end = -1;
                if (j < len && s[j] == '(') {
                    int depth_p = 1, k = j + 1;
                    args_start = k;
                    while (k < len && depth_p > 0) {
                        if (s[k] == '"' || s[k] == '\'') { char q = s[k++]; while (k < len && s[k] != q) k++; }
                        else if (s[k] == '(') depth_p++;
                        else if (s[k] == ')') depth_p--;
                        k++;
                    }
                    args_end = k - 1;
                    j = k;
                }
                i = j;
                if (dbl || w_eq(name, l, "before") || w_eq(name, l, "after") || w_eq(name, l, "first-line") || w_eq(name, l, "first-letter")) {
                    if (w_eq(name, l, "before")) { pseudo_elem = 1; spec_tags++; }
                    else if (w_eq(name, l, "after")) { pseudo_elem = 2; spec_tags++; }
                    else bad = true;     // ::placeholder, ::selection and friends aren't drawn
                    continue;
                }
                if (w_eq(name, l, "first-child")) { cp->pseudo |= PS_FIRST_CHILD; spec_cls++; }
                else if (w_eq(name, l, "last-child")) { cp->pseudo |= PS_LAST_CHILD; spec_cls++; }
                else if (w_eq(name, l, "only-child")) { cp->pseudo |= PS_ONLY_CHILD; spec_cls++; }
                else if (w_eq(name, l, "first-of-type")) { cp->pseudo |= PS_FIRST_OF_TYPE; spec_cls++; }
                else if (w_eq(name, l, "last-of-type")) { cp->pseudo |= PS_LAST_OF_TYPE; spec_cls++; }
                else if (w_eq(name, l, "only-of-type")) { cp->pseudo |= PS_ONLY_OF_TYPE; spec_cls++; }
                else if (w_eq(name, l, "root") || w_eq(name, l, "scope")) { cp->pseudo |= PS_ROOT; spec_cls++; }
                else if (w_eq(name, l, "empty")) { cp->pseudo |= PS_EMPTY; spec_cls++; }
                else if (w_eq(name, l, "checked")) { cp->pseudo |= PS_CHECKED; spec_cls++; }
                else if (w_eq(name, l, "disabled")) { cp->pseudo |= PS_DISABLED; spec_cls++; }
                else if (w_eq(name, l, "enabled")) { cp->pseudo |= PS_ENABLED; spec_cls++; }
                else if (w_eq(name, l, "link") || w_eq(name, l, "any-link")) { cp->pseudo |= PS_LINK; spec_cls++; }
                else if (w_eq(name, l, "required")) { cp->pseudo |= PS_REQUIRED; spec_cls++; }
                else if (w_eq(name, l, "optional")) { cp->pseudo |= PS_OPTIONAL; spec_cls++; }
                else if (w_eq(name, l, "read-only")) { cp->pseudo |= PS_READ_ONLY; spec_cls++; }
                else if (w_eq(name, l, "read-write")) { cp->pseudo |= PS_READ_WRITE; spec_cls++; }
                else if (w_eq(name, l, "placeholder-shown")) { cp->pseudo |= PS_PLACEHOLDER; spec_cls++; }
                else if (w_eq(name, l, "open")) { cp->pseudo |= PS_OPEN; spec_cls++; }
                else if (w_eq(name, l, "default")) { cp->pseudo |= PS_DEFAULT; spec_cls++; }
                else if (w_eq(name, l, "defined") || w_eq(name, l, "lang") || w_eq(name, l, "dir") || w_eq(name, l, "modal") || w_eq(name, l, "popover-open")) {
                    cp->pseudo |= w_eq(name, l, "popover-open") || w_eq(name, l, "modal") ? PS_NEVER : PS_ALWAYS; spec_cls++;
                }
                else if (w_eq(name, l, "indeterminate")) { cp->pseudo |= PS_NEVER; spec_cls++; }
                else if (w_eq(name, l, "hover") || w_eq(name, l, "focus") || w_eq(name, l, "active") || w_eq(name, l, "visited")
                         || w_eq(name, l, "focus-visible") || w_eq(name, l, "focus-within") || w_eq(name, l, "target")
                         || w_eq(name, l, "invalid") || w_eq(name, l, "valid") || w_eq(name, l, "user-invalid") || w_eq(name, l, "in-range")
                         || w_eq(name, l, "out-of-range") || w_eq(name, l, "fullscreen") || w_eq(name, l, "autofill")
                         || w_eq(name, l, "-webkit-autofill") || w_eq(name, l, "target-within") || w_eq(name, l, "current")
                         || w_eq(name, l, "past") || w_eq(name, l, "future") || w_eq(name, l, "playing") || w_eq(name, l, "paused")
                         || w_eq(name, l, "local-link") || w_eq(name, l, "host") || w_eq(name, l, "state")) { cp->pseudo |= PS_NEVER; spec_cls++; }
                else if (w_eq(name, l, "not") && args_start >= 0) {
                    struct selector **list; int cnt;
                    parse_selector_args(p, s + args_start, args_end - args_start, &list, &cnt, depth);
                    int best = 0;
                    for (int q = 0; q < cnt; q++) if (list[q]->spec > best) best = list[q]->spec;
                    spec_ids += (best >> 16) & 255; spec_cls += (best >> 8) & 255; spec_tags += best & 255;
                    if (cnt == 0) { bad = true; }
                    else if (cp->nnot == 0) { cp->not_sels = list; cp->nnot = cnt; }
                    else {
                        // two :not()s: merge the lists (both must fail to match: treated as one "none of these")
                        struct selector **merged = w_alloc(&p->arena, sizeof(struct selector *) * (size_t)(cp->nnot + cnt));
                        for (int q = 0; q < cp->nnot; q++) merged[q] = cp->not_sels[q];
                        for (int q = 0; q < cnt; q++) merged[cp->nnot + q] = list[q];
                        cp->not_sels = merged; cp->nnot += cnt;
                    }
                }
                else if ((w_eq(name, l, "is") || w_eq(name, l, "where") || w_eq(name, l, "matches") || w_eq(name, l, "-webkit-any") || w_eq(name, l, "-moz-any")) && args_start >= 0) {
                    struct selector **list; int cnt;
                    parse_selector_args(p, s + args_start, args_end - args_start, &list, &cnt, depth);
                    if (!w_eq(name, l, "where")) {
                        int best = 0;
                        for (int q = 0; q < cnt; q++) if (list[q]->spec > best) best = list[q]->spec;
                        spec_ids += (best >> 16) & 255; spec_cls += (best >> 8) & 255; spec_tags += best & 255;
                    }
                    if (cp->nis == 0) { cp->is_sels = list; cp->nis = cnt; if (cnt == 0) bad = true; }
                    else {
                        // :is(a):is(b) both must match: keep only the first (rare)
                    }
                }
                else if (w_eq(name, l, "has") && args_start >= 0) {
                    struct selector **list; int cnt;
                    parse_selector_args(p, s + args_start, args_end - args_start, &list, &cnt, depth);
                    int best = 0;
                    for (int q = 0; q < cnt; q++) if (list[q]->spec > best) best = list[q]->spec;
                    spec_ids += (best >> 16) & 255; spec_cls += (best >> 8) & 255; spec_tags += best & 255;
                    cp->has_sels = list; cp->nhas = cnt;
                    if (cnt == 0) bad = true;
                }
                else if ((w_eq(name, l, "nth-child") || w_eq(name, l, "nth-last-child") || w_eq(name, l, "nth-of-type") || w_eq(name, l, "nth-last-of-type")) && args_start >= 0) {
                    int arg_len = args_end - args_start;
                    // "an+b of S": ignore the "of S" part
                    for (int q = 0; q + 3 < arg_len; q++) {
                        if (w_is_space(s[args_start + q]) && w_lower((unsigned char)s[args_start + q + 1]) == 'o' && w_lower((unsigned char)s[args_start + q + 2]) == 'f' && w_is_space(s[args_start + q + 3])) { arg_len = q; break; }
                    }
                    struct nth nt;
                    memset(&nt, 0, sizeof(nt));
                    if (!parse_nth(s + args_start, arg_len, &nt)) bad = true;
                    nt.of_type = w_eq(name, l, "nth-of-type") || w_eq(name, l, "nth-last-of-type");
                    nt.from_last = w_eq(name, l, "nth-last-child") || w_eq(name, l, "nth-last-of-type");
                    if (cp->nnth < 2) cp->nth[cp->nnth++] = nt;
                    spec_cls++;
                }
                else bad = true;
                continue;
            }
            if (is_ident_char((unsigned char)c)) {
                int l; int j = read_ident(s, len, i, &l);
                cp->tag = copy_ident(p, s + i, l, &cp->tag_len);
                char *lower = (char *)cp->tag;
                for (int q = 0; lower[q]; q++) lower[q] = (char)w_lower((unsigned char)lower[q]);
                cp->tagid = (uint8_t)tag_from_name(cp->tag, cp->tag_len);
                if (cp->tagid == T_OTHER) cp->tagid = 255;
                spec_tags++;
                i = j;
                continue;
            }
            bad = true;
            i++;
        }
        if (!any) { bad = true; break; }
        if (ncls > 0) {
            cp->cls = w_alloc(&p->arena, sizeof(char *) * (size_t)ncls);
            cp->cls_len = w_alloc(&p->arena, sizeof(int) * (size_t)ncls);
            for (int q = 0; q < ncls; q++) { cp->cls[q] = cls_tmp[q]; cp->cls_len[q] = cls_len_tmp[q]; }
            cp->ncls = ncls;
        }
        if (nattr > 0) {
            cp->attrs = w_alloc(&p->arena, sizeof(struct attr_cond) * (size_t)nattr);
            for (int q = 0; q < nattr; q++) cp->attrs[q] = attr_tmp[q];
            cp->nattrs = nattr;
        }
        n++;
    }
    *used = i;
    if (bad || n == 0) return NULL;
    struct selector *sel = w_alloc(&p->arena, sizeof(struct selector));
    sel->c = w_alloc(&p->arena, sizeof(struct compound) * (size_t)n);
    for (int q = 0; q < n; q++) sel->c[q] = tmp[q];
    sel->n = n;
    if (spec_ids > 255) spec_ids = 255;
    if (spec_cls > 255) spec_cls = 255;
    if (spec_tags > 255) spec_tags = 255;
    sel->spec = (spec_ids << 16) | (spec_cls << 8) | spec_tags;
    sel->pseudo_elem = (uint8_t)pseudo_elem;
    return sel;
}

void css_parse_selector_list(struct page *p, const char *text, int length, struct selector ***out, int *count) {
    parse_selector_args(p, text, length, out, count, 0);
}

int css_selector_specificity(const struct selector *s) { return s->spec; }
int css_selector_pseudo_element(const struct selector *s) { return s->pseudo_elem; }

void css_selector_key(const struct selector *s, const char **key, int *kind) {
    const struct compound *c = &s->c[s->n - 1];
    if (c->id) { *key = c->id; *kind = 1; return; }
    if (c->ncls > 0) { *key = c->cls[0]; *kind = 2; return; }
    if (c->tag) { *key = c->tag; *kind = 3; return; }
    *key = NULL; *kind = 0;
}

// ---- Matching ----

static bool class_has(const char *cls, const char *name, int nlen) {
    if (!cls) return false;
    const char *p = cls;
    while (*p) {
        while (*p && w_is_space(*p)) p++;
        const char *start = p;
        while (*p && !w_is_space(*p)) p++;
        if ((int)(p - start) == nlen && memcmp(start, name, (size_t)nlen) == 0) return true;
    }
    return false;
}

static bool attr_matches(const struct node *n, const struct attr_cond *ac) {
    const char *v = node_attr(n, ac->name);
    if (!v) return false;
    if (ac->op == 0) return true;
    int vl = w_len(v);
    const char *want = ac->value;
    int wl = ac->vlen;
    #define EQC(a, b) (ac->ci ? w_lower((unsigned char)(a)) == w_lower((unsigned char)(b)) : (a) == (b))
    switch (ac->op) {
    case 1:
        if (vl != wl) return false;
        for (int i = 0; i < vl; i++) if (!EQC(v[i], want[i])) return false;
        return true;
    case 2: {
        if (wl == 0) return false;
        const char *p = v;
        while (*p) {
            while (*p && w_is_space(*p)) p++;
            const char *st = p;
            while (*p && !w_is_space(*p)) p++;
            if ((int)(p - st) == wl) { bool ok = true; for (int i = 0; i < wl; i++) if (!EQC(st[i], want[i])) { ok = false; break; } if (ok) return true; }
        }
        return false;
    }
    case 3:
        if (vl < wl) return false;
        for (int i = 0; i < wl; i++) if (!EQC(v[i], want[i])) return false;
        return vl == wl || v[wl] == '-';
    case 4:
        if (wl == 0 || vl < wl) return false;
        for (int i = 0; i < wl; i++) if (!EQC(v[i], want[i])) return false;
        return true;
    case 5:
        if (wl == 0 || vl < wl) return false;
        for (int i = 0; i < wl; i++) if (!EQC(v[vl - wl + i], want[i])) return false;
        return true;
    case 6:
        if (wl == 0) return false;
        for (int i = 0; i + wl <= vl; i++) {
            bool ok = true;
            for (int k = 0; k < wl; k++) if (!EQC(v[i + k], want[k])) { ok = false; break; }
            if (ok) return true;
        }
        return false;
    }
    #undef EQC
    return false;
}

static struct node *prev_element(struct node *n) {
    for (struct node *x = n->prev; x; x = x->prev) if (x->type == N_ELEMENT) return x;
    return NULL;
}

static bool match_selector(const struct selector *s, struct node *n);
static bool match_from(const struct selector *s, int i, struct node *n);

static bool nth_matches(const struct nth *nt, const struct node *n) {
    int pos, total;
    if (nt->of_type) { pos = n->type_index; total = n->type_count; }
    else { pos = n->index; total = n->sibling_count; }
    if (nt->from_last) pos = total - pos + 1;
    if (nt->a == 0) return pos == nt->b;
    int diff = pos - nt->b;
    if (diff % nt->a != 0) return false;
    return diff / nt->a >= 0;
}

static bool has_match(const struct selector *s, struct node *scope) {
    // :has(> a), :has(a), :has(+ b): the selector is relative to `scope`. We handle plain descendant and child forms.
    // (our parser dropped a leading combinator, so treat everything as "somewhere below")
    struct node *stack[64];
    int sp = 0;
    for (struct node *c = scope->first; c; c = c->next) if (c->type == N_ELEMENT) { if (sp < 64) stack[sp++] = c; }
    int budget = 3000;
    while (sp > 0 && budget-- > 0) {
        struct node *x = stack[--sp];
        if (match_selector(s, x)) return true;
        for (struct node *c = x->first; c; c = c->next) if (c->type == N_ELEMENT && sp < 64) stack[sp++] = c;
    }
    return false;
}

static bool match_compound(const struct compound *c, struct node *n) {
    if (c->tag && c->tagid != 255) { if (n->tag != c->tagid) return false; }
    else if (c->tag) { if (n->tag != T_OTHER || !w_eq0(n->name, c->tag)) { 
        // svg elements keep a lower-case name; compare case-insensitively
        if (!w_eq0(n->name, c->tag)) return false; } }
    if (c->id) { if (!n->id || strcmp(n->id, c->id) != 0) return false; }
    for (int i = 0; i < c->ncls; i++) if (!class_has(n->cls, c->cls[i], c->cls_len[i])) return false;
    for (int i = 0; i < c->nattrs; i++) if (!attr_matches(n, &c->attrs[i])) return false;
    uint32_t ps = c->pseudo;
    if (ps) {
        if (ps & PS_NEVER) return false;
        if ((ps & PS_FIRST_CHILD) && n->index != 1) return false;
        if ((ps & PS_LAST_CHILD) && n->index != n->sibling_count) return false;
        if ((ps & PS_ONLY_CHILD) && n->sibling_count != 1) return false;
        if ((ps & PS_FIRST_OF_TYPE) && n->type_index != 1) return false;
        if ((ps & PS_LAST_OF_TYPE) && n->type_index != n->type_count) return false;
        if ((ps & PS_ONLY_OF_TYPE) && n->type_count != 1) return false;
        if ((ps & PS_ROOT) && n->tag != T_HTML) return false;
        if (ps & PS_EMPTY) { for (struct node *k = n->first; k; k = k->next) if (k->type == N_ELEMENT || k->text_len > 0) return false; }
        if (ps & PS_CHECKED) {
            if (!(node_has_attr(n, "checked") || (n->tag == T_OPTION && node_has_attr(n, "selected")))) return false;
        }
        if (ps & PS_DISABLED) {
            if (!node_has_attr(n, "disabled")) {
                bool in_disabled = false;
                for (struct node *a = n->parent; a; a = a->parent) if (a->tag == T_FIELDSET && node_has_attr(a, "disabled")) { in_disabled = true; break; }
                if (!in_disabled) return false;
            }
            if (n->tag != T_INPUT && n->tag != T_BUTTON && n->tag != T_SELECT && n->tag != T_TEXTAREA && n->tag != T_FIELDSET && n->tag != T_OPTION && n->tag != T_OPTGROUP) return false;
        }
        if (ps & PS_ENABLED) {
            if (node_has_attr(n, "disabled")) return false;
            if (n->tag != T_INPUT && n->tag != T_BUTTON && n->tag != T_SELECT && n->tag != T_TEXTAREA) return false;
        }
        if ((ps & PS_LINK) && !((n->tag == T_A || n->tag == T_AREA) && node_has_attr(n, "href"))) return false;
        if ((ps & PS_REQUIRED) && !node_has_attr(n, "required")) return false;
        if ((ps & PS_OPTIONAL) && (node_has_attr(n, "required") || (n->tag != T_INPUT && n->tag != T_SELECT && n->tag != T_TEXTAREA))) return false;
        if (ps & PS_READ_ONLY) { if (!((n->tag != T_INPUT && n->tag != T_TEXTAREA) || node_has_attr(n, "readonly") || node_has_attr(n, "disabled"))) return false; }
        if (ps & PS_READ_WRITE) { if (!((n->tag == T_INPUT || n->tag == T_TEXTAREA) && !node_has_attr(n, "readonly") && !node_has_attr(n, "disabled"))) return false; }
        if (ps & PS_PLACEHOLDER) { const char *v = node_attr(n, "value"); if (!node_has_attr(n, "placeholder") || (v && *v)) return false; }
        if (ps & PS_OPEN) { if (!node_has_attr(n, "open")) return false; }
        if (ps & PS_DEFAULT) { if (!(node_has_attr(n, "checked") || node_has_attr(n, "selected"))) return false; }
    }
    for (int i = 0; i < c->nnth; i++) if (!nth_matches(&c->nth[i], n)) return false;
    if (c->nnot) { for (int i = 0; i < c->nnot; i++) if (match_selector(c->not_sels[i], n)) return false; }
    if (c->nis) {
        bool any = false;
        for (int i = 0; i < c->nis; i++) if (match_selector(c->is_sels[i], n)) { any = true; break; }
        if (!any) return false;
    }
    if (c->nhas) {
        bool any = false;
        for (int i = 0; i < c->nhas; i++) if (has_match(c->has_sels[i], n)) { any = true; break; }
        if (!any) return false;
    }
    return true;
}

static bool match_from(const struct selector *s, int i, struct node *n) {
    if (!match_compound(&s->c[i], n)) return false;
    if (i == 0) return true;
    switch (s->c[i].comb) {
    case '>': {
        struct node *p = n->parent;
        return p && p->type == N_ELEMENT && match_from(s, i - 1, p);
    }
    case '+': {
        struct node *pr = prev_element(n);
        return pr && match_from(s, i - 1, pr);
    }
    case '~': {
        for (struct node *pr = prev_element(n); pr; pr = prev_element(pr)) if (match_from(s, i - 1, pr)) return true;
        return false;
    }
    default:
        for (struct node *p = n->parent; p; p = p->parent) if (p->type == N_ELEMENT && match_from(s, i - 1, p)) return true;
        return false;
    }
}

static bool match_selector(const struct selector *s, struct node *n) {
    return match_from(s, s->n - 1, n);
}

bool css_selector_matches(const struct selector *s, struct node *n) { return match_selector(s, n); }

// ---- Sibling numbering (for :nth-child and friends) ----

static void prepare(struct node *parent) {
    int count = 0;
    int per_tag[T_COUNT + 1];
    memset(per_tag, 0, sizeof(per_tag));
    for (struct node *c = parent->first; c; c = c->next) if (c->type == N_ELEMENT) { count++; per_tag[c->tag]++; }
    int idx = 0;
    int seen[T_COUNT + 1];
    memset(seen, 0, sizeof(seen));
    for (struct node *c = parent->first; c; c = c->next) {
        if (c->type != N_ELEMENT) continue;
        idx++;
        c->index = idx;
        c->sibling_count = count;
        if (c->tag != T_OTHER || count > 300) {
            c->type_index = ++seen[c->tag];
            c->type_count = per_tag[c->tag];
        } else {
            int ti = 0, tc = 0;
            for (struct node *x = parent->first; x; x = x->next) {
                if (x->type == N_ELEMENT && x->tag == T_OTHER && w_eq0(x->name, c->name)) { tc++; if (x == c) ti = tc; }
            }
            c->type_index = ti;
            c->type_count = tc;
        }
    }
    for (struct node *c = parent->first; c; c = c->next) if (c->type == N_ELEMENT && c->first) prepare(c);
}

void css_node_prepare(struct node *root) {
    root->index = 1; root->sibling_count = 1; root->type_index = 1; root->type_count = 1;
    prepare(root);
}
