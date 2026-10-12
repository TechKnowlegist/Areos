// web_table.c - tables.

#include "web_lay.h"

#define INF 0x3fffffff

struct tcell {
    struct box *b;
    int r, c, rs, cs;
    int cmin, cmax;
    int fixed, pct;         // asked-for width (px / hundredths of percent), or 0
    int x, y, w, h;         // in table coordinates
    int base;               // baseline from the cell's top
};

struct trow {
    struct box *b;
    struct box *group;
    int y, h;
    int hint;               // height asked for
};

struct tgrid {
    struct tcell *cells; int ncell, capcell;
    struct trow *rows; int nrow, caprow;
    int ncols;
    int *cmin, *cmax, *cfixed, *cpct, *cw, *cx;
    bool collapse;
    int sp_h, sp_v;
    struct box **caps; int ncap;
    struct box **groups; int ngroup;
    unsigned char *occ; int occ_cols, occ_rows;
};

static int attr_span(const struct node *n, const char *name, int cap) {
    if (!n) return 1;
    const char *v = node_attr(n, name);
    if (!v) return 1;
    int x = 0;
    while (w_is_space(*v)) v++;
    while (w_is_digit(*v) && x < 100000) { x = x * 10 + (*v - '0'); v++; }
    if (x < 1) x = 1;
    if (x > cap) x = cap;
    return x;
}

static void add_row(struct tgrid *G, struct box *r, struct box *group) {
    if (G->nrow == G->caprow) {
        int nc = G->caprow ? G->caprow * 2 : 16;
        G->rows = realloc(G->rows, sizeof(struct trow) * (size_t)nc);
        G->caprow = nc;
    }
    G->rows[G->nrow].b = r; G->rows[G->nrow].group = group; G->rows[G->nrow].y = G->rows[G->nrow].h = 0; G->rows[G->nrow].hint = 0;
    G->nrow++;
}

static void collect_rows(struct tgrid *G, struct box *t) {
    // header groups first, then the rest, then footers
    for (int pass = 0; pass < 3; pass++) {
        for (struct box *c = t->first; c; c = c->next) {
            if (c->out_of_flow) continue;
            int d = c->style->display;
            if (d == D_TABLE_ROW) { if (pass == 1) add_row(G, c, NULL); }
            else if (d == D_TABLE_ROW_GROUP || d == D_TABLE_HEADER_GROUP || d == D_TABLE_FOOTER_GROUP) {
                int want = d == D_TABLE_HEADER_GROUP ? 0 : (d == D_TABLE_FOOTER_GROUP ? 2 : 1);
                if (want != pass) continue;
                for (struct box *r = c->first; r; r = r->next) if (!r->out_of_flow && r->style->display == D_TABLE_ROW) add_row(G, r, c);
            }
        }
    }
}

static void add_cell(struct tgrid *G, struct box *b, int r, int c, int rs, int cs) {
    if (G->ncell == G->capcell) {
        int nc = G->capcell ? G->capcell * 2 : 32;
        G->cells = realloc(G->cells, sizeof(struct tcell) * (size_t)nc);
        G->capcell = nc;
    }
    struct tcell *t = &G->cells[G->ncell++];
    memset(t, 0, sizeof(*t));
    t->b = b; t->r = r; t->c = c; t->rs = rs; t->cs = cs;
}

