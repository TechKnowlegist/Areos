// svg.c - vector pictures: <svg> inside a page, and .svg files.
//
// Shapes (paths, rectangles, circles, lines, polygons...) are flattened into
// straight lines, then filled by adding up how much of each pixel a line
// covers (so edges are smooth), and stroked by filling a thin shape around the
// line. Colors, gradients, transforms, <use> and <defs> work; text, masks,
// filters and clipping don't. This one file is built with floating point
// switched on; the rest of the browser sees only whole numbers.

#include "web_css.h"
#include <math.h>

static float fabsf_(float x) { return x < 0 ? -x : x; }
static float cosf_(float x) { return (float)cos((double)x); }
static float sinf_(float x) { return (float)sin((double)x); }
static float tanf_(float x) { return (float)tan((double)x); }
static float sqrtf_(float x) { return (float)sqrt((double)x); }
static float floorf_(float x) { return (float)floor((double)x); }
static float ceilf_(float x) { return (float)ceil((double)x); }
static float atan2f_(float y, float x) { return (float)atan2((double)y, (double)x); }
static float fminf_(float a, float b) { return a < b ? a : b; }
static const char *strrchr_(const char *s, int c) { const char *r = NULL; for (; *s; s++) if (*s == (char)c) r = s; return r; }
static const char *strstr_(const char *h, const char *n) { int nl = (int)strlen(n); for (; *h; h++) if (w_starts(h, n)) return h; (void)nl; return NULL; }
static float fmaxf_(float a, float b) { return a > b ? a : b; }

struct svg_doc {
    struct page *page;          // the page we parsed a file into (owned), or NULL
    struct node *root;          // the <svg> element
    struct page *host;          // the page the element is in (for styles)
    float vb_x, vb_y, vb_w, vb_h;
    bool has_vb;
    int width, height;          // size in pixels (0: not given)
    uint32_t cc;                // currentColor
};

// ---------------------------------------------------------------- numbers

static float parse_num(const char **pp, bool *ok) {
    const char *p = *pp;
    while (w_is_space(*p) || *p == ',') p++;
    const char *start = p;
    float sign = 1.f, v = 0.f;
    if (*p == '-') { sign = -1.f; p++; } else if (*p == '+') p++;
    bool digits = false;
    while (w_is_digit(*p)) { v = v * 10.f + (float)(*p - '0'); p++; digits = true; }
    if (*p == '.') {
        p++;
        float f = 0.1f;
        while (w_is_digit(*p)) { v += f * (float)(*p - '0'); f *= 0.1f; p++; digits = true; }
    }
    if (digits && (*p == 'e' || *p == 'E') && (w_is_digit(p[1]) || ((p[1] == '-' || p[1] == '+') && w_is_digit(p[2])))) {
        p++;
        int es = 1;
        if (*p == '-') { es = -1; p++; } else if (*p == '+') p++;
        int e = 0;
        while (w_is_digit(*p)) { e = e * 10 + (*p - '0'); if (e > 60) e = 60; p++; }
        float m = 1.f;
        for (int i = 0; i < e; i++) m *= 10.f;
        v = es > 0 ? v * m : v / m;
    }
    if (!digits) { *ok = false; *pp = start; return 0.f; }
    *ok = true;
    *pp = p;
    return sign * v;
}

// A length such as "24", "1.5em", "50%". `pct_of`: what 100% is. Returns false if there isn't one.
static bool parse_len(const char *s, float em, float pct_of, float *out) {
    if (!s) return false;
    bool ok;
    const char *p = s;
    float v = parse_num(&p, &ok);
    if (!ok) return false;
    while (w_is_space(*p)) p++;
    if (w_starts(p, "px") || *p == '\0') { *out = v; return true; }
    if (w_starts(p, "em")) { *out = v * em; return true; }
    if (w_starts(p, "rem")) { *out = v * 16.f; return true; }
    if (*p == '%') { *out = v * pct_of / 100.f; return true; }
    if (w_starts(p, "pt")) { *out = v * 4.f / 3.f; return true; }
    if (w_starts(p, "pc")) { *out = v * 16.f; return true; }
    if (w_starts(p, "mm")) { *out = v * 3.7795f; return true; }
    if (w_starts(p, "cm")) { *out = v * 37.795f; return true; }
    if (w_starts(p, "in")) { *out = v * 96.f; return true; }
    *out = v;
    return true;
}

// ---------------------------------------------------------------- matrices

struct mat { float a, b, c, d, e, f; };      // x' = a x + c y + e,  y' = b x + d y + f

static struct mat m_id(void) { struct mat m = { 1, 0, 0, 1, 0, 0 }; return m; }
static struct mat m_mul(struct mat p, struct mat q) {       // apply q first, then p
    struct mat r;
    r.a = p.a * q.a + p.c * q.b; r.b = p.b * q.a + p.d * q.b;
    r.c = p.a * q.c + p.c * q.d; r.d = p.b * q.c + p.d * q.d;
    r.e = p.a * q.e + p.c * q.f + p.e; r.f = p.b * q.e + p.d * q.f + p.f;
    return r;
}
static void m_pt(const struct mat *m, float x, float y, float *ox, float *oy) {
    *ox = m->a * x + m->c * y + m->e;
    *oy = m->b * x + m->d * y + m->f;
}
static bool m_inv(const struct mat *m, struct mat *o) {
    float det = m->a * m->d - m->b * m->c;
    if (fabsf_(det) < 1e-9f) return false;
    float id = 1.f / det;
    o->a = m->d * id; o->b = -m->b * id; o->c = -m->c * id; o->d = m->a * id;
    o->e = -(o->a * m->e + o->c * m->f);
    o->f = -(o->b * m->e + o->d * m->f);
    return true;
}

// ---------------------------------------------------------------- transform="..." lists

static struct mat parse_transform(const char *s) {
    struct mat m = m_id();
    if (!s) return m;
    const char *p = s;
    while (*p) {
        while (w_is_space(*p) || *p == ',') p++;
        if (!*p) break;
        const char *name = p;
        while (w_is_alpha(*p)) p++;
        int nl = (int)(p - name);
        while (w_is_space(*p)) p++;
        if (*p != '(') break;
        p++;
        float v[6] = { 0 };
        int n = 0;
        for (;;) {
            bool ok;
            const char *q = p;
            float x = parse_num(&q, &ok);
            if (!ok) break;
            if (n < 6) v[n++] = x;
            p = q;
        }
        while (*p && *p != ')') p++;
        if (*p == ')') p++;
        struct mat t = m_id();
        if (nl == 9 && w_eq(name, 9, "translate")) { t.e = v[0]; t.f = n > 1 ? v[1] : 0; }
        else if (nl == 5 && w_eq(name, 5, "scale")) { t.a = v[0]; t.d = n > 1 ? v[1] : v[0]; }
        else if (nl == 6 && w_eq(name, 6, "rotate")) {
            float r = v[0] * 3.14159265f / 180.f, c = cosf_(r), sn = sinf_(r);
            t.a = c; t.b = sn; t.c = -sn; t.d = c;
            if (n >= 3) {
                struct mat to = m_id(), back = m_id();
                to.e = v[1]; to.f = v[2]; back.e = -v[1]; back.f = -v[2];
                t = m_mul(to, m_mul(t, back));
            }
        }
        else if (nl == 5 && w_eq(name, 5, "skewx")) { t.c = tanf_(v[0] * 3.14159265f / 180.f); }
        else if (nl == 5 && w_eq(name, 5, "skewy")) { t.b = tanf_(v[0] * 3.14159265f / 180.f); }
        else if (nl == 6 && w_eq(name, 6, "matrix") && n == 6) { t.a = v[0]; t.b = v[1]; t.c = v[2]; t.d = v[3]; t.e = v[4]; t.f = v[5]; }
        m = m_mul(m, t);
    }
    return m;
}

// ---------------------------------------------------------------- attributes and style

// An attribute by name, ignoring upper and lower case.
static const char *aget(const struct node *n, const char *name) {
    for (const struct attr *a = n->attrs; a; a = a->next) if (w_eq0(a->name, name)) return a->value;
    return NULL;
}

// A property: the style="..." attribute first, then the attribute of the same name. Copies into out.
static bool prop(const struct node *n, const char *name, char *out, int max) {
    const char *st = aget(n, "style");
    int nl = (int)strlen(name);
    if (st) {
        const char *p = st;
        while (*p) {
            while (w_is_space(*p) || *p == ';') p++;
            const char *k = p;
            while (*p && *p != ':' && *p != ';') p++;
            int kl = (int)(p - k);
            while (kl > 0 && w_is_space(k[kl - 1])) kl--;
            if (*p == ':') {
                p++;
                while (w_is_space(*p)) p++;
                const char *v = p;
                while (*p && *p != ';') p++;
                int vl = (int)(p - v);
                while (vl > 0 && w_is_space(v[vl - 1])) vl--;
                if (kl == nl && w_eq(k, kl, name) && vl > 0) {
                    if (vl >= max) vl = max - 1;
                    memcpy(out, v, (size_t)vl);
                    out[vl] = '\0';
                    return true;
                }
            }
        }
    }
    const char *v = aget(n, name);
    if (v) {
        while (w_is_space(*v)) v++;
        int vl = (int)strlen(v);
        while (vl > 0 && w_is_space(v[vl - 1])) vl--;
        if (vl >= max) vl = max - 1;
        memcpy(out, v, (size_t)vl);
        out[vl] = '\0';
        return vl > 0;
    }
    return false;
}

