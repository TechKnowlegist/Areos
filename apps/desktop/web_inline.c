// web_inline.c - lines of text: breaking, aligning, and placing inline boxes in them.

#include "web_lay.h"

#define INF 0x3fffffff

// ---- Preparing text: collapsing spaces and changing case ----

static int up_cp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return (int)cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return (int)cp - 32;
    if (cp >= 0x3B1 && cp <= 0x3C9 && cp != 0x3C2) return (int)cp - 32;
    if (cp >= 0x430 && cp <= 0x44F) return (int)cp - 32;
    if (cp >= 0x450 && cp <= 0x45F) return (int)cp - 80;
    return (int)cp;
}
static int low_cp(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return (int)cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return (int)cp + 32;
    if (cp >= 0x391 && cp <= 0x3A9) return (int)cp + 32;
    if (cp >= 0x410 && cp <= 0x42F) return (int)cp + 32;
    if (cp >= 0x400 && cp <= 0x40F) return (int)cp + 80;
    return (int)cp;
}

void text_prepare(struct page *p, struct box *t) {
    if (t->transformed) return;
    const struct style *s = t->style;
    const char *src = t->text;
    int len = t->text_len;
    char *out = w_alloc(&p->larena, (size_t)len * 2 + 8);
    int n = 0;
    int ws = s->white_space;
    bool collapse = ws == WS_NORMAL || ws == WS_NOWRAP || ws == WS_PRE_LINE;
    bool keep_nl = ws == WS_PRE || ws == WS_PRE_WRAP || ws == WS_PRE_LINE || ws == WS_BREAK_SPACES;
    bool prev_space = false;
    int col = 0;
    bool cap_next = true;
    const char *q = src, *end = src + len;
    while (q < end) {
        const char *before = q;
        uint32_t cp = w_utf8_next(&q, end);
        if (cp == 0xAD) continue;                      // soft hyphen
        if (cp == '\r') { if (q < end && *q == '\n') continue; cp = '\n'; }
        if (collapse) {
            if (cp == '\n' && keep_nl) {
                // pre-line: spaces before a newline go
                while (n > 0 && out[n - 1] == ' ') n--;
                out[n++] = '\n'; prev_space = true; continue;
            }
            if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\f') {
                if (prev_space) continue;
                out[n++] = ' '; prev_space = true;
                continue;
            }
            prev_space = false;
        } else {
            if (cp == '\t') {
                int ts = s->tab_size > 0 ? s->tab_size : 8;
                int add = ts - (col % ts);
                for (int i = 0; i < add; i++) out[n++] = ' ';
                col += add; continue;
            }
            if (cp == '\n') { out[n++] = '\n'; col = 0; continue;}
            if (cp == '\f') cp = ' ';
        }
        col++;
        int mapped = (int)cp;
        if (s->text_transform == TT_UPPER) mapped = up_cp(cp);
        else if (s->text_transform == TT_LOWER) mapped = low_cp(cp);
        else if (s->text_transform == TT_CAPITALIZE) {
            if (cap_next && w_is_alpha((int)cp)) { mapped = up_cp(cp); }
            else if (cap_next && cp >= 0x80) mapped = up_cp(cp);
            cap_next = (cp == ' ' || cp == '\n' || cp == '\t' || cp == '-' || cp == '(' || cp == '"');
        }
        if (mapped == (int)cp && cp < 0x80) out[n++] = (char)cp;
        else if (mapped == (int)cp) { int k = (int)(q - before); memcpy(out + n, before, (size_t)k); n += k; }
        else n += w_utf8_put((uint32_t)mapped, out + n);
        if (n > len * 2 + 4) break;
    }
    out[n] = '\0';
    t->transformed = out;
    t->tlen = n;
}

// ---- Measuring ----

static int piece_w64(const struct style *s, int font, const char *t, int len) {
    int w = wfont_text_width(font, s->font_size, t, len);
    if (s->letter_spacing || s->word_spacing) {
        const char *q = t, *e = t + len;
        while (q < e) {
            uint32_t cp = w_utf8_next(&q, e);
            w += s->letter_spacing;
            if (cp == ' ') w += s->word_spacing;
        }
    }
    return w;
}

static bool is_cjk(uint32_t cp) {
    return (cp >= 0x2E80 && cp < 0xA000) || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x3000 && cp <= 0x303F)
        || (cp >= 0x20000 && cp < 0x30000);
}

