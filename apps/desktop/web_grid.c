// web_grid.c - grid layout (display: grid).

#include "web_lay.h"

#define INF 0x3fffffff
#define MAXC 64
#define MAXR 4096

void web_register_abs(struct lay *L, struct box *a, struct box *host, int sx, int sy);

struct gt {
    uint8_t min_k, max_k;
    int min_v, max_v;
    int base, limit, fr;     // fr in thousandths
    int limit_content;
    int size, pos;
    bool content_min, content_max;
};

struct gi {
    struct box *b;
    int r0, c0, rs, cs;
    bool row_fixed, col_fixed;
    int cmin, cmax;           // contribution on the axis being sized (outer)
    int ml, mr, mt, mb;
    bool aml, amr, amt, amb;
};

static void norm_track(const struct track *t, struct gt *g, int avail) {
    memset(g, 0, sizeof(*g));
    (void)avail;
    switch (t->kind) {
    case TK_PX: g->min_k = g->max_k = TK_PX; g->min_v = g->max_v = t->v; break;
    case TK_PCT: g->min_k = g->max_k = TK_PCT; g->min_v = g->max_v = t->v; break;
    case TK_FR: g->min_k = TK_AUTO; g->max_k = TK_FR; g->max_v = t->v; break;
    case TK_MIN_CONTENT: g->min_k = g->max_k = TK_MIN_CONTENT; break;
    case TK_MAX_CONTENT: g->min_k = g->max_k = TK_MAX_CONTENT; break;
    case TK_MINMAX: g->min_k = t->min_kind; g->min_v = t->min_v; g->max_k = t->max_kind; g->max_v = t->max_v;
        if (g->min_k == TK_FR) g->min_k = TK_AUTO; break;
    default: g->min_k = g->max_k = TK_AUTO; break;
    }
}

// ---- Named areas ----

static bool find_area(const char *areas, const char *name, int *r0, int *r1, int *c0, int *c1) {
    if (!areas) return false;
    int row = 0;
    *r0 = *r1 = *c0 = *c1 = 0;
    bool found = false;
    const char *p = areas;
    int nl = w_len(name);
    while (*p) {
        int col = 0;
        const char *e = p;
        while (*e && *e != '\n') e++;
        const char *q = p;
        while (q < e) {
            while (q < e && w_is_space(*q)) q++;
            if (q >= e) break;
            const char *ws = q;
            while (q < e && !w_is_space(*q)) q++;
            if ((int)(q - ws) == nl && memcmp(ws, name, (size_t)nl) == 0) {
                if (!found) { *r0 = row; *c0 = col; *r1 = row + 1; *c1 = col + 1; found = true; }
                else { if (row + 1 > *r1) *r1 = row + 1; if (col + 1 > *c1) *c1 = col + 1; if (col < *c0) *c0 = col; }
            }
            col++;
        }
        row++;
        p = *e ? e + 1 : e;
    }
    return found;
}

static void areas_dims(const char *areas, int *rows, int *cols) {
    *rows = *cols = 0;
    if (!areas) return;
    const char *p = areas;
    while (*p) {
        int col = 0;
        const char *e = p;
        while (*e && *e != '\n') e++;
        const char *q = p;
        while (q < e) {
            while (q < e && w_is_space(*q)) q++;
            if (q >= e) break;
            while (q < e && !w_is_space(*q)) q++;
            col++;
        }
        if (col > *cols) *cols = col;
        (*rows)++;
        p = *e ? e + 1 : e;
    }
}

// ---- Sizing a row or column of tracks ----