static struct node *find_by_id(struct node *n, const char *id, int depth) {
    if (depth > 200) return NULL;
    for (struct node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEMENT) continue;
        if (c->id && strcmp(c->id, id) == 0) return c;
        struct node *r = find_by_id(c, id, depth + 1);
        if (r) return r;
    }
    return NULL;
}

// ---------------------------------------------------------------- the state while drawing

struct paint { uint32_t color; const struct node *ref; bool none; };

struct gs {
    struct paint fill, stroke;
    float fill_op, stroke_op, op;
    float sw, miter;
    int cap, join;                  // 0 butt/miter, 1 round, 2 square/bevel
    bool evenodd;
    float dash[16]; int ndash; float doff;
    uint32_t cc;
    float font_px;
    bool hidden;
};

struct ctx {
    struct svg_doc *doc;
    uint32_t *out; int w, h;
    int budget;                     // shapes left to draw (a safety limit)
    int use_depth;
    float vw, vh;                   // the viewport size in user units (for percentages)
};

// "#name", "url(#name)" or "http://x/page#name" -> the element
static struct node *resolve_ref(struct node *scope, const char *s) {
    if (!s) return NULL;
    const char *h = strrchr_(s, '#');
    if (!h) return NULL;
    h++;
    char id[100];
    int k = 0;
    while (*h && *h != ')' && *h != '\'' && *h != '"' && !w_is_space(*h) && k < 99) id[k++] = *h++;
    id[k] = '\0';
    if (!k) return NULL;
    struct node *r = scope;
    while (r->parent) r = r->parent;
    return find_by_id(r, id, 0);
}

static bool parse_paint(struct node *scope, const char *v, uint32_t cc, struct paint *out) {
    while (w_is_space(*v)) v++;
    if (w_eq0(v, "none")) { out->none = true; out->ref = NULL; out->color = 0; return true; }
    if (w_starts(v, "url(")) {
        struct node *r = resolve_ref(scope, v);
        if (r) { out->ref = r; out->none = false; out->color = 0xFF000000u; return true; }
        const char *e = strchr(v, ')');
        if (!e) return false;
        v = e + 1;
        while (w_is_space(*v)) v++;
        if (!*v) { out->none = true; out->ref = NULL; return true; }
    }
    struct vp vp;
    vp_init(&vp, v, (int)strlen(v));
    struct css_ctx cx; memset(&cx, 0, sizeof(cx));
    cx.font_px = 16; cx.root_px = 16; cx.cur_color = cc;
    uint32_t col;
    if (!css_parse_color(&vp, &cx, &col)) return false;
    if (col == COLOR_CURRENT) col = cc;
    out->color = col; out->none = (col >> 24) == 0; out->ref = NULL;
    return true;
}

static void parse_dashes(const char *v, struct gs *g) {
    g->ndash = 0;
    if (!v || w_eq0(v, "none")) return;
    const char *p = v;
    for (;;) {
        bool ok;
        const char *q = p;
        float x = parse_num(&q, &ok);
        if (!ok) break;
        if (g->ndash < 16) g->dash[g->ndash++] = x < 0 ? 0 : x;
        p = q;
        while (*p == '%' || w_is_alpha(*p)) p++;
    }
    float sum = 0;
    for (int i = 0; i < g->ndash; i++) sum += g->dash[i];
    if (sum <= 0.0001f) g->ndash = 0;
    else if (g->ndash & 1) { int n = g->ndash; for (int i = 0; i < n && g->ndash < 16; i++) g->dash[g->ndash++] = g->dash[i]; }
}

static float get_num_prop(const struct node *n, const char *name, float def) {
    char b[64];
    if (!prop(n, name, b, sizeof(b))) return def;
    bool ok;
    const char *p = b;
    float v = parse_num(&p, &ok);
    if (!ok) return def;
    if (*p == '%') v /= 100.f;
    return v;
}

// Work out the drawing state for an element from its parent's. With a computed
// style (the element was in a page that was styled) the style says everything;
// without one (a hidden icon sheet, or something drawn through <use>) the
// attributes are read directly and the parent's values carry on.
static void apply_props(struct node *n, struct gs *g, bool use_style) {
    char b[300];
    const struct style *st = use_style ? n->style : NULL;
    if (st) {
        g->cc = st->color;
        if (st->font_size > 0) g->font_px = (float)st->font_size;
        if (st->svg_fill_set) {
            if (st->svg_fill_ref) { struct node *r = resolve_ref(n, st->svg_fill_ref); g->fill.ref = r; g->fill.none = !r; g->fill.color = 0xFF000000u; }
            else { g->fill.ref = NULL; g->fill.color = st->svg_fill == COLOR_CURRENT ? g->cc : st->svg_fill; g->fill.none = (g->fill.color >> 24) == 0; }
        } else { g->fill.ref = NULL; g->fill.none = false; g->fill.color = 0xFF000000u; }
        if (st->svg_stroke_set) {
            if (st->svg_stroke_ref) { struct node *r = resolve_ref(n, st->svg_stroke_ref); g->stroke.ref = r; g->stroke.none = !r; g->stroke.color = 0xFF000000u; }
            else { g->stroke.ref = NULL; g->stroke.color = st->svg_stroke == COLOR_CURRENT ? g->cc : st->svg_stroke; g->stroke.none = (g->stroke.color >> 24) == 0; }
        } else { g->stroke.ref = NULL; g->stroke.none = true; }
        g->sw = st->svg_stroke_width > 0 ? (float)st->svg_stroke_width / 1000.f : 1.f;
        g->fill_op = (float)st->fill_opacity / 255.f;
        g->stroke_op = (float)st->stroke_opacity / 255.f;
        g->cap = st->svg_linecap; g->join = st->svg_linejoin;
        if (st->visibility != 0) g->hidden = true; else g->hidden = false;
    } else {
        if (prop(n, "color", b, sizeof(b))) { struct paint pp; if (parse_paint(n, b, g->cc, &pp) && !pp.ref && !pp.none) g->cc = pp.color; }
        if (prop(n, "fill", b, sizeof(b))) parse_paint(n, b, g->cc, &g->fill);
        if (prop(n, "stroke", b, sizeof(b))) parse_paint(n, b, g->cc, &g->stroke);
        if (prop(n, "stroke-width", b, sizeof(b))) { float v; if (parse_len(b, g->font_px, 100.f, &v)) g->sw = v; }
        if (prop(n, "fill-opacity", b, sizeof(b))) g->fill_op = get_num_prop(n, "fill-opacity", 1.f);
        if (prop(n, "stroke-opacity", b, sizeof(b))) g->stroke_op = get_num_prop(n, "stroke-opacity", 1.f);
        if (prop(n, "stroke-linecap", b, sizeof(b))) g->cap = w_eq0(b, "round") ? 1 : (w_eq0(b, "square") ? 2 : 0);
        if (prop(n, "stroke-linejoin", b, sizeof(b))) g->join = w_eq0(b, "round") ? 1 : (w_eq0(b, "bevel") ? 2 : 0);
        if (prop(n, "visibility", b, sizeof(b))) g->hidden = w_eq0(b, "hidden") || w_eq0(b, "collapse");
    }
    // things the style sheet code doesn't carry
    if (prop(n, "stroke-miterlimit", b, sizeof(b))) g->miter = get_num_prop(n, "stroke-miterlimit", 4.f);
    if (prop(n, "fill-rule", b, sizeof(b))) g->evenodd = w_eq0(b, "evenodd");
    if (prop(n, "stroke-dasharray", b, sizeof(b))) parse_dashes(b, g);
    if (prop(n, "stroke-dashoffset", b, sizeof(b))) g->doff = get_num_prop(n, "stroke-dashoffset", 0.f);
    if (prop(n, "opacity", b, sizeof(b))) { float o = get_num_prop(n, "opacity", 1.f); if (o < 0) o = 0; if (o > 1) o = 1; g->op *= o; }
}

// ---------------------------------------------------------------- the shape being built (device coordinates)

struct sub { int start, count; bool closed; };

struct shape {
    float *x, *y;
    int n, cap;
    struct sub *subs;
    int ns, scap;
    float ux0, uy0, ux1, uy1;       // bounding box in the element's own coordinates
    bool has_u;
    struct mat m;
    float cx, cy, sx, sy;           // current point and start of the sub-path (user coordinates)
    bool open;                      // a sub-path is under way
    bool oom;
};