// Where does the piece starting at i end? (A piece is a stretch that can't be broken.)
static int piece_end(const struct style *s, const char *t, int i, int len, bool newline) {
    const char *q = t + i, *e = t + len;
    bool break_all = s->word_break == WB_BREAK_ALL;
    bool keep_all = s->word_break == WB_KEEP_ALL;
    int j = i;
    uint32_t prev = 0;
    while (q < e) {
        const char *before = q;
        uint32_t cp = w_utf8_next(&q, e);
        if (cp == ' ' || (cp == '\n' && newline)) { return (int)(before - t); }
        if (j > i) {
            if (break_all) return (int)(before - t);
            if (!keep_all && (is_cjk(cp) || is_cjk(prev))) return (int)(before - t);
            if (prev == '-' && w_is_alnum((int)cp) && j - i >= 2) return (int)(before - t);
            if (prev == 0x200B) return (int)(before - t);
        }
        prev = cp;
        j = (int)(q - t);
    }
    return j;
}

// ---- Line building state ----

struct fitem {
    uint8_t kind;           // 0 text, 1 atomic box
    uint8_t va;             // 0 baseline-ish, 1 top, 2 bottom
    struct box *box;
    int off, len;
    int x, w;
    int above, below, shift;
    int ascent, cellh;
    int focusable;
    int base_off;           // atomic: border box top to baseline
};

struct decor {
    struct box *box;
    int x0, x1, shift;
    bool first, last;
    int asc, h;             // y of the frag top relative to baseline = -asc
};

struct open_box {
    struct box *box;
    int start_x;
    int shift;
    bool first_piece;
    bool has_content;
    bool decor;
    int a, d;               // font ascent / descent of this box
    int xh;                 // half x height of this box's font
};

struct istate {
    struct lay *L;
    struct box *blk;
    struct fctx *f;
    int cx, cy, cw;
    int ind;                // text-indent
    struct fitem *items; int nitems, capitems;
    struct decor *decors; int ndec, capdec;
    struct open_box open[48]; int nopen;
    struct frag *frags; int nfrags, capfrags;
    struct line *lines; int nlines, caplines;
    int line_y, line_left, line_right, x;
    bool started, first_line, ended_space;
    int strut_a, strut_d;
    int cur_shift;
    struct box *pend[16]; int npend;
    int offx, offy;         // border + padding of the block, to put floats in the parent's frame
    int ch_def;
    bool failed;
};

static void *grow(void *p, int *cap, int need, size_t esz) {
    if (need <= *cap) return p;
    int nc = *cap ? *cap * 2 : 32;
    while (nc < need) nc *= 2;
    void *np = realloc(p, esz * (size_t)nc);
    if (!np) return p;
    *cap = nc;
    return np;
}

static bool has_visible_decor(const struct style *s) {
    if ((s->bg_color >> 24) || s->bg_count || s->shadow_count) return true;
    for (int i = 0; i < 4; i++) if (s->border_width[i] > 0 && s->border_style[i] != BS_NONE) return true;
    return false;
}

static void shift_line(struct istate *S, int dx) {
    for (int i = 0; i < S->nitems; i++) S->items[i].x += dx;
    for (int i = 0; i < S->ndec; i++) { S->decors[i].x0 += dx; S->decors[i].x1 += dx; }
    for (int i = 0; i < S->nopen; i++) S->open[i].start_x += dx;
    S->x += dx;
}

static void place_pending(struct istate *S);

static void begin_line(struct istate *S) {
    place_pending(S);
    S->started = true;
    S->nitems = 0; S->ndec = 0;
    int band = S->strut_a + S->strut_d;
    int l, r;
    fl_edges(S->f, S->cy + S->line_y, band, S->cx, S->cx + S->cw, &l, &r);
    S->line_left = l - S->cx; S->line_right = r - S->cx;
    S->x = S->line_left + (S->first_line ? S->ind : 0);
    S->ended_space = true;
    for (int i = 0; i < S->nopen; i++) { S->open[i].start_x = S->x; S->open[i].has_content = false; }
}

static void ensure_line(struct istate *S) { if (!S->started) begin_line(S); }