static bool grid_build(struct lay *L, struct box *t, struct tgrid *G) {
    memset(G, 0, sizeof(*G));
    struct style *ts = t->style;
    G->collapse = ts->border_collapse != 0;
    G->sp_h = G->collapse ? 0 : len_to_px(&ts->spacing_h, 0, 0);
    G->sp_v = G->collapse ? 0 : len_to_px(&ts->spacing_v, 0, 0);
    collect_rows(G, t);
    // occupancy
    int maxcols = 1;
    int rows = G->nrow;
    int occ_c = 128;
    int occ_r = rows + 8;
    G->occ = calloc((size_t)occ_r * (size_t)occ_c, 1);
    G->occ_cols = occ_c; G->occ_rows = occ_r;
    for (int r = 0; r < rows; r++) {
        int c = 0;
        for (struct box *cell = G->rows[r].b->first; cell; cell = cell->next) {
            if (cell->out_of_flow || cell->style->display != D_TABLE_CELL) continue;
            int cs = attr_span(cell->node, "colspan", 100);
            int rs = attr_span(cell->node, "rowspan", 1000);
            if (r + rs > rows) rs = rows - r;
            while (c < occ_c && G->occ[r * occ_c + c]) c++;
            if (c + cs > occ_c) cs = occ_c - c > 0 ? occ_c - c : 1;
            if (c >= occ_c) break;
            for (int y = r; y < r + rs; y++) for (int x = c; x < c + cs; x++) G->occ[y * occ_c + x] = 1;
            add_cell(G, cell, r, c, rs, cs);
            if (c + cs > maxcols) maxcols = c + cs;
            c += cs;
        }
    }
    G->ncols = maxcols;
    int n = G->ncols;
    G->cmin = calloc((size_t)n, sizeof(int)); G->cmax = calloc((size_t)n, sizeof(int));
    G->cfixed = calloc((size_t)n, sizeof(int)); G->cpct = calloc((size_t)n, sizeof(int));
    G->cw = calloc((size_t)n, sizeof(int)); G->cx = calloc((size_t)n + 1, sizeof(int));
    // collapsed borders
    if (G->collapse) {
        // per cell: each shared edge gets half of the wider border
        for (int i = 0; i < G->ncell; i++) {
            struct tcell *c = &G->cells[i];
            struct style *cs = c->b->style;
            c->b->has_collapse_border = true;
            for (int k = 0; k < 4; k++) c->b->collapse_border[k] = cs->border_width[k];
        }
        // neighbours: find who is to the right / below via a map
        int *map = calloc((size_t)(rows ? rows : 1) * (size_t)n, sizeof(int));
        for (int i = 0; i < (rows ? rows : 1) * n; i++) map[i] = -1;
        for (int i = 0; i < G->ncell; i++) {
            struct tcell *c = &G->cells[i];
            for (int y = c->r; y < c->r + c->rs; y++) for (int x = c->c; x < c->c + c->cs && x < n; x++) map[y * n + x] = i;
        }
        for (int i = 0; i < G->ncell; i++) {
            struct tcell *c = &G->cells[i];
            // right neighbour
            int rx = c->c + c->cs;
            if (rx < n) {
                int j = map[c->r * n + rx];
                if (j >= 0 && j != i) {
                    int w = W_MAX(c->b->style->border_width[1], G->cells[j].b->style->border_width[3]);
                    c->b->collapse_border[1] = w - w / 2;
                    G->cells[j].b->collapse_border[3] = w / 2;
                }
            }
            int by = c->r + c->rs;
            if (by < rows) {
                int j = map[by * n + c->c];
                if (j >= 0 && j != i) {
                    int w = W_MAX(c->b->style->border_width[2], G->cells[j].b->style->border_width[0]);
                    c->b->collapse_border[2] = w - w / 2;
                    G->cells[j].b->collapse_border[0] = w / 2;
                }
            }
            // outer edges: the table's border is drawn by the table
            if (c->c == 0) { int w = W_MAX(0, c->b->style->border_width[3] - ts->border_width[3]); c->b->collapse_border[3] = w; }
            if (c->c + c->cs >= n) { int w = W_MAX(0, c->b->style->border_width[1] - ts->border_width[1]); c->b->collapse_border[1] = w; }
            if (c->r == 0) { int w = W_MAX(0, c->b->style->border_width[0] - ts->border_width[0]); c->b->collapse_border[0] = w; }
            if (c->r + c->rs >= rows) { int w = W_MAX(0, c->b->style->border_width[2] - ts->border_width[2]); c->b->collapse_border[2] = w; }
        }
        free(map);
    }
    // captions
    for (struct box *c = t->first; c; c = c->next) if (!c->out_of_flow && c->style->display == D_TABLE_CAPTION) G->ncap++;
    if (G->ncap) {
        G->caps = calloc((size_t)G->ncap, sizeof(struct box *));
        int k = 0;
        for (struct box *c = t->first; c; c = c->next) if (!c->out_of_flow && c->style->display == D_TABLE_CAPTION) G->caps[k++] = c;
    }
    (void)L;
    return true;
}

static void grid_free(struct tgrid *G) {
    free(G->cells); free(G->rows); free(G->cmin); free(G->cmax); free(G->cfixed); free(G->cpct); free(G->cw); free(G->cx); free(G->caps); free(G->occ);
}