static void sh_init(struct shape *s, struct mat m) { memset(s, 0, sizeof(*s)); s->m = m; }
static void sh_free(struct shape *s) { free(s->x); free(s->y); free(s->subs); }

static void sh_u(struct shape *s, float x, float y) {
    if (!s->has_u) { s->ux0 = s->ux1 = x; s->uy0 = s->uy1 = y; s->has_u = true; return; }
    if (x < s->ux0) s->ux0 = x; if (x > s->ux1) s->ux1 = x;
    if (y < s->uy0) s->uy0 = y; if (y > s->uy1) s->uy1 = y;
}

static void sh_add(struct shape *s, float dx, float dy) {
    if (s->oom) return;
    if (s->n == s->cap) {
        int nc = s->cap ? s->cap * 2 : 128;
        if (nc > 400000) { s->oom = true; return; }
        float *nx = realloc(s->x, (size_t)nc * sizeof(float)), *ny = realloc(s->y, (size_t)nc * sizeof(float));
        if (!nx || !ny) { s->oom = true; if (nx) s->x = nx; if (ny) s->y = ny; return; }
        s->x = nx; s->y = ny; s->cap = nc;
    }
    s->x[s->n] = dx; s->y[s->n] = dy; s->n++;
    if (s->open) s->subs[s->ns - 1].count++;
}

static void sh_move(struct shape *s, float x, float y) {
    if (s->oom) return;
    if (s->ns == s->scap) {
        int nc = s->scap ? s->scap * 2 : 16;
        struct sub *ns = realloc(s->subs, (size_t)nc * sizeof(struct sub));
        if (!ns) { s->oom = true; return; }
        s->subs = ns; s->scap = nc;
    }
    s->subs[s->ns].start = s->n; s->subs[s->ns].count = 0; s->subs[s->ns].closed = false;
    s->ns++;
    s->open = true;
    float dx, dy;
    m_pt(&s->m, x, y, &dx, &dy);
    sh_u(s, x, y);
    sh_add(s, dx, dy);
    s->cx = s->sx = x; s->cy = s->sy = y;
}

static void sh_line(struct shape *s, float x, float y) {
    if (!s->open) sh_move(s, s->cx, s->cy);
    float dx, dy;
    m_pt(&s->m, x, y, &dx, &dy);
    sh_u(s, x, y);
    sh_add(s, dx, dy);
    s->cx = x; s->cy = y;
}

static void sh_cubic(struct shape *s, float x1, float y1, float x2, float y2, float x3, float y3) {
    if (!s->open) sh_move(s, s->cx, s->cy);
    float p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y;
    m_pt(&s->m, s->cx, s->cy, &p0x, &p0y);
    m_pt(&s->m, x1, y1, &p1x, &p1y);
    m_pt(&s->m, x2, y2, &p2x, &p2y);
    m_pt(&s->m, x3, y3, &p3x, &p3y);
    sh_u(s, x1, y1); sh_u(s, x2, y2); sh_u(s, x3, y3);
    float d = fabsf_(p0x - 2 * p1x + p2x) + fabsf_(p0y - 2 * p1y + p2y) + fabsf_(p1x - 2 * p2x + p3x) + fabsf_(p1y - 2 * p2y + p3y);
    int steps = (int)ceilf_(sqrtf_(d * 2.5f)) + 1;
    if (steps < 2) steps = 2;
    if (steps > 80) steps = 80;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps, u = 1.f - t;
        float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, e = t * t * t;
        sh_add(s, a * p0x + b * p1x + c * p2x + e * p3x, a * p0y + b * p1y + c * p2y + e * p3y);
    }
    s->cx = x3; s->cy = y3;
}

static void sh_close(struct shape *s) {
    if (!s->open) return;
    s->subs[s->ns - 1].closed = true;
    s->open = false;
    s->cx = s->sx; s->cy = s->sy;
}

// An arc from the current point (SVG's endpoint form), as cubic curves.
static void sh_arc(struct shape *s, float rx, float ry, float rot, bool large, bool sweep, float x, float y) {
    float x0 = s->cx, y0 = s->cy;
    if (x0 == x && y0 == y) return;
    rx = fabsf_(rx); ry = fabsf_(ry);
    if (rx < 1e-6f || ry < 1e-6f) { sh_line(s, x, y); return; }
    float phi = rot * 3.14159265f / 180.f, cp = cosf_(phi), sp = sinf_(phi);
    float dx = (x0 - x) / 2.f, dy = (y0 - y) / 2.f;
    float x1p = cp * dx + sp * dy, y1p = -sp * dx + cp * dy;
    float lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lam > 1.f) { float k = sqrtf_(lam); rx *= k; ry *= k; }
    float num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    float co = den > 0 ? sqrtf_(fmaxf_(0.f, num / den)) : 0.f;
    if (large == sweep) co = -co;
    float cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx;
    float cx = cp * cxp - sp * cyp + (x0 + x) / 2.f, cy = sp * cxp + cp * cyp + (y0 + y) / 2.f;
    float th1 = atan2f_((y1p - cyp) / ry, (x1p - cxp) / rx);
    float th2 = atan2f_((-y1p - cyp) / ry, (-x1p - cxp) / rx);
    float dth = th2 - th1;
    if (sweep && dth < 0) dth += 6.2831853f;
    else if (!sweep && dth > 0) dth -= 6.2831853f;
    int segs = (int)ceilf_(fabsf_(dth) / 1.5707963f - 0.001f);
    if (segs < 1) segs = 1;
    float step = dth / (float)segs;
    float t = 4.f / 3.f * tanf_(step / 4.f);
    float a = th1;
    for (int i = 0; i < segs; i++) {
        float ca = cosf_(a), sa = sinf_(a), cb = cosf_(a + step), sb = sinf_(a + step);
        float e1x = ca - t * sa, e1y = sa + t * ca, e2x = cb + t * sb, e2y = sb - t * cb;
        float p1x = cx + rx * e1x * cp - ry * e1y * sp, p1y = cy + rx * e1x * sp + ry * e1y * cp;
        float p2x = cx + rx * e2x * cp - ry * e2y * sp, p2y = cy + rx * e2x * sp + ry * e2y * cp;
        float px = cx + rx * cb * cp - ry * sb * sp, py = cy + rx * cb * sp + ry * sb * cp;
        if (i == segs - 1) { px = x; py = y; }
        sh_cubic(s, p1x, p1y, p2x, p2y, px, py);
        a += step;
    }
}

// d="M 10 10 L ..."
static void build_path(struct shape *s, const char *d) {
    const char *p = d;
    char cmd = 0;
    float lx2 = 0, ly2 = 0;           // the last control point (for smooth curves)
    char prev = 0;
    for (int guard = 0; guard < 200000 && !s->oom; guard++) {
        while (w_is_space(*p) || *p == ',') p++;
        if (!*p) break;
        if (w_is_alpha(*p)) { cmd = *p++; if (cmd == 'z' || cmd == 'Z') { sh_close(s); prev = 'z'; continue; } }
        else if (!cmd) break;
        bool rel = cmd >= 'a' && cmd <= 'z';
        char up = rel ? (char)(cmd - 32) : cmd;
        float v[7];
        int need = up == 'M' || up == 'L' || up == 'T' ? 2 : up == 'H' || up == 'V' ? 1 : up == 'C' ? 6 : up == 'S' || up == 'Q' ? 4 : up == 'A' ? 7 : 0;
        if (!need) break;
        bool ok = true;
        for (int i = 0; i < need && ok; i++) {
            if (up == 'A' && (i == 3 || i == 4)) {       // flags: one character each
                while (w_is_space(*p) || *p == ',') p++;
                if (*p == '0' || *p == '1') { v[i] = (float)(*p - '0'); p++; } else ok = false;
            } else v[i] = parse_num(&p, &ok);
        }
        if (!ok) break;
        float ox = rel ? s->cx : 0, oy = rel ? s->cy : 0;
        switch (up) {
        case 'M':
            sh_move(s, v[0] + ox, v[1] + oy);
            cmd = rel ? 'l' : 'L';
            prev = 'M';
            break;
        case 'L': sh_line(s, v[0] + ox, v[1] + oy); prev = 'L'; break;
        case 'H': sh_line(s, v[0] + (rel ? s->cx : 0), s->cy); prev = 'L'; break;
        case 'V': sh_line(s, s->cx, v[0] + (rel ? s->cy : 0)); prev = 'L'; break;
        case 'C':
            sh_cubic(s, v[0] + ox, v[1] + oy, v[2] + ox, v[3] + oy, v[4] + ox, v[5] + oy);
            lx2 = v[2] + ox; ly2 = v[3] + oy; prev = 'C';
            break;
        case 'S': {
            float c1x = s->cx, c1y = s->cy;
            if (prev == 'C') { c1x = 2 * s->cx - lx2; c1y = 2 * s->cy - ly2; }
            sh_cubic(s, c1x, c1y, v[0] + ox, v[1] + oy, v[2] + ox, v[3] + oy);
            lx2 = v[0] + ox; ly2 = v[1] + oy; prev = 'C';
            break; }
        case 'Q': {
            float qx = v[0] + ox, qy = v[1] + oy, ex = v[2] + ox, ey = v[3] + oy;
            float sx = s->cx, sy = s->cy;
            sh_cubic(s, sx + 2.f / 3.f * (qx - sx), sy + 2.f / 3.f * (qy - sy), ex + 2.f / 3.f * (qx - ex), ey + 2.f / 3.f * (qy - ey), ex, ey);
            lx2 = qx; ly2 = qy; prev = 'Q';
            break; }
        case 'T': {
            float qx = s->cx, qy = s->cy;
            if (prev == 'Q') { qx = 2 * s->cx - lx2; qy = 2 * s->cy - ly2; }
            float ex = v[0] + ox, ey = v[1] + oy, sx = s->cx, sy = s->cy;
            sh_cubic(s, sx + 2.f / 3.f * (qx - sx), sy + 2.f / 3.f * (qy - sy), ex + 2.f / 3.f * (qx - ex), ey + 2.f / 3.f * (qy - ey), ex, ey);
            lx2 = qx; ly2 = qy; prev = 'Q';
            break; }
        case 'A':
            sh_arc(s, v[0], v[1], v[2], v[3] != 0, v[4] != 0, v[5] + ox, v[6] + oy);
            prev = 'A';
            break;
        }
    }
}