static void place_pending(struct istate *S) {
    // floats that did not fit beside the previous line
    struct lay *L = S->L;
    int n = S->npend;
    S->npend = 0;
    for (int i = 0; i < n; i++) {
        struct box *c = S->pend[i];
        int ow = c->w + c->margin.left + c->margin.right, oh = c->h + c->margin.top + c->margin.bottom;
        bool left = c->style->floating == FL_LEFT;
        int y = S->cy + S->line_y;
        for (int k = 0; k < S->f->count; k++) if (S->f->items[k].y > y) y = S->f->items[k].y;
        if (c->style->clear) y = fl_clear_y(S->f, y, c->style->clear);
        for (int guard = 0;; guard++) {
            int l, r;
            fl_edges(S->f, y, oh, S->cx, S->cx + S->cw, &l, &r);
            if (r - l >= ow || guard > 100) {
                int x = left ? l : r - ow;
                fl_add(S->f, x, y, ow, oh, left);
                c->x = (x - S->cx) + c->margin.left + S->offx; c->y = (y - S->cy) + c->margin.top + S->offy;
                break;
            }
            int nx = fl_next_edge(S->f, y);
            if (nx >= INF) { int x = left ? S->cx : S->cx + S->cw - ow; fl_add(S->f, x, y, ow, oh, left); c->x = (x - S->cx) + c->margin.left + S->offx; c->y = (y - S->cy) + c->margin.top + S->offy; break; }
            y = nx;
        }
    }
    (void)L;
}

// ---- Finishing a line ----

static struct frag *new_frag(struct istate *S) {
    S->frags = grow(S->frags, &S->capfrags, S->nfrags + 1, sizeof(struct frag));
    if (S->capfrags < S->nfrags + 1) { S->failed = true; return &S->frags[0]; }
    struct frag *f = &S->frags[S->nfrags++];
    memset(f, 0, sizeof(*f));
    f->focusable = -1;
    return f;
}

static void finish_line(struct istate *S, bool forced) {
    if (!S->started) {
        if (!forced) return;
        begin_line(S);
    }
    // close the pieces of open inline boxes
    // (they continue on the next line)
    // trailing collapsible space does not count
    if (S->nitems > 0) {
        struct fitem *last = &S->items[S->nitems - 1];
        if (last->kind == 0 && last->len > 0) {
            const char *t = last->box->transformed;
            int ws = last->box->style->white_space;
            if (t[last->off + last->len - 1] == ' ' && ws != WS_PRE && ws != WS_BREAK_SPACES) {
                int sw = (piece_w64(last->box->style, font_for(last->box->style), " ", 1) + 32) / 64;
                last->len--; last->w -= sw;
                if (last->w < 0) last->w = 0;
            }
        }
    }
    for (int i = 0; i < S->nopen; i++) {
        struct open_box *o = &S->open[i];
        if (o->decor && o->has_content) {
            S->decors = grow(S->decors, &S->capdec, S->ndec + 1, sizeof(struct decor));
            struct decor *d = &S->decors[S->ndec++];
            d->box = o->box; d->x0 = o->start_x; d->x1 = S->nitems ? S->items[S->nitems - 1].x + S->items[S->nitems - 1].w : S->x;
            if (d->x1 < d->x0) d->x1 = d->x0;
            d->shift = o->shift; d->first = o->first_piece; d->last = false;
            const struct style *st = o->box->style;
            d->asc = o->a + st->padding[0].px * 0 + o->box->pad.top + o->box->border.top;
            d->h = o->a + o->d + o->box->pad.top + o->box->pad.bottom + o->box->border.top + o->box->border.bottom;
        }
        o->first_piece = false;
    }
    int ba = S->strut_a, bb = S->strut_d;
    int th = 0;
    for (int i = 0; i < S->nitems; i++) {
        struct fitem *it = &S->items[i];
        if (it->va == 0) {
            if (it->above + it->shift > ba) ba = it->above + it->shift;
            if (it->below - it->shift > bb) bb = it->below - it->shift;
        } else if (it->above + it->below > th) th = it->above + it->below;
    }
    for (int i = 0; i < S->ndec; i++) {
        struct decor *d = &S->decors[i];
        // decorations may stick out above/below the line box: they don't change its height
        (void)d;
    }
    if (ba + bb < th) bb = th - ba;
    int H = ba + bb;
    // alignment
    int used_end = S->line_left + (S->first_line ? S->ind : 0);
    if (S->nitems) used_end = S->items[S->nitems - 1].x + S->items[S->nitems - 1].w;
    int avail = S->line_right - S->line_left;
    int dx = 0;
    int ta = S->blk->style->text_align;
    if (ta == TA_CENTER) dx = (avail - (used_end - S->line_left)) / 2;
    else if (ta == TA_RIGHT || ta == TA_END) dx = avail - (used_end - S->line_left);
    if (dx < 0) dx = 0;
    int first_frag = S->nfrags;
    int y0 = S->line_y;
    // decor frags first (painted under the text)
    for (int i = 0; i < S->ndec; i++) {
        struct decor *d = &S->decors[i];
        struct frag *fr = new_frag(S);
        fr->type = FRAG_DECOR; fr->box = d->box;
        fr->x = d->x0 + dx; fr->w = d->x1 - d->x0;
        fr->y = y0 + ba - d->shift - d->asc; fr->h = d->h;
        fr->baseline = d->asc;
        fr->first_piece = d->first; fr->last_piece = d->last;
        fr->focusable = d->box->focusable;
        fr->line = S->nlines;
    }
    for (int i = 0; i < S->nitems; i++) {
        struct fitem *it = &S->items[i];
        struct frag *fr = new_frag(S);
        fr->line = S->nlines;
        fr->x = it->x + dx;
        fr->focusable = it->focusable;
        fr->box = it->box;
        if (it->kind == 0) {
            fr->type = FRAG_TEXT;
            fr->text_off = it->off; fr->text_len = it->len;
            fr->w = it->w; fr->h = it->cellh;
            fr->baseline = it->ascent;
            fr->y = y0 + (it->va == 1 ? it->ascent - it->ascent + (it->above - it->ascent) * 0 : 0);
            int base;
            if (it->va == 1) base = it->above;                 // top aligned
            else if (it->va == 2) base = H - it->below;        // bottom aligned
            else base = ba - it->shift;
            fr->y = y0 + base - it->ascent;
        } else {
            fr->type = FRAG_BOX;
            struct box *bx = it->box;
            int base;
            if (it->va == 1) base = it->above;
            else if (it->va == 2) base = H - it->below;
            else base = ba - it->shift;
            // base is where the box's baseline falls; the box top is base_off above that
            fr->y = y0 + base - it->base_off;
            fr->w = bx->w; fr->h = bx->h;
            // the box itself: relative to the block's content origin
            fr->x = fr->x + bx->margin.left;
            bx->x = fr->x + S->offx;
            bx->y = fr->y + S->offy;
            fr->baseline = it->base_off;
        }
    }
    S->lines = grow(S->lines, &S->caplines, S->nlines + 1, sizeof(struct line));
    struct line *ln = &S->lines[S->nlines++];
    ln->y = y0; ln->h = H; ln->first_frag = first_frag; ln->frag_count = S->nfrags - first_frag;
    ln->baseline = ba; ln->left = S->line_left; ln->right = S->line_right;
    S->line_y += H;
    S->started = false;
    S->first_line = false;
    S->nitems = 0; S->ndec = 0;
}