static void grid_columns(struct lay *L, struct tgrid *G, int cb_w) {
    int n = G->ncols;
    // <col> widths
    struct box *tb = G->nrow ? G->rows[0].b->parent : NULL;
    (void)tb;
    for (int i = 0; i < G->ncell; i++) {
        struct tcell *c = &G->cells[i];
        struct box *b = c->b;
        lay_intrinsic(L, b);
        c->cmin = b->min_content; c->cmax = b->max_content;
        struct style *s = b->style;
        if (s->width.kind == L_VAL) {
            if (s->width.pct && !s->width.expr) c->pct = s->width.pct;
            else if (!s->width.expr) {
                int w = s->width.px;
                lay_edges(b, cb_w);
                int ex = b->pad.left + b->pad.right + b->border.left + b->border.right;
                if (s->box_sizing == 0) w += ex;
                c->fixed = w;
                if (c->cmax < w) c->cmax = w;
                if (c->cmin < w && false) c->cmin = w;
            }
        }
        if (c->cmax < c->cmin) c->cmax = c->cmin;
    }
    for (int i = 0; i < G->ncell; i++) {
        struct tcell *c = &G->cells[i];
        if (c->cs != 1) continue;
        if (c->cmin > G->cmin[c->c]) G->cmin[c->c] = c->cmin;
        if (c->cmax > G->cmax[c->c]) G->cmax[c->c] = c->cmax;
        if (c->fixed > G->cfixed[c->c]) G->cfixed[c->c] = c->fixed;
        if (c->pct > G->cpct[c->c]) G->cpct[c->c] = c->pct;
    }
    for (int span = 2; span <= n; span++) {
        for (int i = 0; i < G->ncell; i++) {
            struct tcell *c = &G->cells[i];
            if (c->cs != span) continue;
            int inner = (span - 1) * G->sp_h;
            long long smin = inner, smax = inner;
            for (int k = c->c; k < c->c + span && k < n; k++) { smin += G->cmin[k]; smax += G->cmax[k]; }
            if (smin < c->cmin) {
                int extra = (int)(c->cmin - smin);
                // spread in proportion to the max widths
                long long tot = 0; for (int k = c->c; k < c->c + span && k < n; k++) tot += G->cmax[k] ? G->cmax[k] : 1;
                int given = 0;
                for (int k = c->c; k < c->c + span && k < n; k++) {
                    int add = (int)((long long)extra * (G->cmax[k] ? G->cmax[k] : 1) / tot);
                    G->cmin[k] += add; given += add;
                }
                G->cmin[c->c + span - 1 < n ? c->c + span - 1 : n - 1] += extra - given;
            }
            if (smax < c->cmax) {
                int extra = (int)(c->cmax - smax);
                int each = extra / span, rem = extra - each * span;
                for (int k = c->c; k < c->c + span && k < n; k++) { G->cmax[k] += each + (rem > 0 ? 1 : 0); if (rem > 0) rem--; }
            }
        }
    }
    for (int k = 0; k < n; k++) if (G->cmax[k] < G->cmin[k]) G->cmax[k] = G->cmin[k];
}

void intr_table(struct lay *L, struct box *b, int *mn, int *mx) {
    struct tgrid G;
    grid_build(L, b, &G);
    grid_columns(L, &G, 0);
    int smin = 0, smax = 0;
    for (int k = 0; k < G.ncols; k++) {
        int mnk = G.cmin[k];
        int mxk = G.cmax[k];
        if (G.cfixed[k] && G.cfixed[k] > mxk) mxk = G.cfixed[k];
        smin += mnk; smax += mxk;
    }
    int sp = (G.ncols + 1) * G.sp_h;
    *mn = smin + sp; *mx = smax + sp;
    // percent columns: the table must be wide enough that they come out right
    {
        long long pct_total = 0; int pct_cols = 0;
        for (int k = 0; k < G.ncols; k++) if (G.cpct[k]) { pct_total += G.cpct[k]; pct_cols++; }
        if (pct_cols && pct_total > 0 && pct_total < 10000) {
            // width such that every percent column gets at least its max
            long long need = 0;
            for (int k = 0; k < G.ncols; k++) if (G.cpct[k]) { long long w = (long long)G.cmax[k] * 10000 / G.cpct[k]; if (w > need) need = w; }
            if (need > *mx - sp) *mx = (int)need + sp;
        }
    }
    // captions widen it
    for (int i = 0; i < G.ncap; i++) { lay_intrinsic(L, G.caps[i]); if (G.caps[i]->min_content > *mn) *mn = G.caps[i]->min_content; }
    grid_free(&G);
}