static void rounded_rect(struct shape *s, float x, float y, float w, float h, float rx, float ry) {
    if (rx <= 0 || ry <= 0) {
        sh_move(s, x, y); sh_line(s, x + w, y); sh_line(s, x + w, y + h); sh_line(s, x, y + h); sh_close(s);
        return;
    }
    if (rx > w / 2) rx = w / 2;
    if (ry > h / 2) ry = h / 2;
    sh_move(s, x + rx, y);
    sh_line(s, x + w - rx, y);
    sh_arc(s, rx, ry, 0, false, true, x + w, y + ry);
    sh_line(s, x + w, y + h - ry);
    sh_arc(s, rx, ry, 0, false, true, x + w - rx, y + h);
    sh_line(s, x + rx, y + h);
    sh_arc(s, rx, ry, 0, false, true, x, y + h - ry);
    sh_line(s, x, y + ry);
    sh_arc(s, rx, ry, 0, false, true, x + rx, y);
    sh_close(s);
}

static void ellipse_path(struct shape *s, float cx, float cy, float rx, float ry) {
    sh_move(s, cx + rx, cy);
    sh_arc(s, rx, ry, 0, false, true, cx, cy + ry);
    sh_arc(s, rx, ry, 0, false, true, cx - rx, cy);
    sh_arc(s, rx, ry, 0, false, true, cx, cy - ry);
    sh_arc(s, rx, ry, 0, false, true, cx + rx, cy);
    sh_close(s);
}

static void points_path(struct shape *s, const char *pts, bool close) {
    const char *p = pts;
    bool first = true;
    for (;;) {
        bool ok;
        float x = parse_num(&p, &ok);
        if (!ok) break;
        float y = parse_num(&p, &ok);
        if (!ok) break;
        if (first) { sh_move(s, x, y); first = false; } else sh_line(s, x, y);
    }
    if (close) sh_close(s);
}

// ---------------------------------------------------------------- the rasterizer
//
// Each edge adds to a grid how much it covers; adding along each row then gives
// the coverage of every pixel (so edges come out smooth).

struct cov { float *a; int x0, y0, w, h, stride; };

static bool cov_new(struct cov *c, float minx, float miny, float maxx, float maxy, int cw, int ch) {
    memset(c, 0, sizeof(*c));
    if (!(minx == minx) || !(miny == miny) || !(maxx == maxx) || !(maxy == maxy)) return false;
    int x0 = (int)floorf_(fmaxf_(minx, -1.f)), y0 = (int)floorf_(fmaxf_(miny, -1.f));
    int x1 = (int)ceilf_(fminf_(maxx, (float)cw + 1.f)), y1 = (int)ceilf_(fminf_(maxy, (float)ch + 1.f));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > cw) x1 = cw;
    if (y1 > ch) y1 = ch;
    if (x1 <= x0 || y1 <= y0) return false;
    long long cells = (long long)(x1 - x0 + 2) * (y1 - y0);
    if (cells > 20000000LL) return false;
    c->a = calloc((size_t)cells, sizeof(float));
    if (!c->a) return false;
    c->x0 = x0; c->y0 = y0; c->w = x1 - x0; c->h = y1 - y0; c->stride = c->w + 2;
    return true;
}

static float clampf(float v, float lo, float hi) { if (!(v >= lo)) return lo; return v > hi ? hi : v; }

static void cov_line(struct cov *c, float x0, float y0, float x1, float y1) {
    x0 = clampf(x0 - (float)c->x0, -1e7f, 1e7f); x1 = clampf(x1 - (float)c->x0, -1e7f, 1e7f);
    y0 = clampf(y0 - (float)c->y0, -1e7f, 1e7f); y1 = clampf(y1 - (float)c->y0, -1e7f, 1e7f);
    if (y0 == y1) return;
    float dir = 1.f;
    if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1.f; }
    if (y1 <= 0.f || y0 >= (float)c->h) return;
    float dxdy = (x1 - x0) / (y1 - y0);
    float x = x0;
    if (y0 < 0.f) { x -= y0 * dxdy; y0 = 0.f; }
    int ys = (int)y0;
    int ye = (int)ceilf_(y1);
    if (ye > c->h) ye = c->h;
    float fw = (float)c->w;
    for (int y = ys; y < ye; y++) {
        float dy = fminf_((float)(y + 1), y1) - fmaxf_((float)y, y0);
        float xnext = x + dxdy * dy;
        float d = dy * dir;
        float xa = clampf(x, 0.f, fw), xb = clampf(xnext, 0.f, fw);
        float xl = xa < xb ? xa : xb, xr = xa < xb ? xb : xa;
        float *row = c->a + (size_t)y * (size_t)c->stride;
        float x0f = floorf_(xl);
        int x0i = (int)x0f;
        float x1c = ceilf_(xr);
        int x1i = (int)x1c;
        if (x1i <= x0i + 1) {
            float xmf = 0.5f * (xa + xb) - x0f;
            row[x0i] += d - d * xmf;
            row[x0i + 1] += d * xmf;
        } else {
            float s = 1.f / (xr - xl);
            float x0fr = xl - x0f;
            float a0 = 0.5f * s * (1.f - x0fr) * (1.f - x0fr);
            float x1fr = xr - x1c + 1.f;
            float am = 0.5f * s * x1fr * x1fr;
            row[x0i] += d * a0;
            if (x1i == x0i + 2) {
                row[x0i + 1] += d * (1.f - a0 - am);
            } else {
                float a1 = s * (1.5f - x0fr);
                row[x0i + 1] += d * (a1 - a0);
                for (int xi = x0i + 2; xi < x1i - 1; xi++) row[xi] += d * s;
                float a2 = a1 + (float)(x1i - x0i - 3) * s;
                row[x1i - 1] += d * (1.f - a2 - am);
            }
            row[x1i] += d * am;
        }
        x = xnext;
    }
}

// A closed polygon, wound the right way round whichever way it was given.
static void cov_poly(struct cov *c, const float *x, const float *y, int n) {
    if (n < 3) return;
    float area = 0;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += x[i] * y[j] - x[j] * y[i]; }
    if (area >= 0) for (int i = 0; i < n; i++) { int j = (i + 1) % n; cov_line(c, x[i], y[i], x[j], y[j]); }
    else for (int i = n - 1; i >= 0; i--) { int j = (i + n - 1) % n; cov_line(c, x[i], y[i], x[j], y[j]); }
}

// Turn the sums into coverage (0..1), in place.
static void cov_resolve(struct cov *c, bool evenodd) {
    for (int y = 0; y < c->h; y++) {
        float *row = c->a + (size_t)y * (size_t)c->stride;
        float acc = 0;
        for (int x = 0; x < c->w; x++) {
            acc += row[x];
            float v = acc < 0 ? -acc : acc;
            if (evenodd) {
                v = v - 2.f * floorf_(v * 0.5f);
                if (v > 1.f) v = 2.f - v;
            } else if (v > 1.f) v = 1.f;
            row[x] = v;
        }
    }
}

// ---------------------------------------------------------------- gradients

struct grad {
    bool radial;
    int spread;                     // 0 pad, 1 reflect, 2 repeat
    struct mat inv;                 // device -> gradient space
    float x1, y1, x2, y2;
    float cx, cy, r, fx, fy;
    int n;
    float off[24];
    uint32_t col[24];
};