// ---- Adding things to a line ----

// Make room for something `w` wide. Breaks the line if it doesn't fit.
static void make_room(struct istate *S, int w, int sp) {
    ensure_line(S);
    for (int guard = 0; guard < 400; guard++) {
        if (S->x + sp + w <= S->line_right) return;
        if (S->nitems > 0) {
            finish_line(S, false);
            begin_line(S);
            continue;
        }
        // empty line and still not enough room
        bool narrowed = S->line_left > 0 || S->line_right < S->cw;
        if (!narrowed) return;
        int nx = fl_next_edge(S->f, S->cy + S->line_y);
        if (nx >= INF) return;
        S->line_y = nx - S->cy;
        S->started = false;
        begin_line(S);
    }
}

static void mark_content(struct istate *S) { for (int i = 0; i < S->nopen; i++) S->open[i].has_content = true; }

static void hard_break(struct istate *S) {
    ensure_line(S);
    finish_line(S, true);
}

static void text_metrics(const struct style *s, int *a, int *d, int *above, int *below, int *cellh) {
    int font = font_for(s);
    int asc, desc, gap;
    wfont_vmetrics(font, s->font_size, &asc, &desc, &gap);
    int lh = box_font_height(s, NULL, NULL);
    int lead = lh - (asc + desc);
    int top = lead / 2;
    if (lead < 0) top = -((-lead) / 2);
    *a = asc; *d = desc;
    *above = asc + top;
    *below = lh - *above;
    *cellh = asc + desc;
}

