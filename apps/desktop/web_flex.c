// web_flex.c - flexible boxes (display: flex).

#include "web_lay.h"

#define INF 0x3fffffff

void web_register_abs(struct lay *L, struct box *a, struct box *host, int sx, int sy);

struct fi {
    struct box *b;
    int order, idx;
    int base, hyp, mn, mx;
    int m_s, m_e;           // main-axis margins
    bool am_s, am_e;        // ... which are auto
    int c_s, c_e;           // cross-axis margins
    bool ac_s, ac_e;
    int size;               // final main size (border box)
    int cross;              // final cross size (border box)
    bool frozen;
    int grow, shrink;
    int line;
    int pos_m, pos_c;
    int tw, th;             // size from a trial layout (column direction), or -1
    int align;
};

static int len_or(const struct len *l, int base, int def) { return len_to_px(l, base, def); }

static int ex_h(const struct box *b) { return b->pad.left + b->pad.right + b->border.left + b->border.right; }
static int ex_v(const struct box *b) { return b->pad.top + b->pad.bottom + b->border.top + b->border.bottom; }

static void items_sort(struct fi *a, int n) {
    // stable insertion sort by order
    for (int i = 1; i < n; i++) {
        struct fi t = a[i];
        int j = i;
        while (j > 0 && a[j - 1].order > t.order) { a[j] = a[j - 1]; j--; }
        a[j] = t;
    }
}

static void lay_item(struct lay *L, struct fi *it, int cb_w, int cb_h, int fw, int fh, bool shrink, int avail) {
    struct lin in;
    memset(&in, 0, sizeof(in));
    in.cb_w = cb_w; in.cb_h = cb_h; in.avail_w = avail; in.forced_w = fw; in.forced_h = fh; in.shrink = shrink;
    it->b->abs_first = it->b->abs_last = NULL;
    lay_box(L, it->b, &in);
}

static bool needs_relayout(const struct box *b) {
    int d = b->style->display;
    return d == D_FLEX || d == D_GRID || d == D_TABLE || d == D_INLINE_FLEX || d == D_INLINE_GRID || d == D_INLINE_TABLE || b->abs_first != NULL || b->type == BT_REPLACED;
}