// An attribute of a gradient, following href to the gradients it borrows from.
static const char *gattr(struct node *g, const char *name) {
    for (int depth = 0; g && depth < 8; depth++) {
        const char *v = aget(g, name);
        if (v) return v;
        const char *h = aget(g, "href");
        if (!h) h = aget(g, "xlink:href");
        if (!h) break;
        g = resolve_ref(g, h);
    }
    return NULL;
}

static float gnum(struct node *g, const char *name, float def, bool bbox, float userpct) {
    const char *v = gattr(g, name);
    if (!v) return def;
    float r;
    if (!parse_len(v, 16.f, bbox ? 1.f : userpct, &r)) return def;
    return r;
}

static bool parse_stop_color(struct node *st, uint32_t *out) {
    char b[100];
    uint32_t col = 0xFF000000u;
    if (prop(st, "stop-color", b, sizeof(b))) {
        struct paint p;
        if (parse_paint(st, b, 0xFF000000u, &p) && !p.ref) col = p.none ? 0 : p.color;
    }
    float o = get_num_prop(st, "stop-opacity", 1.f);
    if (o < 0) o = 0; if (o > 1) o = 1;
    unsigned a = (unsigned)((float)(col >> 24) * o + 0.5f);
    *out = (a << 24) | (col & 0xFFFFFFu);
    return true;
}

static bool grad_build(struct ctx *C, struct shape *s, struct node *g, struct grad *o) {
    memset(o, 0, sizeof(*o));
    bool radial = w_eq0(g->name, "radialGradient");
    if (!radial && !w_eq0(g->name, "linearGradient")) return false;
    o->radial = radial;
    // stops: the first gradient in the chain that has any
    struct node *sg = g;
    for (int depth = 0; sg && depth < 8; depth++) {
        int cnt = 0;
        for (struct node *c = sg->first; c; c = c->next) if (c->type == N_ELEMENT && w_eq0(c->name, "stop")) cnt++;
        if (cnt) break;
        const char *h = aget(sg, "href");
        if (!h) h = aget(sg, "xlink:href");
        sg = h ? resolve_ref(sg, h) : NULL;
    }
    if (!sg) return false;
    float last = 0;
    for (struct node *c = sg->first; c && o->n < 24; c = c->next) {
        if (c->type != N_ELEMENT || !w_eq0(c->name, "stop")) continue;
        float off = 0;
        const char *ov = aget(c, "offset");
        if (ov) { float v; if (parse_len(ov, 16.f, 1.f, &v)) off = v; if (strchr(ov, '%')) off = off; }
        if (ov && strchr(ov, '%')) { bool ok; const char *q = ov; float pv = parse_num(&q, &ok); off = ok ? pv / 100.f : 0; }
        off = clampf(off, 0.f, 1.f);
        if (off < last) off = last;
        last = off;
        uint32_t col;
        parse_stop_color(c, &col);
        o->off[o->n] = off; o->col[o->n] = col; o->n++;
    }
    if (o->n == 0) return false;
    const char *gu = gattr(g, "gradientUnits");
    bool bbox = !(gu && w_eq0(gu, "userSpaceOnUse"));
    float bw = s->ux1 - s->ux0, bh = s->uy1 - s->uy0;
    if (bbox && (bw <= 1e-6f || bh <= 1e-6f)) return false;
    if (radial) {
        o->cx = gnum(g, "cx", bbox ? 0.5f : C->vw * 0.5f, bbox, C->vw);
        o->cy = gnum(g, "cy", bbox ? 0.5f : C->vh * 0.5f, bbox, C->vh);
        o->r = gnum(g, "r", bbox ? 0.5f : C->vw * 0.5f, bbox, C->vw);
        o->fx = gnum(g, "fx", o->cx, bbox, C->vw);
        o->fy = gnum(g, "fy", o->cy, bbox, C->vh);
        if (o->r <= 1e-9f) return false;
        float dx = o->fx - o->cx, dy = o->fy - o->cy, dd = sqrtf_(dx * dx + dy * dy);
        if (dd > o->r * 0.999f) { float k = o->r * 0.999f / dd; o->fx = o->cx + dx * k; o->fy = o->cy + dy * k; }
    } else {
        o->x1 = gnum(g, "x1", 0.f, bbox, C->vw);
        o->y1 = gnum(g, "y1", 0.f, bbox, C->vh);
        o->x2 = gnum(g, "x2", bbox ? 1.f : C->vw, bbox, C->vw);
        o->y2 = gnum(g, "y2", 0.f, bbox, C->vh);
    }
    const char *sp = gattr(g, "spreadMethod");
    o->spread = sp ? (w_eq0(sp, "reflect") ? 1 : (w_eq0(sp, "repeat") ? 2 : 0)) : 0;
    struct mat m = s->m;
    if (bbox) { struct mat bm = { bw, 0, 0, bh, s->ux0, s->uy0 }; m = m_mul(m, bm); }
    const char *gt = gattr(g, "gradientTransform");
    if (gt) m = m_mul(m, parse_transform(gt));
    return m_inv(&m, &o->inv);
}

static uint32_t grad_color(const struct grad *g, float px, float py) {
    float x, y;
    m_pt(&g->inv, px, py, &x, &y);
    float t;
    if (!g->radial) {
        float dx = g->x2 - g->x1, dy = g->y2 - g->y1, dd = dx * dx + dy * dy;
        t = dd > 1e-12f ? ((x - g->x1) * dx + (y - g->y1) * dy) / dd : 0.f;
    } else {
        float ex = g->cx - g->fx, ey = g->cy - g->fy;
        float dx = x - g->fx, dy = y - g->fy;
        float a = ex * ex + ey * ey - g->r * g->r;
        float b = -2.f * (dx * ex + dy * ey);
        float c = dx * dx + dy * dy;
        if (fabsf_(a) < 1e-9f) t = fabsf_(b) > 1e-9f ? -c / b : 0.f;
        else {
            float disc = b * b - 4.f * a * c;
            if (disc < 0) disc = 0;
            float sq = sqrtf_(disc);
            float t1 = (-b + sq) / (2.f * a), t2 = (-b - sq) / (2.f * a);
            t = t1 > t2 ? t1 : t2;
        }
        if (t != t) t = 0;
    }
    if (g->spread == 2) t = t - floorf_(t);
    else if (g->spread == 1) { t = t - 2.f * floorf_(t * 0.5f); if (t > 1.f) t = 2.f - t; }
    t = clampf(t, 0.f, 1.f);
    if (t <= g->off[0]) return g->col[0];
    int n = g->n;
    if (t >= g->off[n - 1]) return g->col[n - 1];
    for (int i = 1; i < n; i++) {
        if (t <= g->off[i]) {
            float span = g->off[i] - g->off[i - 1];
            float f = span > 1e-9f ? (t - g->off[i - 1]) / span : 1.f;
            uint32_t p = g->col[i - 1], q = g->col[i];
            unsigned r = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                float cp = (float)((p >> sh) & 255), cq = (float)((q >> sh) & 255);
                r |= (unsigned)(cp + (cq - cp) * f + 0.5f) << sh;
            }
            return r;
        }
    }
    return g->col[n - 1];
}

// ---------------------------------------------------------------- putting coverage on the picture

static void blend(uint32_t *dst, uint32_t src, float cov) {
    float sa = (float)(src >> 24) * (1.f / 255.f) * cov;
    if (sa <= 0.f) return;
    uint32_t d = *dst;
    float da = (float)(d >> 24) * (1.f / 255.f);
    float oa = sa + da * (1.f - sa);
    if (oa <= 0.f) return;
    float k = da * (1.f - sa);
    unsigned r = (unsigned)(((float)((src >> 16) & 255) * sa + (float)((d >> 16) & 255) * k) / oa + 0.5f);
    unsigned g = (unsigned)(((float)((src >> 8) & 255) * sa + (float)((d >> 8) & 255) * k) / oa + 0.5f);
    unsigned b = (unsigned)(((float)(src & 255) * sa + (float)(d & 255) * k) / oa + 0.5f);
    unsigned a = (unsigned)(oa * 255.f + 0.5f);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255; if (a > 255) a = 255;
    *dst = (a << 24) | (r << 16) | (g << 8) | b;
}

static void cov_paint(struct ctx *C, struct cov *c, uint32_t color, const struct grad *gr, float opacity) {
    for (int y = 0; y < c->h; y++) {
        const float *row = c->a + (size_t)y * (size_t)c->stride;
        uint32_t *dst = C->out + (size_t)(c->y0 + y) * (size_t)C->w + (size_t)c->x0;
        for (int x = 0; x < c->w; x++) {
            float v = row[x];
            if (v <= 0.002f) continue;
            uint32_t col = gr ? grad_color(gr, (float)(c->x0 + x) + 0.5f, (float)(c->y0 + y) + 0.5f) : color;
            blend(dst + x, col, v * opacity);
        }
    }
}

// ---------------------------------------------------------------- strokes