static void place_text(struct istate *S, struct box *t) {
    text_prepare(S->L->p, t);
    const struct style *s = t->style;
    const char *text = t->transformed;
    int len = t->tlen;
    if (!len) return;
    int font = font_for(s);
    int ws = s->white_space;
    bool preserve = ws == WS_PRE || ws == WS_PRE_WRAP || ws == WS_BREAK_SPACES;
    bool newline = ws == WS_PRE || ws == WS_PRE_WRAP || ws == WS_PRE_LINE || ws == WS_BREAK_SPACES;
    bool can_wrap = ws == WS_NORMAL || ws == WS_PRE_WRAP || ws == WS_PRE_LINE || ws == WS_BREAK_SPACES;
    int a, d, above, below, cellh;
    text_metrics(s, &a, &d, &above, &below, &cellh);
    int space64 = piece_w64(s, font, " ", 1);
    int i = 0;
    while (i < len) {
        if (text[i] == '\n' && newline) {
            ensure_line(S);
            hard_break(S);
            i++;
            continue;
        }
        int sp_start = -1, sp_w = 0;
        if (text[i] == ' ') {
            int j = i;
            while (j < len && text[j] == ' ' && (preserve || j == i)) j++;
            ensure_line(S);
            if (!preserve && S->ended_space) { i = j; continue; }
            sp_start = i;
            sp_w = ((j - i) * space64 + 32) / 64;
            if (s->letter_spacing || s->word_spacing) sp_w = (piece_w64(s, font, text + i, j - i) + 32) / 64;
            i = j;
            if (i >= len || (text[i] == '\n' && newline)) {
                // a space at the end of the text: keep it with the previous piece
                bool ext = false;
                if (S->nitems > 0) {
                    struct fitem *last = &S->items[S->nitems - 1];
                    if (last->kind == 0 && last->box == t && last->off + last->len == sp_start) {
                        last->len += i - sp_start; last->w += sp_w; ext = true;
                    }
                }
                if (!ext && S->nitems > 0 && preserve) {
                    // keep the space as its own piece
                }
                S->x += sp_w;
                if (!preserve) S->ended_space = true;
                continue;
            }
        }
        int je = piece_end(s, text, i, len, newline);
        if (je <= i) je = i + 1;
        int w = (piece_w64(s, font, text + i, je - i) + 32) / 64;
        ensure_line(S);
        if (can_wrap) {
            if (S->x + sp_w + w > S->line_right && S->nitems > 0) {
                finish_line(S, false);
                begin_line(S);
                sp_w = 0; sp_start = -1;
            }
            if (S->nitems == 0 && S->x + w > S->line_right) {
                make_room(S, w, 0);
            }
            // a word longer than the line: break it up
            bool breakable = s->word_break == WB_BREAK_WORD || s->word_break == WB_BREAK_ALL || ws == WS_BREAK_SPACES;
            if (S->x + sp_w + w > S->line_right && breakable) {
                // fit as many characters as possible
                const char *q = text + i, *e = text + je;
                int fit_end = i;
                while (q < e) {
                    const char *bef = q;
                    w_utf8_next(&q, e);
                    int ww = (piece_w64(s, font, text + i, (int)(q - text) - i) + 32) / 64;
                    if (S->x + sp_w + ww > S->line_right && fit_end > i) { q = bef; break; }
                    fit_end = (int)(q - text);
                    if (S->x + sp_w + ww > S->line_right) break;
                }
                if (fit_end > i && fit_end < je) {
                    je = fit_end;
                    w = (piece_w64(s, font, text + i, je - i) + 32) / 64;
                    // the rest goes on following lines: handled by the loop (we leave i to be advanced below)
                    // place this part, then force a line break
                    goto place_it;
                }
            }
        }
    place_it:;
        {
            int start = sp_start >= 0 ? sp_start : i;
            int add_w = sp_w + w;
            struct fitem *last = S->nitems ? &S->items[S->nitems - 1] : NULL;
            if (last && last->kind == 0 && last->box == t && last->off + last->len == start) {
                last->len = je - last->off; last->w += add_w;
            } else {
                S->items = grow(S->items, &S->capitems, S->nitems + 1, sizeof(struct fitem));
                struct fitem *it = &S->items[S->nitems++];
                memset(it, 0, sizeof(*it));
                it->kind = 0; it->box = t; it->off = start; it->len = je - start;
                it->x = S->x; it->w = add_w;
                it->above = above; it->below = below; it->ascent = a; it->cellh = cellh;
                it->shift = S->cur_shift;
                it->focusable = t->focusable;
                it->va = 0;
            }
            S->x += add_w;
            S->ended_space = false;
            mark_content(S);
        }
        i = je;
        // we split a long word: continue on a new line
        if (i < len && text[i] != ' ' && text[i] != '\n' && can_wrap && S->x > S->line_right - 1) {
            finish_line(S, false);
            begin_line(S);
        }
    }
}

// Where does the baseline of an atomic inline fall, measured from its border box top?
static int atom_baseline(const struct box *b) {
    int bl = lay_baseline(b);
    if (bl >= 0) return bl;
    return b->h;
}