void lay_shift_content(struct box *b, int dy) {
    if (!dy) return;
    for (struct box *c = b->first; c; c = c->next) c->y += dy;
    for (int i = 0; i < b->frag_count; i++) b->frags[i].y += dy;
    for (int i = 0; i < b->line_count; i++) b->lines[i].y += dy;
    for (struct box *a = b->abs_first; a; a = a->abs_next) a->y += dy * 0;
}

void lay_table(struct lay *L, struct box *b, const struct lin *in, int content_w) {
    struct style *ts = b->style;
    struct tgrid G;
    grid_build(L, b, &G);
    grid_columns(L, &G, content_w);
    int n = G.ncols;
    int sp_total = (n + 1) * G.sp_h;
    int avail = content_w - sp_total;
    if (avail < 0) avail = 0;
    // widths
    long long smin = 0;
    for (int k = 0; k < n; k++) smin += G.cmin[k];
    int *want = calloc((size_t)n, sizeof(int));
    long long swant = 0;
    bool fixed_layout = ts->table_layout_fixed && ts->width.kind == L_VAL;
    if (fixed_layout) {
        // widths from the first row, the rest share what is left
        int *fw = calloc((size_t)n, sizeof(int));
        int unset = 0; long long used = 0;
        for (int i = 0; i < G.ncell; i++) {
            struct tcell *c = &G.cells[i];
            if (c->r != 0) continue;
            int w = c->fixed ? c->fixed : (c->pct ? (int)((long long)avail * c->pct / 10000) : 0);
            if (w) for (int k = c->c; k < c->c + c->cs; k++) fw[k] = w / c->cs;
        }
        for (int k = 0; k < n; k++) { if (fw[k]) used += fw[k]; else unset++; }
        for (int k = 0; k < n; k++) {
            want[k] = fw[k] ? fw[k] : (unset ? (int)((avail - used) / unset) : 0);
            if (want[k] < 0) want[k] = 0;
            G.cw[k] = want[k];
        }
        free(fw);
    } else {
        for (int k = 0; k < n; k++) {
            int w;
            if (G.cpct[k]) w = (int)((long long)avail * G.cpct[k] / 10000);
            else if (G.cfixed[k]) w = G.cfixed[k];
            else w = G.cmax[k];
            if (w < G.cmin[k]) w = G.cmin[k];
            want[k] = w; swant += w;
        }
        if (avail <= smin) {
            for (int k = 0; k < n; k++) G.cw[k] = G.cmin[k];
        } else if (swant <= avail) {
            // spread what's left over the columns that can take it
            long long extra = avail - swant;
            long long tot = 0; int cnt = 0;
            for (int k = 0; k < n; k++) if (!G.cfixed[k] && !G.cpct[k]) { tot += want[k] ? want[k] : 1; cnt++; }
            bool use_all = cnt == 0;
            if (use_all) { for (int k = 0; k < n; k++) tot += want[k] ? want[k] : 1; }
            long long given = 0;
            int last = -1;
            for (int k = 0; k < n; k++) {
                G.cw[k] = want[k];
                if (use_all || (!G.cfixed[k] && !G.cpct[k])) {
                    int add = (int)(extra * (want[k] ? want[k] : 1) / (tot ? tot : 1));
                    G.cw[k] += add; given += add; last = k;
                }
            }
            if (last >= 0) G.cw[last] += (int)(extra - given);
        } else {
            long long free_sp = avail - smin;
            long long grow_tot = 0;
            for (int k = 0; k < n; k++) grow_tot += want[k] - G.cmin[k];
            for (int k = 0; k < n; k++) {
                int g = grow_tot ? (int)((long long)(want[k] - G.cmin[k]) * free_sp / grow_tot) : 0;
                G.cw[k] = G.cmin[k] + g;
            }
            long long sum = 0; for (int k = 0; k < n; k++) sum += G.cw[k];
            if (sum < avail && n) G.cw[n - 1] += (int)(avail - sum);
        }
    }
    free(want);
    int x = G.sp_h;
    long long total_w = sp_total;
    for (int k = 0; k < n; k++) { G.cx[k] = x; x += G.cw[k] + G.sp_h; total_w += G.cw[k]; }
    G.cx[n] = x;
    int grid_w = (int)total_w;
    // a table wider than asked: it grows
    int final_content_w = grid_w > content_w ? grid_w : content_w;
    if (grid_w < content_w && ts->width.kind == L_AUTO) final_content_w = grid_w;

    // ---- lay out the cells ----
    for (int i = 0; i < G.ncell; i++) {
        struct tcell *c = &G.cells[i];
        int w = 0;
        for (int k = c->c; k < c->c + c->cs && k < n; k++) w += G.cw[k];
        w += (c->cs - 1) * G.sp_h;
        c->w = w;
        struct lin ci; memset(&ci, 0, sizeof(ci));
        ci.cb_w = w; ci.cb_h = -1; ci.avail_w = w; ci.forced_w = w; ci.forced_h = -1;
        c->b->abs_first = c->b->abs_last = NULL;
        lay_box(L, c->b, &ci);
        c->h = c->b->h;
        int bl = lay_baseline(c->b);
        c->base = bl >= 0 ? bl : -1;
    }
    // row heights
    for (int r = 0; r < G.nrow; r++) {
        struct trow *row = &G.rows[r];
        int h = 0;
        int max_base = 0;
        for (int i = 0; i < G.ncell; i++) {
            struct tcell *c = &G.cells[i];
            if (c->r != r || c->rs != 1) continue;
            if (c->b->style->vertical_align == VA_BASELINE || c->b->style->vertical_align == VA_TEXT_TOP) {
                int bl = c->base >= 0 ? c->base : 0;
                if (bl > max_base) max_base = bl;
            }
        }
        for (int i = 0; i < G.ncell; i++) {
            struct tcell *c = &G.cells[i];
            if (c->r != r || c->rs != 1) continue;
            int ch = c->h;
            if (c->b->style->vertical_align == VA_BASELINE) ch += max_base - (c->base >= 0 ? c->base : 0);
            if (c->b->style->height.kind == L_VAL && !c->b->style->height.pct && !c->b->style->height.expr) {
                int hh = c->b->style->height.px;
                if (c->b->style->box_sizing == 0) hh += c->b->pad.top + c->b->pad.bottom + c->b->border.top + c->b->border.bottom;
                if (hh > ch) ch = hh;
            }
            if (ch > h) h = ch;
        }
        if (row->b->style->height.kind == L_VAL && !row->b->style->height.pct) { if (row->b->style->height.px > h) h = row->b->style->height.px; }
        row->h = h;
        row->hint = max_base;
    }
    // cells with rowspan
    for (int i = 0; i < G.ncell; i++) {
        struct tcell *c = &G.cells[i];
        if (c->rs <= 1) continue;
        long long sum = (long long)(c->rs - 1) * G.sp_v;
        for (int r = c->r; r < c->r + c->rs; r++) sum += G.rows[r].h;
        if (sum < c->h) G.rows[c->r + c->rs - 1].h += (int)(c->h - sum);
    }
    // vertical positions
    int y = G.sp_v;
    int cap_top = 0, cap_bottom = 0;
    // table height asked for
    for (int r = 0; r < G.nrow; r++) { G.rows[r].y = y; y += G.rows[r].h + G.sp_v; }
    int grid_h = G.nrow ? y : 0;
    if (!G.nrow) grid_h = G.sp_v * 2;
    {
        const struct len *hl = &ts->height;
        int extra_v = b->pad.top + b->pad.bottom + b->border.top + b->border.bottom;
        int want_h = -1;
        if (in->forced_h >= 0) want_h = in->forced_h - extra_v;
        else if (hl->kind == L_VAL && !((hl->pct || hl->expr) && in->cb_h < 0)) { want_h = len_to_px(hl, in->cb_h, 0); if (ts->box_sizing == 0) {} else want_h -= extra_v; }
        // captions take part of it
        if (want_h > grid_h && G.nrow) {
            int extra = want_h - grid_h;
            int each = extra / G.nrow, rem = extra - each * G.nrow;
            for (int r = 0; r < G.nrow; r++) { G.rows[r].h += each + (r == G.nrow - 1 ? rem : 0); }
            y = G.sp_v;
            for (int r = 0; r < G.nrow; r++) { G.rows[r].y = y; y += G.rows[r].h + G.sp_v; }
            grid_h = y;
        }
    }
    // captions
    int cw_for_caps = final_content_w;
    for (int i = 0; i < G.ncap; i++) {
        struct box *cap = G.caps[i];
        struct lin ci; memset(&ci, 0, sizeof(ci));
        ci.cb_w = cw_for_caps; ci.cb_h = -1; ci.avail_w = cw_for_caps; ci.forced_w = -1; ci.forced_h = -1;
        lay_box(L, cap, &ci);
        int ch = cap->h + cap->margin.top + cap->margin.bottom;
        cap_top += ch;       // captions go on top (caption-side: bottom is not supported)
    }
    int offx = b->border.left + b->pad.left, offy = b->border.top + b->pad.top;
    // place captions
    {
        int cy = 0;
        for (int i = 0; i < G.ncap; i++) {
            struct box *cap = G.caps[i];
            cap->x = offx + cap->margin.left; cap->y = offy + cy + cap->margin.top;
            cy += cap->h + cap->margin.top + cap->margin.bottom;
        }
    }
    // place groups, rows and cells
    for (int r = 0; r < G.nrow; r++) {
        struct trow *row = &G.rows[r];
        row->b->w = grid_w - 2 * G.sp_h; row->b->h = row->h;
        row->b->x = G.sp_h; row->b->y = row->y;
        if (row->group) {
            // the group's box covers its rows
            struct box *g = row->group;
            int top = row->y, bot = row->y + row->h;
            if (g->h == 0 && g->w == 0) { g->y = top; g->h = bot - top; g->w = grid_w - 2 * G.sp_h; g->x = G.sp_h; }
            else { if (row->y + row->h > g->y + g->h) g->h = row->y + row->h - g->y; }
        }
    }
    for (int r = 0; r < G.nrow; r++) {
        struct trow *row = &G.rows[r];
        if (row->group) { row->b->x = 0; row->b->y = row->y - row->group->y; }
        row->b->parent = row->group ? row->group : b;
    }
    for (int r = 0; r < G.nrow; r++) {
        struct trow *row = &G.rows[r];
        if (!row->group) { row->b->x += offx; row->b->y += offy + cap_top; }
    }
    // groups are in table coordinates already: offset them
    for (int r = 0; r < G.nrow; r++) {
        struct trow *row = &G.rows[r];
        if (row->group && !row->group->bfc) { }
    }
    {
        // each group once
        struct box *seen[64]; int ns = 0;
        for (int r = 0; r < G.nrow; r++) {
            struct box *g = G.rows[r].group;
            if (!g) continue;
            bool done = false;
            for (int k = 0; k < ns; k++) if (seen[k] == g) done = true;
            if (done) continue;
            if (ns < 64) seen[ns++] = g;
            g->x += offx; g->y += offy + cap_top;
        }
    }
    for (int i = 0; i < G.ncell; i++) {
        struct tcell *c = &G.cells[i];
        long long hsum = (long long)(c->rs - 1) * G.sp_v;
        for (int r = c->r; r < c->r + c->rs; r++) hsum += G.rows[r].h;
        int cell_h = (int)hsum;
        int dy = 0;
        struct style *cs = c->b->style;
        int free_v = cell_h - c->b->h;
        if (cs->vertical_align == VA_MIDDLE) dy = free_v / 2;
        else if (cs->vertical_align == VA_BOTTOM) dy = free_v;
        else if (cs->vertical_align == VA_BASELINE && c->rs == 1) dy = G.rows[c->r].hint - (c->base >= 0 ? c->base : 0);
        if (dy < 0) dy = 0;
        c->b->h = cell_h;
        lay_shift_content(c->b, dy);
        struct trow *row = &G.rows[c->r];
        c->b->parent = row->b;
        c->b->x = G.cx[c->c] - G.sp_h;
        c->b->y = 0;
        
        c->b->x = G.cx[c->c] - G.sp_h;       // relative to the row (which starts at sp_h)
    }
    // sizes of the table
    int content_h = cap_top + grid_h + 0;
    b->content_h = content_h;
    b->content_w = final_content_w;
    b->w = final_content_w + b->pad.left + b->pad.right + b->border.left + b->border.right;
    (void)cap_bottom;
    grid_free(&G);
}