static void circle_poly(struct cov *c, float cx, float cy, float r) {
    int n = (int)(r * 1.2f) + 8;
    if (n > 40) n = 40;
    float xs[40], ys[40];
    for (int i = 0; i < n; i++) {
        float a = 6.2831853f * (float)i / (float)n;
        xs[i] = cx + r * cosf_(a); ys[i] = cy + r * sinf_(a);
    }
    cov_poly(c, xs, ys, n);
}

// One open or closed polyline in device coordinates, given a width.
static void stroke_line(struct cov *c, const float *px, const float *py, int n, bool closed, float hw, int cap, int join, float miter) {
    // drop repeated points
    float *x = malloc(sizeof(float) * (size_t)(n + 1) * 2), *y;
    if (!x) return;
    y = x + (n + 1);
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (m > 0 && fabsf_(px[i] - x[m - 1]) < 1e-4f && fabsf_(py[i] - y[m - 1]) < 1e-4f) continue;
        x[m] = px[i]; y[m] = py[i]; m++;
    }
    if (closed && m > 1 && fabsf_(x[0] - x[m - 1]) < 1e-4f && fabsf_(y[0] - y[m - 1]) < 1e-4f) m--;
    if (m == 1) {
        if (cap == 1) circle_poly(c, x[0], y[0], hw);
        else if (cap == 2) { float qx[4] = { x[0] - hw, x[0] + hw, x[0] + hw, x[0] - hw }, qy[4] = { y[0] - hw, y[0] - hw, y[0] + hw, y[0] + hw }; cov_poly(c, qx, qy, 4); }
        free(x);
        return;
    }
    if (m < 2) { free(x); return; }
    int segs = closed ? m : m - 1;
    for (int i = 0; i < segs; i++) {
        int j = (i + 1) % m;
        float dx = x[j] - x[i], dy = y[j] - y[i];
        float len = sqrtf_(dx * dx + dy * dy);
        if (len < 1e-6f) continue;
        float ux = dx / len, uy = dy / len;
        float nx = -uy * hw, ny = ux * hw;
        float sx0 = x[i], sy0 = y[i], sx1 = x[j], sy1 = y[j];
        if (!closed && cap == 2) {
            if (i == 0) { sx0 -= ux * hw; sy0 -= uy * hw; }
            if (i == segs - 1) { sx1 += ux * hw; sy1 += uy * hw; }
        }
        float qx[4] = { sx0 + nx, sx1 + nx, sx1 - nx, sx0 - nx };
        float qy[4] = { sy0 + ny, sy1 + ny, sy1 - ny, sy0 - ny };
        cov_poly(c, qx, qy, 4);
    }
    // joins
    int j0 = closed ? 0 : 1, j1 = closed ? m : m - 1;
    for (int v = j0; v < j1; v++) {
        int pi = (v + m - 1) % m, ni = (v + 1) % m;
        float d0x = x[v] - x[pi], d0y = y[v] - y[pi], d1x = x[ni] - x[v], d1y = y[ni] - y[v];
        float l0 = sqrtf_(d0x * d0x + d0y * d0y), l1 = sqrtf_(d1x * d1x + d1y * d1y);
        if (l0 < 1e-6f || l1 < 1e-6f) continue;
        d0x /= l0; d0y /= l0; d1x /= l1; d1y /= l1;
        float cross = d0x * d1y - d0y * d1x, dot = d0x * d1x + d0y * d1y;
        if (fabsf_(cross) < 1e-4f && dot > 0) continue;       // straight on
        if (join == 1) { circle_poly(c, x[v], y[v], hw); continue; }
        float sg = cross > 0 ? -1.f : 1.f;
        float n0x = -d0y * hw * sg, n0y = d0x * hw * sg, n1x = -d1y * hw * sg, n1y = d1x * hw * sg;
        float ax = x[v] + n0x, ay = y[v] + n0y, bx = x[v] + n1x, by = y[v] + n1y;
        // normals dot = dot of directions
        float k = 1.f + dot;
        bool use_miter = false;
        float mx = 0, my = 0;
        if (join == 0 && k > 1e-4f) {
            float ratio = sqrtf_(2.f / k);
            if (ratio <= miter) { use_miter = true; mx = x[v] + (n0x + n1x) / k; my = y[v] + (n0y + n1y) / k; }
        }
        if (use_miter) { float qx[4] = { x[v], ax, mx, bx }, qy[4] = { y[v], ay, my, by }; cov_poly(c, qx, qy, 4); }
        else { float qx[3] = { x[v], ax, bx }, qy[3] = { y[v], ay, by }; cov_poly(c, qx, qy, 3); }
    }
    if (!closed && cap == 1) { circle_poly(c, x[0], y[0], hw); circle_poly(c, x[m - 1], y[m - 1], hw); }
    free(x);
}

// Cut a polyline into dashes and stroke each piece.
static void stroke_dashed(struct cov *c, const float *px, const float *py, int n, bool closed, float hw, int cap, int join, float miter,
                          const float *dash, int nd, float off, float scale) {
    int total = closed ? n + 1 : n;
    float *x = malloc(sizeof(float) * (size_t)(total + 1) * 2 + sizeof(float) * (size_t)(total + 2));
    if (!x) return;
    float *y = x + (total + 1);
    float *piece_x = y + (total + 1);   // reused as scratch below (size total+2 for both via two halves)
    (void)piece_x;
    int m = 0;
    for (int i = 0; i < n; i++) { x[m] = px[i]; y[m] = py[i]; m++; }
    if (closed) { x[m] = px[0]; y[m] = py[0]; m++; }
    float *qx = malloc(sizeof(float) * (size_t)(m + 4) * 2);
    if (!qx) { free(x); return; }
    float *qy = qx + (m + 4);
    float period = 0;
    for (int i = 0; i < nd; i++) period += dash[i] * scale;
    if (period < 0.5f) { free(x); free(qx); return; }
    float o = off * scale;
    o = o - period * floorf_(o / period);
    int di = 0;
    while (o >= dash[di] * scale && o > 0) { o -= dash[di] * scale; di = (di + 1) % nd; if (di == 0 && o < 0.0001f) break; }
    float remain = dash[di] * scale - o;
    bool on = (di & 1) == 0;
    int pieces = 0, qn = 0;
    if (on) { qx[0] = x[0]; qy[0] = y[0]; qn = 1; }
    for (int i = 0; i + 1 < m && pieces < 20000; i++) {
        float dx = x[i + 1] - x[i], dy = y[i + 1] - y[i];
        float len = sqrtf_(dx * dx + dy * dy), pos = 0;
        while (len - pos > remain && pieces < 20000) {
            pos += remain;
            float t = len > 0 ? pos / len : 0;
            float ex = x[i] + dx * t, ey = y[i] + dy * t;
            if (on) {
                qx[qn] = ex; qy[qn] = ey; qn++;
                stroke_line(c, qx, qy, qn, false, hw, cap, join, miter);
                pieces++;
                qn = 0;
            } else { qx[0] = ex; qy[0] = ey; qn = 1; }
            on = !on;
            di = (di + 1) % nd;
            remain = dash[di] * scale;
            if (remain < 0.01f) remain = 0.01f;
        }
        remain -= len - pos;
        if (on && qn < m + 2) { qx[qn] = x[i + 1]; qy[qn] = y[i + 1]; qn++; }
    }
    if (on && qn > 1) stroke_line(c, qx, qy, qn, false, hw, cap, join, miter);
    free(x); free(qx);
}

// ---------------------------------------------------------------- drawing a shape