static void place_atom(struct istate *S, struct box *c) {
    struct lin in;
    memset(&in, 0, sizeof(in));
    in.cb_w = S->cw; in.cb_h = S->ch_def; in.avail_w = S->cw; in.forced_w = -1; in.forced_h = -1; in.shrink = true;
    lay_box(S->L, c, &in);
    int w = c->w + c->margin.left + c->margin.right;
    ensure_line(S);
    make_room(S, w, 0);
    S->items = grow(S->items, &S->capitems, S->nitems + 1, sizeof(struct fitem));
    struct fitem *it = &S->items[S->nitems++];
    memset(it, 0, sizeof(*it));
    it->kind = 1; it->box = c;
    it->x = S->x; it->w = w;
    int bo = atom_baseline(c);
    it->base_off = bo;
    it->above = bo + c->margin.top;
    it->below = c->h - bo + c->margin.bottom;
    const struct style *s = c->style;
    it->va = 0;
    it->shift = S->cur_shift;
    switch (s->vertical_align) {
    case VA_TOP: it->va = 1; break;
    case VA_BOTTOM: it->va = 2; break;
    case VA_MIDDLE: {
        // middle of the box on the baseline plus half the x-height of the parent
        int xh = S->nopen ? S->open[S->nopen - 1].xh : S->strut_a / 4;
        int mid = (c->h) / 2;
        it->shift += xh - mid + bo - 0 - (bo - mid) * 0;
        // box midpoint sits at xh above the baseline: shift = xh - (bo - h/2)
        it->shift = S->cur_shift + xh - (bo - mid);
        break;
    }
    case VA_SUB: it->shift -= S->strut_a / 5 + 1; break;
    case VA_SUPER: it->shift += S->strut_a / 3 + 1; break;
    case VA_TEXT_TOP: { int pa = S->nopen ? S->open[S->nopen - 1].a : S->strut_a; it->shift += pa - (bo + c->margin.top); break; }
    case VA_TEXT_BOTTOM: { int pd = S->nopen ? S->open[S->nopen - 1].d : S->strut_d; it->shift += -(pd - (c->h - bo + c->margin.bottom)); break; }
    case VA_LEN: {
        int lh = box_font_height(s, NULL, NULL);
        int v = len_to_px(&s->vertical_align_len, lh, 0);
        it->shift += v; break;
    }
    }
    it->focusable = c->focusable;
    S->x += w;
    S->ended_space = false;
    mark_content(S);
}

static void place_float_here(struct istate *S, struct box *c) {
    struct lin in;
    memset(&in, 0, sizeof(in));
    in.cb_w = S->cw; in.cb_h = S->ch_def; in.avail_w = S->cw; in.forced_w = -1; in.forced_h = -1; in.shrink = true;
    lay_box(S->L, c, &in);
    int ow = c->w + c->margin.left + c->margin.right, oh = c->h + c->margin.top + c->margin.bottom;
    bool left = c->style->floating == FL_LEFT;
    ensure_line(S);
    int y = S->cy + S->line_y;
    bool later = c->style->clear != 0;
    for (int k = 0; k < S->f->count; k++) if (S->f->items[k].y > y) later = true;
    if (!later) {
        int l, r;
        fl_edges(S->f, y, oh, S->cx, S->cx + S->cw, &l, &r);
        int used = S->x - S->line_left;
        if (S->nitems == 0) used = 0;
        if ((r - l) - (S->line_right - S->line_left) + (S->line_right - S->line_left) >= ow + used && r - l >= ow + used) {
            // fits next to what is already on the line
            int x = left ? l : r - ow;
            fl_add(S->f, x, y, ow, oh, left);
            c->x = (x - S->cx) + c->margin.left + S->offx; c->y = (y - S->cy) + c->margin.top + S->offy;
            if (left) { int ndx = (l + ow - S->cx) - S->line_left; S->line_left += ndx; shift_line(S, ndx); }
            else S->line_right = (x - S->cx);
            return;
        }
    }
    if (S->npend < 16) S->pend[S->npend++] = c;
}

static void open_inline(struct istate *S, struct box *ib) {
    ensure_line(S);
    const struct style *s = ib->style;
    lay_edges(ib, S->cw);
    int font_a, font_d, g;
    wfont_vmetrics(font_for(s), s->font_size, &font_a, &font_d, &g);
    int left = ib->margin.left + ib->border.left + ib->pad.left;
    // wrap before an inline box that does not fit? only the text inside decides
    if (S->nopen < 48) {
        struct open_box *o = &S->open[S->nopen++];
        o->box = ib; o->first_piece = true; o->has_content = left > 0 || ib->pad.right + ib->border.right > 0;
        o->start_x = S->x + ib->margin.left;
        o->decor = has_visible_decor(s);
        o->a = font_a; o->d = font_d; o->xh = font_a / 3 + 0;
        o->shift = S->cur_shift;
        // vertical-align of an inline box moves everything inside it
        int shift = 0;
        int pa = S->nopen >= 2 ? S->open[S->nopen - 2].a : S->strut_a;
        int pd = S->nopen >= 2 ? S->open[S->nopen - 2].d : S->strut_d;
        int pxh = S->nopen >= 2 ? S->open[S->nopen - 2].xh : S->strut_a / 3;
        switch (s->vertical_align) {
        case VA_SUB: shift = -(s->font_size / 5 + 1); break;
        case VA_SUPER: shift = s->font_size / 3 + 1; break;
        case VA_MIDDLE: shift = pxh - (font_a - font_d) / 2; break;
        case VA_TEXT_TOP: shift = pa - font_a; break;
        case VA_TEXT_BOTTOM: shift = -(pd - font_d); break;
        case VA_LEN: { int lh = box_font_height(s, NULL, NULL); shift = len_to_px(&s->vertical_align_len, lh, 0); break; }
        }
        S->cur_shift += shift;
        o->shift = S->cur_shift;
    }
    S->x += left;
}