static void size_tracks(struct gt *t, int n, int avail, int gap, struct gi *it, int ni, bool cols, int justify) {
    if (n == 0) return;
    // starting sizes
    for (int i = 0; i < n; i++) {
        struct gt *g = &t[i];
        g->base = 0; g->limit = INF; g->fr = 0; g->limit_content = 0;
        g->content_min = g->content_max = false;
        switch (g->min_k) {
        case TK_PX: g->base = g->min_v; break;
        case TK_PCT: g->base = avail < 0 ? 0 : (int)((long long)avail * g->min_v / 10000); break;
        default: g->content_min = true; break;
        }
        switch (g->max_k) {
        case TK_PX: g->limit = g->max_v; break;
        case TK_PCT: g->limit = avail < 0 ? INF : (int)((long long)avail * g->max_v / 10000); break;
        case TK_FR: g->fr = g->max_v; g->limit = INF; break;
        default: g->content_max = true; break;
        }
        if (g->limit < g->base) g->limit = g->base;
    }
    // single-span items
    for (int k = 0; k < ni; k++) {
        struct gi *x = &it[k];
        int st = cols ? x->c0 : x->r0, sp = cols ? x->cs : x->rs;
        if (sp != 1 || st < 0 || st >= n) continue;
        struct gt *g = &t[st];
        if (g->content_min) {
            if (g->min_k == TK_MAX_CONTENT) { if (x->cmax > g->base) g->base = x->cmax; }
            else if (x->cmin > g->base) g->base = x->cmin;
        }
        if (g->content_max) {
            int v = g->max_k == TK_MIN_CONTENT ? x->cmin : x->cmax;
            if (v > g->limit_content) g->limit_content = v;
        }
        if (g->fr && g->content_min && x->cmin > g->base) g->base = x->cmin;
    }
    for (int i = 0; i < n; i++) {
        struct gt *g = &t[i];
        if (g->content_max) { g->limit = g->limit_content > g->base ? g->limit_content : g->base; }
        if (g->limit < g->base) g->limit = g->base;
    }
    // items spanning several tracks, smallest first
    for (int span = 2; span <= n; span++) {
        for (int k = 0; k < ni; k++) {
            struct gi *x = &it[k];
            int st = cols ? x->c0 : x->r0, sp = cols ? x->cs : x->rs;
            if (sp != span || st < 0 || st + sp > n) continue;
            long long sb = (long long)(sp - 1) * gap, sl = sb;
            int flex_cnt = 0, cmin_cnt = 0, cmax_cnt = 0;
            for (int i = st; i < st + sp; i++) { sb += t[i].base; sl += t[i].limit >= INF ? t[i].base : t[i].limit; if (t[i].content_min) cmin_cnt++; if (t[i].content_max) cmax_cnt++; if (t[i].fr) flex_cnt++; }
            if (sb < x->cmin) {
                int extra = (int)(x->cmin - sb);
                int cnt = cmin_cnt ? cmin_cnt : sp;
                int each = extra / cnt, rem = extra - each * cnt;
                for (int i = st; i < st + sp; i++) if (cmin_cnt == 0 || t[i].content_min) { t[i].base += each + (rem > 0 ? 1 : 0); if (rem > 0) rem--; if (t[i].limit < t[i].base) t[i].limit = t[i].base; }
            }
            if (cmax_cnt && sl < x->cmax) {
                int extra = (int)(x->cmax - sl);
                int each = extra / cmax_cnt, rem = extra - each * cmax_cnt;
                for (int i = st; i < st + sp; i++) if (t[i].content_max) { if (t[i].limit >= INF) t[i].limit = t[i].base; t[i].limit += each + (rem > 0 ? 1 : 0); if (rem > 0) rem--; }
            }
        }
    }
    for (int i = 0; i < n; i++) { t[i].size = t[i].base; }
    long long gaps = (long long)(n - 1) * gap;
    if (avail < 0) {
        // indefinite: content sizes (fr tracks use their content)
        for (int i = 0; i < n; i++) if (t[i].fr && t[i].limit < INF && t[i].limit > t[i].size) t[i].size = t[i].limit;
        for (int i = 0; i < n; i++) if (t[i].content_max && t[i].limit < INF && t[i].limit > t[i].size && !t[i].fr) t[i].size = t[i].limit;
        return;
    }
    // maximize tracks up to their growth limits
    long long used = gaps;
    for (int i = 0; i < n; i++) used += t[i].size;
    long long freesp = avail - used;
    bool have_fr = false;
    for (int i = 0; i < n; i++) if (t[i].fr) have_fr = true;
    if (!have_fr && freesp > 0) {
        for (int iter = 0; iter < 20 && freesp > 0; iter++) {
            int cnt = 0;
            for (int i = 0; i < n; i++) if (t[i].size < t[i].limit && t[i].limit < INF) cnt++;
            if (!cnt) break;
            long long share = freesp / cnt; if (share < 1) share = 1;
            for (int i = 0; i < n && freesp > 0; i++) {
                if (t[i].size < t[i].limit && t[i].limit < INF) {
                    long long g = t[i].limit - t[i].size; if (g > share) g = share; if (g > freesp) g = freesp;
                    t[i].size += (int)g; freesp -= g;
                }
            }
        }
        // stretch auto tracks
        if (freesp > 0 && (justify == FA_NORMAL || justify == FA_STRETCH || justify == FA_AUTO)) {
            int cnt = 0;
            for (int i = 0; i < n; i++) if (t[i].max_k == TK_AUTO) cnt++;
            if (cnt) {
                int each = (int)(freesp / cnt), rem = (int)(freesp - (long long)each * cnt);
                for (int i = 0; i < n; i++) if (t[i].max_k == TK_AUTO) { t[i].size += each + (rem > 0 ? 1 : 0); if (rem > 0) rem--; }
            }
        }
    } else if (have_fr) {
        long long fixed = gaps;
        long long sumfr = 0;
        for (int i = 0; i < n; i++) { if (t[i].fr) sumfr += t[i].fr; else fixed += t[i].size; }
        bool *active = calloc((size_t)n + 1, 1);
        int m = n;
        for (int i = 0; i < m; i++) active[i] = t[i].fr != 0;
        long long leftover = avail - fixed;
        if (leftover < 0) leftover = 0;
        long long unit = 0;     // thousandths of px per fr thousandth... computed as px * 1000 / fr
        for (int iter = 0; iter < 20; iter++) {
            long long fr_total = 0, left = leftover;
            for (int i = 0; i < m; i++) { if (active[i]) fr_total += t[i].fr; else if (t[i].fr) left -= t[i].size; }
            if (fr_total == 0) break;
            if (fr_total < 1000) { left = left * fr_total / 1000; fr_total = 1000; }
            unit = left * 1000 / fr_total;      // px per whole fr, times 1000
            bool changed = false;
            for (int i = 0; i < m; i++) {
                if (active[i] && (long long)t[i].fr * unit / 1000 < t[i].base) { active[i] = false; t[i].size = t[i].base; changed = true; }
            }
            if (!changed) break;
        }
        (void)sumfr;
        for (int i = 0; i < m; i++) if (active[i]) { long long sz = (long long)t[i].fr * unit / 1000; t[i].size = (int)(sz > t[i].base ? sz : t[i].base); }
        free(active);
    }
}