static void draw_shape(struct ctx *C, struct node *n, struct shape *s, const struct gs *g) {
    if (s->oom || s->n < 2 || g->hidden || C->budget <= 0) return;
    C->budget--;
    float minx = s->x[0], maxx = s->x[0], miny = s->y[0], maxy = s->y[0];
    for (int i = 1; i < s->n; i++) {
        if (s->x[i] < minx) minx = s->x[i]; if (s->x[i] > maxx) maxx = s->x[i];
        if (s->y[i] < miny) miny = s->y[i]; if (s->y[i] > maxy) maxy = s->y[i];
    }
    (void)n;
    // fill
    if (!g->fill.none && g->fill_op * g->op > 0.001f) {
        struct grad gr;
        const struct grad *gp = NULL;
        bool ok = true;
        if (g->fill.ref) {
            if (grad_build(C, s, (struct node *)g->fill.ref, &gr)) gp = &gr;
            else ok = false;      // a pattern or a broken reference: nothing
        }
        struct cov cv;
        if (ok && cov_new(&cv, minx, miny, maxx, maxy, C->w, C->h)) {
            for (int k = 0; k < s->ns; k++) {
                const struct sub *sb = &s->subs[k];
                if (sb->count < 2) continue;
                const float *xs = s->x + sb->start, *ys = s->y + sb->start;
                for (int i = 0; i < sb->count; i++) { int j = (i + 1) % sb->count; cov_line(&cv, xs[i], ys[i], xs[j], ys[j]); }
            }
            cov_resolve(&cv, g->evenodd);
            cov_paint(C, &cv, g->fill.color, gp, g->fill_op * g->op);
            free(cv.a);
        }
    }
    // stroke
    if (!g->stroke.none && g->stroke_op * g->op > 0.001f && g->sw > 0) {
        float scale = sqrtf_(fabsf_(s->m.a * s->m.d - s->m.b * s->m.c));
        float hw = g->sw * scale * 0.5f;
        if (hw < 0.05f) hw = 0.05f;
        struct grad gr;
        const struct grad *gp = NULL;
        bool ok = true;
        if (g->stroke.ref) {
            if (grad_build(C, s, (struct node *)g->stroke.ref, &gr)) gp = &gr;
            else ok = false;
        }
        float ext = hw * (g->join == 0 ? (g->miter > 1.5f ? g->miter : 1.5f) : 1.5f) + 2.f;
        struct cov cv;
        if (ok && cov_new(&cv, minx - ext, miny - ext, maxx + ext, maxy + ext, C->w, C->h)) {
            for (int k = 0; k < s->ns; k++) {
                const struct sub *sb = &s->subs[k];
                if (sb->count < 1) continue;
                const float *xs = s->x + sb->start, *ys = s->y + sb->start;
                if (g->ndash > 0) stroke_dashed(&cv, xs, ys, sb->count, sb->closed, hw, g->cap, g->join, g->miter, g->dash, g->ndash, g->doff, scale);
                else stroke_line(&cv, xs, ys, sb->count, sb->closed, hw, g->cap, g->join, g->miter);
            }
            cov_resolve(&cv, false);
            cov_paint(C, &cv, g->stroke.color, gp, g->stroke_op * g->op);
            free(cv.a);
        }
    }
}

// ---------------------------------------------------------------- walking the tree

static float lenattr(struct ctx *C, const struct node *n, const char *name, float def, bool horiz) {
    const char *v = aget(n, name);
    float r;
    if (v && parse_len(v, 16.f, horiz ? C->vw : C->vh, &r)) return r;
    return def;
}

// The matrix that fits a viewBox into a w x h space, as preserveAspectRatio says.
static struct mat fit_viewbox(const char *par, float vx, float vy, float vw, float vh, float w, float h) {
    struct mat m = m_id();
    if (vw <= 0 || vh <= 0 || w <= 0 || h <= 0) return m;
    float sx = w / vw, sy = h / vh;
    float ax = 0.5f, ay = 0.5f;
    bool none = false, slice = false;
    if (par) {
        const char *p = par;
        while (w_is_space(*p)) p++;
        if (w_starts(p, "defer")) { while (*p && !w_is_space(*p)) p++; while (w_is_space(*p)) p++; }
        if (w_starts(p, "none")) none = true;
        else {
            if (w_starts(p, "xmin")) ax = 0; else if (w_starts(p, "xmax")) ax = 1;
            if (p[0] && p[1] && p[2] && p[3]) {
                if (w_starts(p + 4, "ymin")) ay = 0; else if (w_starts(p + 4, "ymax")) ay = 1;
            }
            if (strstr_(p, "slice")) slice = true;
        }
    }
    if (!none) { float s = slice ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy); sx = sy = s; }
    m.a = sx; m.d = sy;
    m.e = ax * (w - vw * sx) - vx * sx;
    m.f = ay * (h - vh * sy) - vy * sy;
    return m;
}

static void draw_node(struct ctx *C, struct node *n, struct gs *pg, struct mat m, bool via_use, int depth);

static void draw_children(struct ctx *C, struct node *n, struct gs *g, struct mat m, bool via_use, int depth) {
    for (struct node *c = n->first; c; c = c->next) if (c->type == N_ELEMENT) draw_node(C, c, g, m, via_use, depth + 1);
}

static void draw_node(struct ctx *C, struct node *n, struct gs *pg, struct mat m, bool via_use, int depth) {
    if (depth > 60 || C->budget <= 0 || !n->name) return;
    const char *nm = n->name;
    if (w_eq0(nm, "defs") || w_eq0(nm, "symbol") || w_eq0(nm, "clipPath") || w_eq0(nm, "mask") || w_eq0(nm, "marker")
        || w_eq0(nm, "pattern") || w_eq0(nm, "linearGradient") || w_eq0(nm, "radialGradient") || w_eq0(nm, "filter")
        || w_eq0(nm, "style") || w_eq0(nm, "title") || w_eq0(nm, "desc") || w_eq0(nm, "metadata") || w_eq0(nm, "script")
        || w_eq0(nm, "text") || w_eq0(nm, "image") || w_eq0(nm, "foreignObject")) {
        if (!(depth == 0)) return;
    }
    const char *dsp = aget(n, "display");
    if (dsp && w_eq0(dsp, "none")) return;
    char b[200];
    if (prop(n, "display", b, sizeof(b)) && w_eq0(b, "none")) return;
    bool use_style = !via_use && n->style != NULL;
    if (use_style && n->style->display == D_NONE && depth > 0 && !w_eq0(nm, "svg")) return;
    struct gs g = *pg;
    apply_props(n, &g, use_style);
    struct mat nm_m = m;
    const char *tf = aget(n, "transform");
    if (tf) nm_m = m_mul(m, parse_transform(tf));

    if (w_eq0(nm, "g") || w_eq0(nm, "a") || w_eq0(nm, "svg") || w_eq0(nm, "switch")) {
        if (w_eq0(nm, "svg") && depth > 0) {
            // a nested svg: place it, and fit its viewBox
            float x = lenattr(C, n, "x", 0, true), y = lenattr(C, n, "y", 0, false);
            float w = lenattr(C, n, "width", C->vw, true), h = lenattr(C, n, "height", C->vh, false);
            struct mat t = m_id(); t.e = x; t.f = y;
            nm_m = m_mul(nm_m, t);
            const char *vb = aget(n, "viewBox");
            if (vb) {
                const char *q = vb; bool ok;
                float v0 = parse_num(&q, &ok), v1 = ok ? parse_num(&q, &ok) : 0, v2 = ok ? parse_num(&q, &ok) : 0, v3 = ok ? parse_num(&q, &ok) : 0;
                if (ok && v2 > 0 && v3 > 0) nm_m = m_mul(nm_m, fit_viewbox(aget(n, "preserveAspectRatio"), v0, v1, v2, v3, w, h));
            }
        }
        if (w_eq0(nm, "switch")) {
            for (struct node *c = n->first; c; c = c->next) if (c->type == N_ELEMENT) { draw_node(C, c, &g, nm_m, via_use, depth + 1); break; }
            return;
        }
        draw_children(C, n, &g, nm_m, via_use, depth);
        return;
    }
    if (w_eq0(nm, "use")) {
        if (C->use_depth > 8) return;
        const char *h = aget(n, "href");
        if (!h) h = aget(n, "xlink:href");
        struct node *r = h ? resolve_ref(n, h) : NULL;
        if (!r) return;
        // the element must not be one of use's own ancestors
        for (struct node *a = n; a; a = a->parent) if (a == r) return;
        struct mat t = m_id();
        t.e = lenattr(C, n, "x", 0, true); t.f = lenattr(C, n, "y", 0, false);
        struct mat um = m_mul(nm_m, t);
        C->use_depth++;
        if (w_eq0(r->name, "symbol") || w_eq0(r->name, "svg")) {
            float w = lenattr(C, n, "width", lenattr(C, r, "width", C->vw, true), true);
            float hh = lenattr(C, n, "height", lenattr(C, r, "height", C->vh, false), false);
            const char *vb = aget(r, "viewBox");
            if (vb) {
                const char *q = vb; bool ok;
                float v0 = parse_num(&q, &ok), v1 = ok ? parse_num(&q, &ok) : 0, v2 = ok ? parse_num(&q, &ok) : 0, v3 = ok ? parse_num(&q, &ok) : 0;
                if (ok && v2 > 0 && v3 > 0) um = m_mul(um, fit_viewbox(aget(r, "preserveAspectRatio"), v0, v1, v2, v3, w, hh));
            }
            struct gs rg = g;
            apply_props(r, &rg, false);
            draw_children(C, r, &rg, um, true, depth);
        } else {
            draw_node(C, r, &g, um, true, depth + 1);
        }
        C->use_depth--;
        return;
    }

    struct shape s;
    sh_init(&s, nm_m);
    if (w_eq0(nm, "path")) { const char *d = aget(n, "d"); if (d) build_path(&s, d); }
    else if (w_eq0(nm, "rect")) {
        float x = lenattr(C, n, "x", 0, true), y = lenattr(C, n, "y", 0, false);
        float w = lenattr(C, n, "width", 0, true), h = lenattr(C, n, "height", 0, false);
        float rx = lenattr(C, n, "rx", -1, true), ry = lenattr(C, n, "ry", -1, false);
        if (rx < 0 && ry < 0) rx = ry = 0; else if (rx < 0) rx = ry; else if (ry < 0) ry = rx;
        if (w > 0 && h > 0) rounded_rect(&s, x, y, w, h, rx, ry);
    }
    else if (w_eq0(nm, "circle")) {
        float r = lenattr(C, n, "r", 0, true);
        if (r > 0) ellipse_path(&s, lenattr(C, n, "cx", 0, true), lenattr(C, n, "cy", 0, false), r, r);
    }
    else if (w_eq0(nm, "ellipse")) {
        float rx = lenattr(C, n, "rx", 0, true), ry = lenattr(C, n, "ry", 0, false);
        if (rx > 0 && ry > 0) ellipse_path(&s, lenattr(C, n, "cx", 0, true), lenattr(C, n, "cy", 0, false), rx, ry);
    }
    else if (w_eq0(nm, "line")) {
        sh_move(&s, lenattr(C, n, "x1", 0, true), lenattr(C, n, "y1", 0, false));
        sh_line(&s, lenattr(C, n, "x2", 0, true), lenattr(C, n, "y2", 0, false));
        g.fill.none = true;
    }
    else if (w_eq0(nm, "polyline") || w_eq0(nm, "polygon")) {
        const char *pts = aget(n, "points");
        if (pts) points_path(&s, pts, w_eq0(nm, "polygon"));
    }
    if (s.n > 0) {
        // paint servers are looked up in user space of the element; the bounding box is the shape's
        draw_shape(C, n, &s, &g);
    }
    sh_free(&s);
}