static void close_inline(struct istate *S, struct box *ib) {
    if (S->nopen == 0 || S->open[S->nopen - 1].box != ib) return;
    struct open_box *o = &S->open[S->nopen - 1];
    ensure_line(S);
    int right = ib->pad.right + ib->border.right;
    S->x += right;
    if (o->decor && o->has_content) {
        S->decors = grow(S->decors, &S->capdec, S->ndec + 1, sizeof(struct decor));
        struct decor *d = &S->decors[S->ndec++];
        d->box = ib; d->x0 = o->start_x; d->x1 = S->x; d->shift = o->shift;
        d->first = o->first_piece; d->last = true;
        d->asc = o->a + ib->pad.top + ib->border.top;
        d->h = o->a + o->d + ib->pad.top + ib->pad.bottom + ib->border.top + ib->border.bottom;
    }
    S->x += ib->margin.right;
    // undo the shift
    const struct style *s = ib->style;
    int shift = o->shift - (S->nopen >= 2 ? S->open[S->nopen - 2].shift : 0);
    if (S->nopen < 2) shift = o->shift;
    (void)s;
    S->nopen--;
    S->cur_shift = S->nopen ? S->open[S->nopen - 1].shift : 0;
    (void)shift;
}

static void walk(struct istate *S, struct box *parent, int depth) {
    for (struct box *c = parent->first; c; c = c->next) {
        if (c->out_of_flow) {
            if (c->is_float) { place_float_here(S, c); }
            else {
                ensure_line(S);
                struct box *host = S->blk;
                c->sx = S->x; c->sy = S->line_y;
                // register with the container
                extern void web_register_abs(struct lay *L, struct box *a, struct box *host, int sx, int sy);
                web_register_abs(S->L, c, host, S->x, S->line_y);
            }
            continue;
        }
        switch (c->type) {
        case BT_TEXT: place_text(S, c); break;
        case BT_BR:
            ensure_line(S);
            hard_break(S);
            break;
        case BT_INLINE:
            open_inline(S, c);
            if (depth < 300) walk(S, c, depth + 1);
            close_inline(S, c);
            break;
        default:
            place_atom(S, c);
            break;
        }
    }
}

void lay_inline(struct lay *L, struct box *b, const struct lin *in, struct fctx *f, int content_w, int cx, int cy, int *content_h) {
    struct istate *S = calloc(1, sizeof(struct istate));
    if (!S) { *content_h = 0; return; }
    S->L = L; S->blk = b; S->f = f; S->cx = cx; S->cy = cy; S->cw = content_w;
    S->offx = b->border.left + b->pad.left; S->offy = b->border.top + b->pad.top;
    S->ch_def = -1;
    S->first_line = true;
    const struct style *s = b->style;
    S->ind = len_to_px(&s->text_indent, content_w, 0);
    int above, below, cellh, a, d;
    text_metrics(s, &a, &d, &above, &below, &cellh);
    S->strut_a = above; S->strut_d = below;
    walk(S, b, 0);
    if (S->started || S->nitems) finish_line(S, false);
    // floats that never found a place
    if (S->npend) { S->started = false; place_pending(S); }
    // copy results into the page's memory
    struct page *p = L->p;
    if (S->nlines) {
        b->lines = w_alloc(&p->larena, sizeof(struct line) * (size_t)S->nlines);
        memcpy(b->lines, S->lines, sizeof(struct line) * (size_t)S->nlines);
        b->line_count = S->nlines;
        b->frags = w_alloc(&p->larena, sizeof(struct frag) * (size_t)(S->nfrags ? S->nfrags : 1));
        if (S->nfrags) memcpy(b->frags, S->frags, sizeof(struct frag) * (size_t)S->nfrags);
        b->frag_count = S->nfrags;
    }
    *content_h = S->line_y;
    free(S->items); free(S->decors); free(S->frags); free(S->lines); free(S);
    (void)in;
}