static void place_tracks(struct gt *t, int n, int gap, int avail, int justify, int *total) {
    long long used = (long long)(n > 0 ? n - 1 : 0) * gap;
    for (int i = 0; i < n; i++) used += t[i].size;
    int lead = 0, between = gap;
    long long freesp = avail >= 0 ? avail - used : 0;
    if (freesp > 0 && n > 0) {
        if (justify == FA_CENTER) lead = (int)(freesp / 2);
        else if (justify == FA_END) lead = (int)freesp;
        else if (justify == FA_SPACE_BETWEEN && n > 1) between = gap + (int)(freesp / (n - 1));
        else if (justify == FA_SPACE_AROUND) { int e = (int)(freesp / n); lead = e / 2; between = gap + e; }
        else if (justify == FA_SPACE_EVENLY) { int e = (int)(freesp / (n + 1)); lead = e; between = gap + e; }
    }
    int pos = lead;
    for (int i = 0; i < n; i++) { t[i].pos = pos; pos += t[i].size + between; }
    *total = (int)used;
}

static bool occ_free(unsigned char *occ, int cols, int r, int c, int rs, int cs) {
    for (int y = r; y < r + rs; y++) for (int x = c; x < c + cs; x++) if (occ[y * MAXC + x]) return false;
    return true;
}

static void occ_set(unsigned char *occ, int r, int c, int rs, int cs) {
    for (int y = r; y < r + rs; y++) for (int x = c; x < c + cs; x++) occ[y * MAXC + x] = 1;
}