void lay_flex(struct lay *L, struct box *b, const struct lin *in, int content_w, int *content_h) {
    struct style *s = b->style;
    bool row = s->flex_dir == FD_ROW || s->flex_dir == FD_ROW_REVERSE;
    bool rev = s->flex_dir == FD_ROW_REVERSE || s->flex_dir == FD_COLUMN_REVERSE;
    bool wrap = s->flex_wrap != 0, wrap_rev = s->flex_wrap == 2;
    int extra_v = ex_v(b);
    // the container's own height, if known
    int def_h = -1;
    if (in->forced_h >= 0) def_h = in->forced_h - extra_v;
    else if (s->height.kind == L_VAL && !((s->height.pct || s->height.expr) && in->cb_h < 0)) {
        int h = len_to_px(&s->height, in->cb_h, 0);
        if (s->box_sizing == 0) def_h = h; else def_h = h - extra_v;
    }
    if (def_h < -1) def_h = 0;
    int min_h = 0;
    if (s->min_height.kind == L_VAL && !((s->min_height.pct || s->min_height.expr) && in->cb_h < 0)) {
        min_h = len_to_px(&s->min_height, in->cb_h, 0);
        if (s->box_sizing != 0) min_h -= extra_v;
        if (min_h < 0) min_h = 0;
    }
    int gap_m = row ? len_or(&s->column_gap, content_w, 0) : len_or(&s->row_gap, def_h, 0);
    int gap_c = row ? len_or(&s->row_gap, def_h, 0) : len_or(&s->column_gap, content_w, 0);
    if (gap_m < 0) gap_m = 0; if (gap_c < 0) gap_c = 0;

    int n = 0;
    for (struct box *c = b->first; c; c = c->next) n++;
    struct fi *it = calloc((size_t)(n ? n : 1), sizeof(struct fi));
    int cnt = 0;
    int offx = b->border.left + b->pad.left, offy = b->border.top + b->pad.top;
    for (struct box *c = b->first; c; c = c->next) {
        if (c->out_of_flow) { web_register_abs(L, c, b, 0, 0); continue; }
        struct fi *f = &it[cnt];
        f->b = c; f->idx = cnt; f->order = c->style->order; cnt++;
    }
    items_sort(it, cnt);
    int main_avail = row ? content_w : (def_h >= 0 ? def_h : (min_h > 0 ? min_h : -1));
    if (!row && def_h < 0 && min_h > 0) main_avail = min_h;

    // ---- sizes of each item ----
    for (int i = 0; i < cnt; i++) {
        struct fi *f = &it[i];
        struct box *c = f->b;
        struct style *cs = c->style;
        lay_edges(c, content_w);
        f->tw = f->th = -1;
        f->grow = cs->flex_grow; f->shrink = cs->flex_shrink;
        if (row) {
            f->m_s = c->margin.left; f->m_e = c->margin.right; f->am_s = cs->margin[3].kind == L_AUTO; f->am_e = cs->margin[1].kind == L_AUTO;
            f->c_s = c->margin.top; f->c_e = c->margin.bottom; f->ac_s = cs->margin[0].kind == L_AUTO; f->ac_e = cs->margin[2].kind == L_AUTO;
        } else {
            f->m_s = c->margin.top; f->m_e = c->margin.bottom; f->am_s = cs->margin[0].kind == L_AUTO; f->am_e = cs->margin[2].kind == L_AUTO;
            f->c_s = c->margin.left; f->c_e = c->margin.right; f->ac_s = cs->margin[3].kind == L_AUTO; f->ac_e = cs->margin[1].kind == L_AUTO;
        }
        f->align = cs->align_self != FA_AUTO ? cs->align_self : s->align_items;
        if (f->align == FA_NORMAL || f->align == FA_AUTO) f->align = FA_STRETCH;
        int ex = row ? ex_h(c) : ex_v(c);
        int bs = cs->box_sizing;
        int bextra = row ? (c->border.left + c->border.right) : (c->border.top + c->border.bottom);
        const struct len *msize = row ? &cs->width : &cs->height;
        const struct len *mmin = row ? &cs->min_width : &cs->min_height;
        const struct len *mmax = row ? &cs->max_width : &cs->max_height;
        int mbase = row ? content_w : main_avail;
        // convert a length for the box-sizing property
        #define TO_BORDER_BOX(v) ((bs == 0) ? (v) + ex : ((bs == 2) ? (v) + bextra : (v)))
        bool size_def = msize->kind == L_VAL && !((msize->pct || msize->expr) && mbase < 0);
        // flex-basis
        const struct len *fb = &cs->flex_basis;
        int base;
        if (fb->kind == L_VAL && !((fb->pct || fb->expr) && mbase < 0)) base = TO_BORDER_BOX(len_to_px(fb, mbase, 0));
        else if (fb->kind == L_MAX_CONTENT || fb->kind == L_FIT_CONTENT || (fb->kind == L_AUTO && !size_def) || fb->kind == L_MIN_CONTENT) {
            if (row) { lay_intrinsic(L, c); base = fb->kind == L_MIN_CONTENT ? c->min_content : c->max_content; }
            else {
                // content height at the cross size it will have
                int cw = content_w - f->c_s - f->c_e;
                bool stretch = f->align == FA_STRETCH && cs->width.kind == L_AUTO;
                struct fi tmp = *f;
                if (stretch) lay_item(L, &tmp, content_w, -1, cw, -1, false, content_w);
                else lay_item(L, &tmp, content_w, -1, -1, -1, true, content_w);
                f->tw = c->w; f->th = c->h;
                base = c->h;
            }
        } else {
            base = TO_BORDER_BOX(len_to_px(msize, mbase, 0));
        }
        if (base < ex) base = ex;
        f->base = base;
        // min / max
        int mn, mx = INF;
        if (mmin->kind == L_VAL && !((mmin->pct || mmin->expr) && mbase < 0)) mn = TO_BORDER_BOX(len_to_px(mmin, mbase, 0));
        else if (mmin->kind == L_AUTO || mmin->kind == L_MIN_CONTENT) {
            // automatic minimum: the content, unless the box scrolls or has a size
            bool scroller = row ? cs->overflow_x != OV_VISIBLE : cs->overflow_y != OV_VISIBLE;
            if (scroller) mn = ex;
            else if (row) {
                lay_intrinsic(L, c);
                mn = c->min_content;
                if (size_def) { int sv = TO_BORDER_BOX(len_to_px(msize, mbase, 0)); if (sv < mn) mn = sv; }
            } else {
                if (f->th >= 0) mn = f->th;
                else if (size_def) mn = TO_BORDER_BOX(len_to_px(msize, mbase, 0));
                else mn = ex;
                if (size_def) { int sv = TO_BORDER_BOX(len_to_px(msize, mbase, 0)); if (sv < mn) mn = sv; }
            }
        } else mn = ex;
        if (mmax->kind == L_VAL && !((mmax->pct || mmax->expr) && mbase < 0)) mx = TO_BORDER_BOX(len_to_px(mmax, mbase, 0));
        if (mn < ex) mn = ex;
        if (mx < mn) mx = mn;
        f->mn = mn; f->mx = mx;
        int hyp = base;
        if (hyp > mx) hyp = mx;
        if (hyp < mn) hyp = mn;
        f->hyp = hyp;
        #undef TO_BORDER_BOX
    }

    // ---- lines ----
    int nlines = 0;
    int *line_start = calloc((size_t)(cnt + 2), sizeof(int));
    {
        int cur = 0; int sum = 0;
        line_start[0] = 0; nlines = 1;
        for (int i = 0; i < cnt; i++) {
            int outer = it[i].hyp + it[i].m_s + it[i].m_e;
            if (wrap && main_avail >= 0 && i > line_start[nlines - 1] && sum + gap_m + outer > main_avail) {
                line_start[nlines++] = i;
                sum = 0;
            }
            sum += (i > line_start[nlines - 1] ? gap_m : 0) + outer;
            it[i].line = nlines - 1;
            (void)cur;
        }
        line_start[nlines] = cnt;
    }

    int *line_cross = calloc((size_t)(nlines + 1), sizeof(int));
    int *line_main_used = calloc((size_t)(nlines + 1), sizeof(int));
    int *line_base_cross = calloc((size_t)(nlines + 1), sizeof(int));
    int max_main_total = 0;
    // ---- resolve flexible lengths, lay out, find cross sizes ----
    for (int ln = 0; ln < nlines; ln++) {
        int a0 = line_start[ln], a1 = line_start[ln + 1];
        int count = a1 - a0;
        int gaps = (count - 1) * gap_m;
        if (gaps < 0) gaps = 0;
        long long hyp_sum = gaps;
        for (int i = a0; i < a1; i++) hyp_sum += it[i].hyp + it[i].m_s + it[i].m_e;
        bool definite = main_avail >= 0;
        for (int i = a0; i < a1; i++) { it[i].size = it[i].hyp; it[i].frozen = false; }
        if (definite) {
            bool use_grow = hyp_sum < main_avail;
            long long sum_grow = 0, sum_shr = 0;
            for (int i = a0; i < a1; i++) {
                struct fi *f = &it[i];
                bool freeze = (use_grow ? f->grow == 0 : f->shrink == 0) || (use_grow ? f->base > f->hyp : f->base < f->hyp);
                if (freeze) { f->frozen = true; f->size = f->hyp; }
            }
            for (int iter = 0; iter < 12; iter++) {
                long long used = gaps;
                long long gsum = 0, ssum = 0;
                for (int i = a0; i < a1; i++) {
                    struct fi *f = &it[i];
                    used += f->m_s + f->m_e;
                    if (f->frozen) used += f->size; else { used += f->base; gsum += f->grow; ssum += (long long)f->shrink * f->base; }
                }
                bool any = false;
                for (int i = a0; i < a1; i++) if (!it[i].frozen) any = true;
                if (!any) break;
                long long freesp = main_avail - used;
                // sizes
                long long total_viol = 0;
                int tent[1024];     // tentative sizes (first 1024 items of a line; more get no flexing)
                for (int i = a0; i < a1; i++) {
                    struct fi *f = &it[i];
                    if (f->frozen || i - a0 >= 1024) continue;
                    long long sz = f->base;
                    if (freesp > 0 && use_grow && gsum > 0) {
                        long long share = freesp;
                        if (gsum < 1000) share = freesp * gsum / 1000;
                        sz = f->base + share * f->grow / gsum;
                    } else if (freesp < 0 && !use_grow && ssum > 0) {
                        sz = f->base + freesp * ((long long)f->shrink * f->base) / ssum;
                    }
                    long long clamped = sz;
                    if (clamped < f->mn) clamped = f->mn;
                    if (clamped > f->mx) clamped = f->mx;
                    if (clamped < 0) clamped = 0;
                    total_viol += clamped - sz;
                    tent[i - a0] = (int)clamped;
                    f->size = (int)clamped;
                }
                // freeze violators
                bool froze = false;
                for (int i = a0; i < a1; i++) {
                    struct fi *f = &it[i];
                    if (f->frozen) continue;
                    if (i - a0 >= 1024) { f->frozen = true; continue; }
                    long long unclamped;
                    {
                        long long sz = f->base;
                        if (freesp > 0 && use_grow && gsum > 0) { long long share = freesp; if (gsum < 1000) share = freesp * gsum / 1000; sz = f->base + share * f->grow / gsum; }
                        else if (freesp < 0 && !use_grow && ssum > 0) sz = f->base + freesp * ((long long)f->shrink * f->base) / ssum;
                        unclamped = sz;
                    }
                    bool min_v = tent[i - a0] > unclamped, max_v = tent[i - a0] < unclamped;
                    if (total_viol == 0 || (total_viol > 0 && min_v) || (total_viol < 0 && max_v)) { f->frozen = true; froze = true; }
                }
                if (!froze) { for (int i = a0; i < a1; i++) it[i].frozen = true; }
            }
            (void)sum_grow; (void)sum_shr;
        }
        // free space left for auto margins and justify-content
        long long used = gaps;
        for (int i = a0; i < a1; i++) used += it[i].size + it[i].m_s + it[i].m_e;
        line_main_used[ln] = (int)used;
        if (used > max_main_total) max_main_total = (int)used;

        // lay out each item and find its cross size
        int lc = 0;
        for (int i = a0; i < a1; i++) {
            struct fi *f = &it[i];
            struct box *c = f->b;
            if (row) {
                int fh = -1;
                lay_item(L, f, content_w, -1, f->size, fh, false, content_w);
                f->cross = c->h;
            } else {
                int cw = content_w - f->c_s - f->c_e;
                bool stretch = f->align == FA_STRETCH && c->style->width.kind == L_AUTO && !f->ac_s && !f->ac_e;
                if (f->th == f->size && f->tw >= 0 && f->tw <= content_w) {
                    // the trial layout still stands (when the same width applies)
                    if (stretch && f->tw == cw) { f->cross = f->tw; }
                    else if (!stretch) f->cross = f->tw;
                    else { lay_item(L, f, content_w, -1, cw, f->size, false, content_w); f->cross = c->w; }
                } else {
                    if (stretch) lay_item(L, f, content_w, -1, cw, f->size, false, content_w);
                    else lay_item(L, f, content_w, -1, -1, f->size, true, content_w);
                    f->cross = c->w;
                }
            }
            int outer_c = f->cross + f->c_s + f->c_e;
            if (outer_c > lc) lc = outer_c;
        }
        line_cross[ln] = lc;
        line_base_cross[ln] = lc;
    }

    // ---- cross size of the container ----
    int cross_total = 0;
    for (int ln = 0; ln < nlines; ln++) cross_total += line_cross[ln];
    cross_total += (nlines > 1 ? (nlines - 1) * gap_c : 0);
    int cross_avail = row ? (def_h >= 0 ? def_h : -1) : content_w;
    if (row && def_h < 0 && min_h > cross_total) cross_avail = min_h;
    if (row && def_h < 0 && cross_avail < 0 && s->max_height.kind == L_VAL && !((s->max_height.pct) && in->cb_h < 0)) {
        int mh = len_to_px(&s->max_height, in->cb_h, INF);
        if (s->box_sizing != 0) mh -= extra_v;
        if (cross_total > mh) cross_avail = mh;
    }
    int container_cross = cross_avail >= 0 ? cross_avail : cross_total;
    if (!row) container_cross = content_w;

    // multiple lines or single line: line cross sizes
    if (nlines == 1 && cross_avail >= 0) {
        line_cross[0] = container_cross;          // a lone line fills the container
    } else if (nlines > 1 && cross_avail >= 0 && container_cross > cross_total) {
        // align-content
        int ac = s->align_content;
        int free_c = container_cross - cross_total;
        if (ac == FA_STRETCH || ac == FA_NORMAL || ac == FA_AUTO) {
            for (int ln = 0; ln < nlines; ln++) line_cross[ln] += free_c / nlines;
            line_cross[nlines - 1] += free_c - (free_c / nlines) * nlines;
        }
    }
    // ---- position and align ----
    int cross_pos = 0;
    int extra_lead = 0, extra_gap = 0;
    if (nlines > 1 && cross_avail >= 0 && container_cross > cross_total) {
        int ac = s->align_content;
        int free_c = container_cross - cross_total;
        if (ac == FA_CENTER) extra_lead = free_c / 2;
        else if (ac == FA_END) extra_lead = free_c;
        else if (ac == FA_SPACE_BETWEEN) extra_gap = free_c / (nlines - 1);
        else if (ac == FA_SPACE_AROUND) { extra_gap = free_c / nlines; extra_lead = extra_gap / 2; }
        else if (ac == FA_SPACE_EVENLY) { extra_gap = free_c / (nlines + 1); extra_lead = extra_gap; }
    } else if (nlines == 1 && cross_avail < 0) { }
    cross_pos = extra_lead;
    int total_cross_used = 0;
    for (int k = 0; k < nlines; k++) {
        int ln = wrap_rev ? nlines - 1 - k : k;
        int a0 = line_start[ln], a1 = line_start[ln + 1];
        int lcross = line_cross[ln];
        // main axis distribution
        int count = a1 - a0;
        long long freesp = (row ? content_w : (main_avail >= 0 ? main_avail : line_main_used[ln])) - line_main_used[ln];
        int auto_cnt = 0;
        for (int i = a0; i < a1; i++) { auto_cnt += (it[i].am_s ? 1 : 0) + (it[i].am_e ? 1 : 0); }
        int lead = 0, between = gap_m;
        int auto_each = 0, auto_rem = 0;
        if (freesp < 0) freesp = 0;
        if (auto_cnt > 0 && freesp > 0) { auto_each = (int)(freesp / auto_cnt); auto_rem = (int)(freesp - (long long)auto_each * auto_cnt); freesp = 0; }
        else {
            int jc = s->justify_content;
            bool reverse_main = rev;
            if (jc == FA_CENTER) lead = (int)(freesp / 2);
            else if (jc == FA_END) lead = (int)freesp;
            else if (jc == FA_SPACE_BETWEEN && count > 1) between = gap_m + (int)(freesp / (count - 1));
            else if (jc == FA_SPACE_AROUND && count > 0) { int e = (int)(freesp / count); lead = e / 2; between = gap_m + e; }
            else if (jc == FA_SPACE_EVENLY && count > 0) { int e = (int)(freesp / (count + 1)); lead = e; between = gap_m + e; }
            (void)reverse_main;
            // with reversed direction, "start" is the far end
            if (rev && (jc == FA_NORMAL || jc == FA_START || jc == FA_AUTO || jc == FA_STRETCH)) { lead = (int)freesp; }
            else if (rev && jc == FA_END) { lead = 0; }
            else if (rev && jc == FA_CENTER) { }
        }
        // positions along the main axis, in visual order
        int pos = lead;
        // order of placement: normal: first to last; reversed: positions run from the end
        int ordered[2048];
        int oc = 0;
        for (int i = a0; i < a1 && oc < 2048; i++) ordered[oc++] = i;
        if (rev) { for (int x = 0; x < oc / 2; x++) { int t = ordered[x]; ordered[x] = ordered[oc - 1 - x]; ordered[oc - 1 - x] = t; } }
        // baseline alignment
        int max_base = 0;
        bool have_base = false;
        for (int i = a0; i < a1; i++) {
            struct fi *f = &it[i];
            if (row && f->align == FA_BASELINE) {
                int bl = lay_baseline(f->b);
                if (bl < 0) bl = f->b->h;
                int top = f->c_s + bl;
                if (top > max_base) max_base = top;
                have_base = true;
            }
        }
        int cur_auto_rem = auto_rem;
        for (int q = 0; q < oc; q++) {
            struct fi *f = &it[ordered[q]];
            struct box *c = f->b;
            int ms = f->m_s, me = f->m_e;
            if (f->am_s) { ms = auto_each + (cur_auto_rem > 0 ? 1 : 0); if (cur_auto_rem > 0) cur_auto_rem--; }
            if (f->am_e) { me = auto_each + (cur_auto_rem > 0 ? 1 : 0); if (cur_auto_rem > 0) cur_auto_rem--; }
            if (rev && q == 0 && auto_cnt == 0) { }
            f->pos_m = pos + ms;
            pos += ms + f->size + me + (q + 1 < oc ? between : 0);
            // cross axis
            int outer_c = f->cross + f->c_s + f->c_e;
            int free_c = lcross - outer_c;
            int cs_ = f->c_s;
            int al = f->align;
            if (free_c > 0 && (f->ac_s || f->ac_e)) {
                if (f->ac_s && f->ac_e) cs_ = free_c / 2;
                else if (f->ac_s) cs_ = free_c + f->c_s * 0;
                else cs_ = f->c_s;
                if (f->ac_s && !f->ac_e) cs_ = free_c;
                al = FA_START;
                f->pos_c = cross_pos + cs_;
            } else {
                switch (al) {
                case FA_END: f->pos_c = cross_pos + free_c + f->c_s; break;
                case FA_CENTER: f->pos_c = cross_pos + free_c / 2 + f->c_s; break;
                case FA_BASELINE:
                    if (row && have_base) { int bl = lay_baseline(c); if (bl < 0) bl = c->h; f->pos_c = cross_pos + max_base - bl; }
                    else f->pos_c = cross_pos + f->c_s;
                    break;
                case FA_STRETCH: {
                    bool auto_c = row ? c->style->height.kind == L_AUTO : c->style->width.kind == L_AUTO;
                    f->pos_c = cross_pos + f->c_s;
                    if (auto_c && !f->ac_s && !f->ac_e) {
                        int target = lcross - f->c_s - f->c_e;
                        if (target > f->cross) {
                            if (row) {
                                if (needs_relayout(c)) lay_item(L, f, content_w, -1, f->size, target, false, content_w);
                                else c->h = target;
                                f->cross = target;
                            } else { f->cross = target; }
                        }
                    }
                    break;
                }
                default: f->pos_c = cross_pos + f->c_s; break;
                }
            }
        }
        cross_pos += lcross + gap_c + extra_gap;
        total_cross_used += lcross;
    }
    // place the boxes
    for (int i = 0; i < cnt; i++) {
        struct fi *f = &it[i];
        struct box *c = f->b;
        if (row) { c->x = offx + f->pos_m; c->y = offy + f->pos_c; }
        else { c->x = offx + f->pos_c; c->y = offy + f->pos_m; }
        // a column item's stretched width: lay out again if its width changed after the trial
        if (!row) {
            if (c->w != f->cross && f->align == FA_STRETCH && c->style->width.kind == L_AUTO) { lay_item(L, f, content_w, -1, f->cross, f->size, false, content_w); c->x = offx + f->pos_c; c->y = offy + f->pos_m; }
        }
    }
    // height of the container's content
    int ch;
    if (row) {
        ch = cross_avail >= 0 && (nlines == 1 || container_cross > cross_total) ? container_cross : cross_total;
        if (cross_avail >= 0 && nlines > 1 && container_cross <= cross_total) ch = cross_total;
        if (def_h >= 0) ch = def_h;
    } else {
        ch = max_main_total;
        if (main_avail >= 0 && def_h >= 0) ch = def_h;
        else if (min_h > ch) ch = min_h;
    }
    *content_h = ch;
    // out-of-flow children get their static position at the start of the content box (set by register)
    free(it); free(line_start); free(line_cross); free(line_main_used); free(line_base_cross);
}