// ---- Intrinsic widths ----

struct mstate {
    struct lay *L;
    int cur_max, best_max, best_min, run_min;
    bool prev_space;
    bool first_text;
};

static void m_break_run(struct mstate *M) { if (M->run_min > M->best_min) M->best_min = M->run_min; M->run_min = 0; }

static void m_text(struct mstate *M, struct box *t) {
    text_prepare(M->L->p, t);
    const struct style *s = t->style;
    const char *text = t->transformed;
    int len = t->tlen;
    int font = font_for(s);
    int ws = s->white_space;
    bool nowrap = ws == WS_NOWRAP || ws == WS_PRE;
    bool preserve = ws == WS_PRE || ws == WS_PRE_WRAP || ws == WS_BREAK_SPACES;
    bool newline = ws == WS_PRE || ws == WS_PRE_WRAP || ws == WS_PRE_LINE || ws == WS_BREAK_SPACES;
    int i = 0;
    while (i < len) {
        if (text[i] == '\n' && newline) {
            m_break_run(M);
            if (M->cur_max > M->best_max) M->best_max = M->cur_max;
            M->cur_max = 0; M->prev_space = true; i++; continue;
        }
        if (text[i] == ' ') {
            int j = i;
            while (j < len && text[j] == ' ' && (preserve || j == i)) j++;
            if (!preserve && M->prev_space) { i = j; continue; }
            int w = (piece_w64(s, font, text + i, j - i) + 32) / 64;
            M->cur_max += w;
            if (nowrap) M->run_min += w; else m_break_run(M);
            if (!preserve) M->prev_space = true;
            i = j;
            continue;
        }
        int je = piece_end(s, text, i, len, newline);
        if (je <= i) je = i + 1;
        int w = (piece_w64(s, font, text + i, je - i) + 32) / 64;
        M->cur_max += w;
        M->run_min += w;
        M->prev_space = false;
        i = je;
        // pieces that end at a break opportunity (hyphen, CJK) end the run
        if (i < len && text[i] != ' ' && !(text[i] == '\n' && newline) && !nowrap) m_break_run(M);
    }
}

static void m_walk(struct mstate *M, struct box *parent, int depth) {
    for (struct box *c = parent->first; c; c = c->next) {
        if (c->out_of_flow && !c->is_float) continue;
        if (c->is_float) {
            lay_intrinsic(M->L, c);
            int om = c->margin.left + c->margin.right;
            M->cur_max += c->max_content + om;
            if (c->min_content + om > M->best_min) M->best_min = c->min_content + om;
            continue;
        }
        switch (c->type) {
        case BT_TEXT: m_text(M, c); break;
        case BT_BR:
            m_break_run(M);
            if (M->cur_max > M->best_max) M->best_max = M->cur_max;
            M->cur_max = 0; M->prev_space = true;
            break;
        case BT_INLINE: {
            lay_edges(c, 0);
            int l = c->margin.left + c->border.left + c->pad.left, r = c->margin.right + c->border.right + c->pad.right;
            M->cur_max += l; M->run_min += l;
            if (depth < 300) m_walk(M, c, depth + 1);
            M->cur_max += r; M->run_min += r;
            break;
        }
        default: {
            lay_intrinsic(M->L, c);
            lay_edges(c, 0);
            int om = c->margin.left + c->margin.right;
            m_break_run(M);
            if (c->min_content + om > M->best_min) M->best_min = c->min_content + om;
            M->cur_max += c->max_content + om;
            M->prev_space = false;
            break;
        }
        }
    }
}

void intr_inline(struct lay *L, struct box *b, int *mn, int *mx) {
    struct mstate M;
    memset(&M, 0, sizeof(M));
    M.L = L; M.prev_space = true;
    if (b->type == BT_TEXT) {
        m_text(&M, b);
    } else {
        m_walk(&M, b, 0);
    }
    m_break_run(&M);
    if (M.cur_max > M.best_max) M.best_max = M.cur_max;
    // trailing collapsed space isn't counted by browsers; ignore
    *mn = M.best_min;
    *mx = M.best_max;
    int ws = b->style->white_space;
    if (ws == WS_NOWRAP || ws == WS_PRE) { *mn = *mx; }
    if (b->style->text_indent.kind == L_VAL && !b->style->text_indent.pct && b->type != BT_TEXT) { *mx += b->style->text_indent.px; if (*mx < 0) *mx = 0; }
}