static void item_margins(struct gi *x, int cb_w) {
    struct box *c = x->b;
    lay_edges(c, cb_w);
    struct style *cs = c->style;
    x->ml = c->margin.left; x->mr = c->margin.right; x->mt = c->margin.top; x->mb = c->margin.bottom;
    x->aml = cs->margin[3].kind == L_AUTO; x->amr = cs->margin[1].kind == L_AUTO;
    x->amt = cs->margin[0].kind == L_AUTO; x->amb = cs->margin[2].kind == L_AUTO;
}

void lay_grid(struct lay *L, struct box *b, const struct lin *in, int content_w, int *content_h) {
    struct style *s = b->style;
    int extra_v = b->pad.top + b->pad.bottom + b->border.top + b->border.bottom;
    int def_h = -1;
    if (in->forced_h >= 0) def_h = in->forced_h - extra_v;
    else if (s->height.kind == L_VAL && !((s->height.pct || s->height.expr) && in->cb_h < 0)) {
        int h = len_to_px(&s->height, in->cb_h, 0);
        def_h = s->box_sizing == 0 ? h : h - extra_v;
    }
    if (def_h < -1) def_h = 0;
    int min_h = 0;
    if (s->min_height.kind == L_VAL && !((s->min_height.pct || s->min_height.expr) && in->cb_h < 0)) {
        min_h = len_to_px(&s->min_height, in->cb_h, 0);
        if (s->box_sizing != 0) min_h -= extra_v;
        if (min_h < 0) min_h = 0;
    }
    int col_gap = len_to_px(&s->column_gap, content_w, 0), row_gap = len_to_px(&s->row_gap, def_h, 0);
    if (col_gap < 0) col_gap = 0; if (row_gap < 0) row_gap = 0;
    int offx = b->border.left + b->pad.left, offy = b->border.top + b->pad.top;

    // explicit tracks
    struct gt cols[MAXC];
    struct gt *rows = calloc(MAXR, sizeof(struct gt));
    int ncols = 0, nrows = 0;
    struct gt *rows_heap = rows;
    int ex_cols = 0, ex_rows = 0;
    for (int i = 0; i < s->col_track_count; i++) {
        const struct track *t = &s->col_tracks[i];
        if (t->repeat) {
            // how many fit?
            int sz = 0;
            if (t->kind == TK_PX) sz = t->v; else if (t->kind == TK_MINMAX && t->min_kind == TK_PX) sz = t->min_v;
            else if (t->kind == TK_MINMAX && t->min_kind == TK_PCT) sz = (int)((long long)content_w * t->min_v / 10000);
            else if (t->kind == TK_PCT) sz = (int)((long long)content_w * t->v / 10000);
            else sz = 1;
            if (sz < 1) sz = 1;
            int rep = (content_w + col_gap) / (sz + col_gap);
            if (rep < 1) rep = 1;
            // other explicit tracks use room too
            int others = 0, others_n = 0;
            for (int k = 0; k < s->col_track_count; k++) if (k != i && !s->col_tracks[k].repeat) { const struct track *o = &s->col_tracks[k]; others += (o->kind == TK_PX ? o->v : (o->kind == TK_MINMAX && o->min_kind == TK_PX ? o->min_v : 0)); others_n++; }
            if (others_n) { rep = (content_w - others - others_n * col_gap + col_gap) / (sz + col_gap); if (rep < 1) rep = 1; }
            for (int r = 0; r < rep && ncols < MAXC; r++) norm_track(t, &cols[ncols++], content_w);
        } else if (ncols < MAXC) norm_track(t, &cols[ncols++], content_w);
    }
    if (ncols == 0 && s->grid_areas) { int ar, ac; areas_dims(s->grid_areas, &ar, &ac); (void)ar; }
    ex_cols = ncols;
    for (int i = 0; i < s->row_track_count && nrows < MAXR; i++) {
        const struct track *t = &s->row_tracks[i];
        int rep = 1;
        if (t->repeat) rep = 1;
        for (int r = 0; r < rep && nrows < MAXR; r++) norm_track(t, &rows[nrows++], def_h);
    }
    // areas define a grid when tracks are missing
    if (s->grid_areas) {
        int ar, ac; areas_dims(s->grid_areas, &ar, &ac);
        struct track at; memset(&at, 0, sizeof(at)); at.kind = TK_AUTO;
        while (nrows < ar && nrows < MAXR) norm_track(&at, &rows[nrows++], def_h);
        while (ncols < ac && ncols < MAXC) norm_track(&at, &cols[ncols++], content_w);
        ex_cols = ncols;
    }
    ex_rows = nrows;
    bool flow_col = s->grid_auto_flow_column;
    if (ncols == 0) { struct track at = s->auto_col; norm_track(&at, &cols[ncols++], content_w); if (flow_col) ex_cols = 0; }

    // items
    int n = 0;
    for (struct box *c = b->first; c; c = c->next) n++;
    struct gi *it = calloc((size_t)(n ? n : 1), sizeof(struct gi));
    int cnt = 0;
    for (struct box *c = b->first; c; c = c->next) {
        if (c->out_of_flow) { web_register_abs(L, c, b, 0, 0); continue; }
        it[cnt].b = c; cnt++;
    }
    // stable sort by order
    for (int i = 1; i < cnt; i++) { struct gi t = it[i]; int j = i; while (j > 0 && it[j - 1].b->style->order > t.b->style->order) { it[j] = it[j - 1]; j--; } it[j] = t; }
    int max_r = 512;
    unsigned char *occ = calloc((size_t)max_r * MAXC, 1);
    // explicit positions first
    for (int i = 0; i < cnt; i++) {
        struct gi *x = &it[i];
        struct style *cs = x->b->style;
        x->rs = x->cs = 1; x->r0 = x->c0 = -1;
        int r0, r1, c0, c1;
        if (cs->grid_area && find_area(s->grid_areas, cs->grid_area, &r0, &r1, &c0, &c1)) {
            x->r0 = r0; x->rs = r1 - r0; x->c0 = c0; x->cs = c1 - c0; x->row_fixed = x->col_fixed = true;
            continue;
        }
        // columns
        {
            int st = cs->col_start, en = cs->col_end, sp = cs->col_span;
            int ncl = ex_cols;
            if (st < 0) st = ncl + 2 + st;
            if (en < 0) en = ncl + 2 + en;
            if (st > 0 && en > 0) { x->c0 = st - 1; x->cs = en > st ? en - st : 1; x->col_fixed = true; }
            else if (st > 0) { x->c0 = st - 1; x->cs = sp > 0 ? sp : 1; x->col_fixed = true; }
            else if (en > 0) { x->cs = sp > 0 ? sp : 1; x->c0 = en - 1 - x->cs; if (x->c0 < 0) x->c0 = 0; x->col_fixed = true; }
            else { x->cs = sp > 0 ? sp : 1; }
        }
        {
            int st = cs->row_start, en = cs->row_end, sp = cs->row_span;
            int nrl = ex_rows;
            if (st < 0) st = nrl + 2 + st;
            if (en < 0) en = nrl + 2 + en;
            if (st > 0 && en > 0) { x->r0 = st - 1; x->rs = en > st ? en - st : 1; x->row_fixed = true; }
            else if (st > 0) { x->r0 = st - 1; x->rs = sp > 0 ? sp : 1; x->row_fixed = true; }
            else if (en > 0) { x->rs = sp > 0 ? sp : 1; x->r0 = en - 1 - x->rs; if (x->r0 < 0) x->r0 = 0; x->row_fixed = true; }
            else { x->rs = sp > 0 ? sp : 1; }
        }
        if (x->cs > MAXC) x->cs = MAXC;
        if (x->rs > 200) x->rs = 200;
    }
    #define ENSURE_ROWS(need) do { int nd_ = (need); if (nd_ + 4 > max_r) { int nm = max_r; while (nm < nd_ + 4) nm *= 2; unsigned char *no_ = realloc(occ, (size_t)nm * MAXC); if (no_) { memset(no_ + (size_t)max_r * MAXC, 0, (size_t)(nm - max_r) * MAXC); occ = no_; max_r = nm; } } } while (0)
    int used_rows = 0;
    for (int i = 0; i < cnt; i++) {
        struct gi *x = &it[i];
        if (x->row_fixed && x->col_fixed) {
            if (x->c0 + x->cs > MAXC) x->c0 = MAXC - x->cs;
            ENSURE_ROWS(x->r0 + x->rs);
            occ_set(occ, x->r0, x->c0, x->rs, x->cs);
            if (x->r0 + x->rs > used_rows) used_rows = x->r0 + x->rs;
            if (x->c0 + x->cs > ncols) { struct track at = s->auto_col; while (ncols < x->c0 + x->cs && ncols < MAXC) norm_track(&at, &cols[ncols++], content_w); }
        }
    }
    // auto placement (rows direction; columns direction is the same with the axes swapped)
    int cur_r = 0, cur_c = 0;
    bool dense = s->grid_dense;
    for (int i = 0; i < cnt; i++) {
        struct gi *x = &it[i];
        if (x->row_fixed && x->col_fixed) continue;
        if (!flow_col) {
            int ncl = ncols;
            if (x->cs > ncl) x->cs = ncl;
            if (x->col_fixed) {
                int c = x->c0;
                if (c + x->cs > MAXC) { c = MAXC - x->cs; x->c0 = c; }
                if (c + x->cs > ncols) { struct track at = s->auto_col; while (ncols < c + x->cs && ncols < MAXC) norm_track(&at, &cols[ncols++], content_w); }
                int r = dense ? 0 : (cur_c > c ? cur_r + 1 : cur_r);
                if (x->row_fixed) r = x->r0;
                for (;; r++) { ENSURE_ROWS(r + x->rs); if (occ_free(occ, ncols, r, c, x->rs, x->cs)) break; if (x->row_fixed) break; }
                x->r0 = r; x->c0 = c;
                if (!dense) { cur_r = r; cur_c = c; }
            } else if (x->row_fixed) {
                int r = x->r0, c = 0;
                ENSURE_ROWS(r + x->rs);
                for (; c + x->cs <= ncl; c++) if (occ_free(occ, ncols, r, c, x->rs, x->cs)) break;
                if (c + x->cs > ncl) c = 0;
                x->c0 = c;
            } else {
                int r = dense ? 0 : cur_r, c = dense ? 0 : cur_c;
                for (;; ) {
                    ENSURE_ROWS(r + x->rs);
                    if (c + x->cs <= ncl && occ_free(occ, ncols, r, c, x->rs, x->cs)) break;
                    c++;
                    if (c + x->cs > ncl) { c = 0; r++; }
                    if (r > 20000) break;
                }
                x->r0 = r; x->c0 = c;
                if (!dense) { cur_r = r; cur_c = c; }
            }
            ENSURE_ROWS(x->r0 + x->rs);
            occ_set(occ, x->r0, x->c0, x->rs, x->cs);
            if (x->r0 + x->rs > used_rows) used_rows = x->r0 + x->rs;
        } else {
            // flow by columns: rows are the fixed count
            int nrl = nrows > 0 ? nrows : 1;
            if (x->rs > nrl) x->rs = nrl;
            int r = 0, c = dense ? 0 : cur_c;
            if (x->col_fixed) c = x->c0;
            for (;;) {
                if (c + x->cs > MAXC) break;
                ENSURE_ROWS(nrl + x->rs);
                if (x->row_fixed) r = x->r0;
                if (r + x->rs <= nrl && occ_free(occ, ncols, r, c, x->rs, x->cs)) break;
                if (x->row_fixed) { c++; continue; }
                r++;
                if (r + x->rs > nrl) { r = 0; c++; }
            }
            if (c + x->cs > MAXC) { c = MAXC - x->cs; }
            x->r0 = r; x->c0 = c;
            if (!dense) cur_c = c;
            occ_set(occ, x->r0, x->c0, x->rs, x->cs);
            if (x->r0 + x->rs > used_rows) used_rows = x->r0 + x->rs;
            while (ncols < c + x->cs && ncols < MAXC) { struct track at = s->auto_col; norm_track(&at, &cols[ncols++], content_w); }
        }
    }
    free(occ);
    // implicit rows
    if (used_rows > MAXR) used_rows = MAXR;
    while (nrows < used_rows) { struct track at = s->auto_row; norm_track(&at, &rows[nrows++], def_h); }
    if (nrows == 0) { /* nothing to place */ }
    for (int i = 0; i < cnt; i++) {
        struct gi *x = &it[i];
        if (x->r0 + x->rs > nrows) x->rs = nrows - x->r0 > 0 ? nrows - x->r0 : 1;
        if (x->c0 + x->cs > ncols) x->cs = ncols - x->c0 > 0 ? ncols - x->c0 : 1;
    }

    // ---- columns ----
    for (int i = 0; i < cnt; i++) {
        struct gi *x = &it[i];
        item_margins(x, content_w);
        lay_intrinsic(L, x->b);
        int om = x->ml + x->mr;
        // a definite width gives both contributions
        x->cmin = x->b->min_content + om; x->cmax = x->b->max_content + om;
        // min-content contribution of an item with a flexible track: an automatic minimum
    }
    int jc = s->justify_content;
    size_tracks(cols, ncols, content_w, col_gap, it, cnt, true, jc);
    int cols_total;
    place_tracks(cols, ncols, col_gap, content_w, jc, &cols_total);

    // ---- lay out items at their column widths ----
    for (int i = 0; i < cnt; i++) {
        struct gi *x = &it[i];
        struct box *c = x->b;
        struct style *cs = c->style;
        int aw = 0;
        for (int k = x->c0; k < x->c0 + x->cs && k < ncols; k++) aw += cols[k].size;
        aw += (x->cs - 1) * col_gap;
        int js = cs->justify_self != FA_AUTO && cs->justify_self != 0 ? cs->justify_self : s->justify_items;
        if (js == FA_AUTO || js == FA_NORMAL) js = c->type == BT_REPLACED ? FA_START : FA_STRETCH;
        struct lin li;
        memset(&li, 0, sizeof(li));
        li.cb_w = aw; li.cb_h = -1; li.avail_w = aw; li.forced_w = -1; li.forced_h = -1;
        bool wauto = cs->width.kind == L_AUTO;
        int avail = aw - x->ml - x->mr;
        if (js == FA_STRETCH && wauto && c->type != BT_REPLACED && !x->aml && !x->amr) { li.forced_w = avail < 0 ? 0 : avail; }
        else if (wauto) { li.shrink = true; li.avail_w = aw; }
        c->abs_first = c->abs_last = NULL;
        lay_box(L, c, &li);
        x->cmin = x->cmax = c->h + x->mt + x->mb;
    }
    // ---- rows ----
    int ac_ = s->align_content;
    int row_avail = def_h >= 0 ? def_h : (min_h > 0 ? min_h : -1);
    size_tracks(rows, nrows, row_avail, row_gap, it, cnt, false, ac_);
    int rows_total;
    place_tracks(rows, nrows, row_gap, row_avail, ac_, &rows_total);

    // ---- place ----
    for (int i = 0; i < cnt; i++) {
        struct gi *x = &it[i];
        struct box *c = x->b;
        struct style *cs = c->style;
        int aw = 0, ah = 0;
        for (int k = x->c0; k < x->c0 + x->cs && k < ncols; k++) aw += cols[k].size;
        aw += (x->cs - 1) * col_gap;
        for (int k = x->r0; k < x->r0 + x->rs && k < nrows; k++) ah += rows[k].size;
        ah += (x->rs - 1) * row_gap;
        int ax = x->c0 < ncols ? cols[x->c0].pos : 0, ay = x->r0 < nrows ? rows[x->r0].pos : 0;
        int al = cs->align_self != FA_AUTO ? cs->align_self : s->align_items;
        if (al == FA_AUTO || al == FA_NORMAL) al = c->type == BT_REPLACED ? FA_START : FA_STRETCH;
        int js = cs->justify_self != FA_AUTO && cs->justify_self != 0 ? cs->justify_self : s->justify_items;
        if (js == FA_AUTO || js == FA_NORMAL) js = c->type == BT_REPLACED ? FA_START : FA_STRETCH;
        // vertical
        int free_v = ah - c->h - x->mt - x->mb;
        int y = ay + x->mt;
        if (x->amt && x->amb && free_v > 0) y = ay + free_v / 2 + x->mt;
        else if (x->amt && free_v > 0) y = ay + free_v + x->mt;
        else if (al == FA_STRETCH && cs->height.kind == L_AUTO && !x->amt && !x->amb) {
            int target = ah - x->mt - x->mb;
            if (target > c->h) {
                int d = c->style->display;
                if (d == D_FLEX || d == D_GRID || d == D_TABLE || c->abs_first) {
                    struct lin li; memset(&li, 0, sizeof(li));
                    int w = c->w;
                    li.cb_w = aw; li.cb_h = -1; li.avail_w = aw; li.forced_w = w; li.forced_h = target;
                    c->abs_first = c->abs_last = NULL;
                    lay_box(L, c, &li);
                } else c->h = target;
            }
        }
        else if (al == FA_CENTER) y = ay + free_v / 2 + x->mt;
        else if (al == FA_END) y = ay + free_v + x->mt;
        int free_h = aw - c->w - x->ml - x->mr;
        int xx = ax + x->ml;
        if (x->aml && x->amr && free_h > 0) xx = ax + free_h / 2 + x->ml;
        else if (x->aml && free_h > 0) xx = ax + free_h + x->ml;
        else if (js == FA_CENTER) xx = ax + free_h / 2 + x->ml;
        else if (js == FA_END) xx = ax + free_h + x->ml;
        c->x = offx + xx; c->y = offy + y;
    }
    int ch = rows_total;
    if (def_h >= 0) ch = def_h;
    else if (min_h > ch) ch = min_h;
    // the tracks may not reach the end of a definite container: fine
    *content_h = ch;
    free(rows_heap);
    free(it);
}