void intr_flex(struct lay *L, struct box *b, int *mn, int *mx) {
    struct style *s = b->style;
    bool row = s->flex_dir == FD_ROW || s->flex_dir == FD_ROW_REVERSE;
    bool wrap = s->flex_wrap != 0;
    int gap = row ? len_to_px(&s->column_gap, 0, 0) : len_to_px(&s->row_gap, 0, 0);
    int sum_mn = 0, sum_mx = 0, max_mn = 0, max_mx = 0, n = 0;
    for (struct box *c = b->first; c; c = c->next) {
        if (c->out_of_flow) continue;
        lay_intrinsic(L, c);
        int om = 0;
        {
            struct style *cs = c->style;
            int l = cs->margin[3].kind == L_VAL && !cs->margin[3].pct ? cs->margin[3].px : 0;
            int r = cs->margin[1].kind == L_VAL && !cs->margin[1].pct ? cs->margin[1].px : 0;
            om = l + r;
        }
        int cmn = c->min_content + om, cmx = c->max_content + om;
        // flex-basis (in px) adds weight to max-content
        const struct len *fb = &c->style->flex_basis;
        if (row && fb->kind == L_VAL && !fb->pct && !fb->expr && c->style->flex_grow == 0 && fb->px > cmx) cmx = fb->px + om;
        sum_mn += cmn; sum_mx += cmx; if (cmn > max_mn) max_mn = cmn; if (cmx > max_mx) max_mx = cmx;
        n++;
    }
    if (n > 1) { sum_mn += gap * (n - 1); sum_mx += gap * (n - 1); }
    if (row) { *mx = sum_mx; *mn = wrap ? max_mn : sum_mn; }
    else { *mx = max_mx; *mn = max_mn; }
}