// ---------------------------------------------------------------- pictures

static struct node *find_svg(struct node *n, int depth) {
    for (struct node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEMENT) continue;
        if (c->name && w_eq0(c->name, "svg")) return c;
        if (depth < 20) { struct node *r = find_svg(c, depth + 1); if (r) return r; }
    }
    return NULL;
}

static bool viewbox_of(const struct node *n, float *x, float *y, float *w, float *h) {
    const char *vb = aget(n, "viewBox");
    if (!vb) return false;
    const char *q = vb; bool ok;
    float a = parse_num(&q, &ok); if (!ok) return false;
    float b = parse_num(&q, &ok); if (!ok) return false;
    float c = parse_num(&q, &ok); if (!ok) return false;
    float d = parse_num(&q, &ok); if (!ok) return false;
    if (c <= 0 || d <= 0) return false;
    *x = a; *y = b; *w = c; *h = d;
    return true;
}

// The size a picture wants to be shown at, from its width, height and viewBox.
bool svg_node_size(struct node *n, int *w, int *h) {
    float vx, vy, vw, vh;
    bool has_vb = viewbox_of(n, &vx, &vy, &vw, &vh);
    float fw = 0, fh = 0;
    const char *a = aget(n, "width"), *b = aget(n, "height");
    if (a && !strchr(a, '%')) { float v; if (parse_len(a, 16.f, 0.f, &v) && v > 0) fw = v; }
    if (b && !strchr(b, '%')) { float v; if (parse_len(b, 16.f, 0.f, &v) && v > 0) fh = v; }
    if (has_vb) {
        if (fw > 0 && fh <= 0) fh = fw * vh / vw;
        else if (fh > 0 && fw <= 0) fw = fh * vw / vh;
        else if (fw <= 0 && fh <= 0) { fw = vw; fh = vh; }
    }
    if (fw <= 0 || fh <= 0) { if (fw <= 0 && fh <= 0) return false; if (fw <= 0) fw = 300; if (fh <= 0) fh = 150; }
    if (fw > 8000) fw = 8000;
    if (fh > 8000) fh = 8000;
    *w = (int)(fw + 0.5f); *h = (int)(fh + 0.5f);
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
    return true;
}

static struct svg_doc *doc_for(struct node *root, struct page *owned) {
    struct svg_doc *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->page = owned;
    d->root = root;
    d->has_vb = viewbox_of(root, &d->vb_x, &d->vb_y, &d->vb_w, &d->vb_h);
    int w = 0, h = 0;
    if (svg_node_size(root, &w, &h)) { d->width = w; d->height = h; }
    d->cc = root->style ? root->style->color : 0xFF000000u;
    return d;
}

struct svg_doc *svg_parse_text(const char *text, int length) {
    if (!text || length <= 0 || length > 4000000) return NULL;
    struct page *p = web_page_new();
    if (!p) return NULL;
    web_page_load(p, text, length, "", false);
    struct node *root = p->root ? find_svg(p->root, 0) : NULL;
    if (!root) { web_page_free(p); return NULL; }
    web_page_layout(p, 300, 150, false);
    root = find_svg(p->root, 0);
    struct svg_doc *d = root ? doc_for(root, p) : NULL;
    if (!d) web_page_free(p);
    return d;
}

struct svg_doc *svg_from_node(struct page *p, struct node *n) {
    struct svg_doc *d = doc_for(n, NULL);
    if (d) d->host = p;
    return d;
}

void svg_size(struct svg_doc *d, int *w, int *h) {
    if (!d) { *w = *h = 0; return; }
    *w = d->width; *h = d->height;
}

void svg_free(struct svg_doc *d) {
    if (!d) return;
    if (d->page) web_page_free(d->page);
    free(d);
}

void svg_render(struct svg_doc *d, uint32_t *out, int w, int h, uint32_t cc) {
    if (!d || !out || w <= 0 || h <= 0 || !d->root) return;
    struct ctx C;
    memset(&C, 0, sizeof(C));
    C.doc = d; C.out = out; C.w = w; C.h = h; C.budget = 6000;
    float uw = d->has_vb ? d->vb_w : (d->width > 0 ? (float)d->width : (float)w);
    float uh = d->has_vb ? d->vb_h : (d->height > 0 ? (float)d->height : (float)h);
    C.vw = uw; C.vh = uh;
    struct mat m;
    if (d->has_vb) m = fit_viewbox(aget(d->root, "preserveAspectRatio"), d->vb_x, d->vb_y, d->vb_w, d->vb_h, (float)w, (float)h);
    else { m = m_id(); m.a = (float)w / uw; m.d = (float)h / uh; }
    struct gs g;
    memset(&g, 0, sizeof(g));
    g.fill.color = 0xFF000000u; g.stroke.none = true;
    g.fill_op = g.stroke_op = g.op = 1.f; g.sw = 1.f; g.miter = 4.f; g.font_px = 16.f;
    g.cc = d->root->style ? d->root->style->color : cc;
    if (!d->root->style) g.cc = cc;
    draw_node(&C, d->root, &g, m, false, 0);
}

// ---------------------------------------------------------------- <svg> inside a page

struct svg_cent {
    struct node *node;
    int w, h, gen;
    uint32_t cc;
    uint32_t *px;
    struct svg_doc *doc;
    struct svg_cent *next;
};

struct svg_cache { struct svg_cent *first; size_t bytes; };

void svg_cache_free(struct page *p) {
    struct svg_cache *c = p->svgc;
    if (!c) return;
    while (c->first) { struct svg_cent *e = c->first; c->first = e->next; free(e->px); svg_free(e->doc); free(e); }
    free(c);
    p->svgc = NULL;
}

const uint32_t *web_box_svg(struct page *p, struct box *b, int w, int h) {
    if (!b || !b->node || w <= 0 || h <= 0 || (long long)w * h > 16000000LL) return NULL;
    struct svg_cache *c = p->svgc;
    if (!c) { c = calloc(1, sizeof(*c)); if (!c) return NULL; p->svgc = c; }
    uint32_t cc = b->style ? b->style->color : 0xFF000000u;
    struct svg_cent **link = &c->first;
    for (struct svg_cent *e = c->first; e; link = &e->next, e = e->next) {
        if (e->node == b->node && e->w == w && e->h == h && e->cc == cc && e->gen == p->generation) {
            *link = e->next; e->next = c->first; c->first = e;          // most recent first
            return e->px;
        }
    }
    struct svg_cent *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->px = calloc((size_t)w * (size_t)h, 4);
    e->doc = svg_from_node(p, b->node);
    if (!e->px || !e->doc) { free(e->px); svg_free(e->doc); free(e); return NULL; }
    svg_render(e->doc, e->px, w, h, cc);
    e->node = b->node; e->w = w; e->h = h; e->cc = cc; e->gen = p->generation;
    e->next = c->first; c->first = e;
    c->bytes += (size_t)w * (size_t)h * 4;
    // keep it to a sensible size: drop the oldest
    while (c->bytes > 24u * 1024u * 1024u && c->first && c->first->next) {
        struct svg_cent **pl = &c->first;
        while ((*pl)->next) pl = &(*pl)->next;
        struct svg_cent *old = *pl;
        *pl = NULL;
        c->bytes -= (size_t)old->w * (size_t)old->h * 4;
        free(old->px); svg_free(old->doc); free(old);
    }
    return e->px;
}