void intr_grid(struct lay *L, struct box *b, int *mn, int *mx) {
    struct style *s = b->style;
    int ncol = s->col_track_count;
    int count = 0, sum_mn = 0, sum_mx = 0, max_mn = 0, max_mx = 0;
    int col_gap = len_to_px(&s->column_gap, 0, 0);
    // explicit pixel columns give a width directly
    long long fixed = 0; bool all_fixed = ncol > 0;
    for (int i = 0; i < ncol; i++) {
        const struct track *t = &s->col_tracks[i];
        if (t->repeat) { all_fixed = false; continue; }
        if (t->kind == TK_PX) fixed += t->v; else all_fixed = false;
    }
    for (struct box *c = b->first; c; c = c->next) {
        if (c->out_of_flow) continue;
        lay_intrinsic(L, c);
        int om = c->style->margin[3].kind == L_VAL && !c->style->margin[3].pct ? c->style->margin[3].px : 0;
        om += c->style->margin[1].kind == L_VAL && !c->style->margin[1].pct ? c->style->margin[1].px : 0;
        int cmn = c->min_content + om, cmx = c->max_content + om;
        sum_mn += cmn; sum_mx += cmx;
        if (cmn > max_mn) max_mn = cmn; if (cmx > max_mx) max_mx = cmx;
        count++;
    }
    int cols = ncol > 0 ? ncol : 1;
    for (int i = 0; i < ncol; i++) if (s->col_tracks[i].repeat) cols = 1;
    if (all_fixed && ncol > 0) { *mn = *mx = (int)fixed + col_gap * (ncol - 1); return; }
    // rough: items spread over `cols` columns
    int per_row_mx = 0, per_row_mn = 0;
    (void)per_row_mx; (void)per_row_mn;
    if (cols <= 1) { *mn = max_mn; *mx = max_mx; }
    else {
        int rows_ = (count + cols - 1) / cols;
        (void)rows_;
        *mn = max_mn * 1; *mx = max_mx * cols + col_gap * (cols - 1);
        if (*mx > sum_mx) *mx = sum_mx + col_gap * (count > 1 ? count - 1 : 0);
    }
    if (*mn > *mx) *mn = *mx;
}
