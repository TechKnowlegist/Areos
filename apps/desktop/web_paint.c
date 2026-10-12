// web_paint.c - drawing the laid-out page.
//
// Order of painting follows the CSS rules, simplified: inside a "stacking
// context" (the page, or anything positioned / see-through / z-indexed) we
// paint, in this order: its own background, boxes with a negative z-index,
// the backgrounds of block boxes, floats, the text and pictures, and last the
// positioned boxes (z-index 0 or more, lowest first).

#include "web_gfx.h"
#include "web_lay.h"

struct item {
    struct box *b;
    int ox, oy;
    int z, seq;
    struct clip *clips;
    int nclip;
};

struct ilist { struct item *v; int n, cap; };

struct pp {
    struct pctx g;
    struct page *p;
    int seq;
    int tree_seq;
    struct { struct box *b; int ox, oy; } cs[96];
    int ncs;
};

static void paint_atomic(struct pp *S, struct box *c, int cx, int cy, struct ilist *outer_fixed);
static void walk(struct pp *S, struct box *b, int bx, int by, int phase, struct ilist *L);

#define ACCENT 0xFF0A64D8u

// ---- helpers ----

static uint32_t cur(const struct style *s, uint32_t c) { return c == COLOR_CURRENT ? (s->color | 0xFF000000u) : c; }

static bool deferred(const struct box *c) {
    if (c->type != BT_BLOCK && c->type != BT_REPLACED) return false;
    const struct style *s = c->style;
    if (s->position != POS_STATIC) return true;
    if (s->opacity < 255) return true;
    if (s->has_z && c->parent && c->parent->style) {
        int d = c->parent->style->display;
        if (d == D_FLEX || d == D_GRID || d == D_INLINE_FLEX || d == D_INLINE_GRID) return true;
    }
    return false;
}

static bool clips_overflow(const struct box *c) {
    if (!c->node || c->type == BT_INLINE) return false;
    if (c->node->tag == T_HTML || c->node->tag == T_BODY) return false;
    return c->style->overflow_x != OV_VISIBLE || c->style->overflow_y != OV_VISIBLE;
}

static void radii_of(const struct box *c, int w, int h, int out[8]) {
    const struct style *s = c->style;
    for (int i = 0; i < 4; i++) {
        const struct len *l = &s->radius[i];
        int rx, ry;
        if (l->pct && !l->px && !l->expr) { rx = (int)((long long)w * l->pct / 10000); ry = (int)((long long)h * l->pct / 10000); }
        else { rx = ry = len_resolve(l, w); }
        out[i] = rx < 0 ? 0 : rx; out[i + 4] = ry < 0 ? 0 : ry;
    }
}

// The radii of the inside edge of the border (and padding too, if `with_pad`).
static void inner_radii(const int rad[8], const struct edges *e_border, const struct edges *e_pad, bool with_pad, int out[8]) {
    int bt = e_border->top, br = e_border->right, bb = e_border->bottom, bl = e_border->left;
    if (with_pad) { bt += e_pad->top; br += e_pad->right; bb += e_pad->bottom; bl += e_pad->left; }
    int h[4] = { bl, br, br, bl }, v[4] = { bt, bt, bb, bb };
    for (int i = 0; i < 4; i++) {
        int rx = rad[i] - h[i], ry = rad[i + 4] - v[i];
        if (rx < 0 || ry < 0) rx = ry = 0;
        out[i] = rx; out[i + 4] = ry;
    }
}

static void uniform(int out[8], int r) { for (int i = 0; i < 8; i++) out[i] = r; }

static void line(struct pp *S, int x0, int y0, int x1, int y1, int thick, uint32_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        gfx_fill(&S->g, x0 - thick / 2, y0 - thick / 2, thick, thick, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void ring(struct pp *S, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    uint32_t c1 = 0xFFFFFFFFu, c2 = ACCENT;
    // white line outside, blue line inside: visible on light and dark pages
    gfx_fill(&S->g, x - 3, y - 3, w + 6, 1, c1); gfx_fill(&S->g, x - 3, y + h + 2, w + 6, 1, c1);
    gfx_fill(&S->g, x - 3, y - 3, 1, h + 6, c1); gfx_fill(&S->g, x + w + 2, y - 3, 1, h + 6, c1);
    gfx_fill(&S->g, x - 2, y - 2, w + 4, 2, c2); gfx_fill(&S->g, x - 2, y + h, w + 4, 2, c2);
    gfx_fill(&S->g, x - 2, y - 2, 2, h + 4, c2); gfx_fill(&S->g, x + w, y - 2, 2, h + 4, c2);
}

// ---- Backgrounds ----

static void paint_bg_layer(struct pp *S, const struct box *c, const struct bg_layer *L, int ox, int oy,
                           int clx, int cly, int clw, int clh, const int clrad[8]) {
    const struct style *s = c->style;
    int w = c->w, h = c->h;
    // positioning area
    int ax, ay, aw, ah;
    if (L->origin == 1) { ax = ox; ay = oy; aw = w; ah = h; }
    else if (L->origin == 2) { ax = ox + c->border.left + c->pad.left; ay = oy + c->border.top + c->pad.top;
                               aw = w - c->border.left - c->border.right - c->pad.left - c->pad.right;
                               ah = h - c->border.top - c->border.bottom - c->pad.top - c->pad.bottom; }
    else { ax = ox + c->border.left; ay = oy + c->border.top; aw = w - c->border.left - c->border.right; ah = h - c->border.top - c->border.bottom; }
    if (aw <= 0 || ah <= 0) return;
    (void)s;
    struct wimage *img = NULL;
    int iw = aw, ih = ah;          // natural size
    bool have_ratio = false;
    if (L->url) {
        img = web_image_get(S->p, L->url);
        if (!img || img->state != IMG_READY) return;
        iw = img->natural_w > 0 ? img->natural_w : img->w; ih = img->natural_h > 0 ? img->natural_h : img->h;
        have_ratio = iw > 0 && ih > 0;
        if (iw <= 0 || ih <= 0) return;
    }
    // tile size
    int tw, th;
    switch (L->size_mode) {
    case BGS_COVER: {
        if (!have_ratio) { tw = aw; th = ah; break; }
        long long a = (long long)aw * ih, b = (long long)ah * iw;
        if (a > b) { tw = aw; th = (int)((long long)aw * ih / iw); } else { th = ah; tw = (int)((long long)ah * iw / ih); }
        if (tw < 1) tw = 1; if (th < 1) th = 1;
        break; }
    case BGS_CONTAIN: {
        if (!have_ratio) { tw = aw; th = ah; break; }
        long long a = (long long)aw * ih, b = (long long)ah * iw;
        if (a < b) { tw = aw; th = (int)((long long)aw * ih / iw); } else { th = ah; tw = (int)((long long)ah * iw / ih); }
        if (tw < 1) tw = 1; if (th < 1) th = 1;
        break; }
    case BGS_LEN: {
        bool wa = L->size_w.kind == L_AUTO, ha = L->size_h.kind == L_AUTO;
        tw = wa ? 0 : len_resolve(&L->size_w, aw); th = ha ? 0 : len_resolve(&L->size_h, ah);
        if (wa && ha) { tw = iw; th = ih; }
        else if (wa) tw = have_ratio ? (int)((long long)th * iw / ih) : aw;
        else if (ha) th = have_ratio ? (int)((long long)tw * ih / iw) : ah;
        break; }
    default: tw = L->url ? iw : aw; th = L->url ? ih : ah; break;
    }
    if (tw <= 0 || th <= 0) return;
    int px = len_resolve(&L->pos_x, aw - tw), py = len_resolve(&L->pos_y, ah - th);
    if (L->pos_x.pct && !L->pos_x.px && !L->pos_x.expr) px = (int)((long long)(aw - tw) * L->pos_x.pct / 10000);
    if (L->pos_y.pct && !L->pos_y.px && !L->pos_y.expr) py = (int)((long long)(ah - th) * L->pos_y.pct / 10000);
    // visible region = intersection of clip box and (when not repeating) the tile
    bool rx = L->repeat_x, ry = L->repeat_y;
    int x0 = ax + px, y0 = ay + py;
    int startx = x0, starty = y0;
    if (rx) { int d = (clx - x0); startx = x0 + (d >= 0 ? (d / tw) * tw : -(((-d) + tw - 1) / tw) * tw); }
    if (ry) { int d = (cly - y0); starty = y0 + (d >= 0 ? (d / th) * th : -(((-d) + th - 1) / th) * th); }
    clip_push_rrect(&S->g, clx, cly, clw, clh, clrad);
    int x1 = clx + clw, y1 = cly + clh;
    int guard = 0;
    const uint32_t *pix = NULL;
    int pw = tw, ph = th;
    if (img) {
        if (tw <= img->w && th <= img->h && (tw != img->w || th != img->h)) { pix = web_image_scaled(img, tw, th); }
        if (!pix) { pix = img->px; pw = img->w; ph = img->h; }
        else { pw = tw; ph = th; }
        if (!pix) { clip_pop(&S->g); return; }
    }
    for (int yy = starty; yy < y1 && guard < 4096; yy += th) {
        if (yy + th <= cly && ry) { guard++; continue; }
        for (int xx = startx; xx < x1 && guard < 4096; xx += tw) {
            guard++;
            if (xx + tw > clx && yy + th > cly) {
                int ix = xx > clx ? xx : clx, iy = yy > cly ? yy : cly;
                int ex = xx + tw < x1 ? xx + tw : x1, ey = yy + th < y1 ? yy + th : y1;
                if (ex > ix && ey > iy) {
                    if (L->gradient) {
                        // the tile, limited to the visible part
                        int saved_w = ex - ix, saved_h = ey - iy;
                        (void)saved_w; (void)saved_h;
                        clip_push_rect(&S->g, ix, iy, ex - ix, ey - iy);
                        gfx_gradient(&S->g, L->gradient, xx, yy, tw, th, ix, iy, ex - ix, ey - iy, NULL, tw, th);
                        clip_pop(&S->g);
                    } else {
                        gfx_image(&S->g, pix, pw, ph, xx, yy, tw, th, ix, iy, ex - ix, ey - iy, 0);
                    }
                }
            }
            if (!rx) break;
        }
        if (!ry) break;
    }
    clip_pop(&S->g);
}

static void paint_decor(struct pp *S, struct box *c, int ox, int oy) {
    const struct style *s = c->style;
    c->ax = ox; c->ay = oy;
    c->stack_order = ++S->seq;
    if (c->hidden) return;
    int w = c->w, h = c->h;
    if (w <= 0 || h <= 0) return;
    // quick reject
    int cx0, cy0, cx1, cy1;
    clip_bounds(&S->g, &cx0, &cy0, &cx1, &cy1);
    int reach = 0;
    for (int i = 0; i < s->shadow_count; i++) { int r = s->shadows[i].blur + (s->shadows[i].spread > 0 ? s->shadows[i].spread : 0) + abs(s->shadows[i].x) + abs(s->shadows[i].y); if (r > reach) reach = r; }
    bool off = ox + w + reach <= cx0 || ox - reach >= cx1 || oy + h + reach <= cy0 || oy - reach >= cy1;
    if (!off) {
        int rad[8];
        radii_of(c, w, h, rad);
        for (int i = s->shadow_count - 1; i >= 0; i--) if (!s->shadows[i].inset) gfx_shadow(&S->g, ox, oy, w, h, rad, &s->shadows[i]);
        uint32_t bg = cur(s, s->bg_color);
        bool root_canvas = S->p->canvas_from_body && c->node && c->node->tag == T_BODY;
        // clip box for the background colour: the first layer decides (the last one in CSS, but they are normally the same)
        int clipmode = s->bg_count ? s->bg[s->bg_count - 1].clip : 0;
        int bx = ox, by = oy, bw = w, bh = h, brad[8]; memcpy(brad, rad, sizeof(brad));
        if (clipmode == 1 || clipmode == 2) {
            bx += c->border.left; by += c->border.top; bw -= c->border.left + c->border.right; bh -= c->border.top + c->border.bottom;
            inner_radii(rad, &c->border, &c->pad, clipmode == 2, brad);
            if (clipmode == 2) { bx += c->pad.left; by += c->pad.top; bw -= c->pad.left + c->pad.right; bh -= c->pad.top + c->pad.bottom; }
        }
        if ((bg >> 24) && !root_canvas && bw > 0 && bh > 0) gfx_fill_rrect(&S->g, bx, by, bw, bh, brad, bg);
        for (int i = s->bg_count - 1; i >= 0; i--) {
            const struct bg_layer *L = &s->bg[i];
            if (!L->url && !L->gradient) continue;
            int lx = ox, ly = oy, lw = w, lh = h, lr[8]; memcpy(lr, rad, sizeof(lr));
            if (L->clip == 1 || L->clip == 2) {
                lx += c->border.left; ly += c->border.top; lw -= c->border.left + c->border.right; lh -= c->border.top + c->border.bottom;
                inner_radii(rad, &c->border, &c->pad, L->clip == 2, lr);
                if (L->clip == 2) { lx += c->pad.left; ly += c->pad.top; lw -= c->pad.left + c->pad.right; lh -= c->pad.top + c->pad.bottom; }
            }
            if (L->clip == 3 || lw <= 0 || lh <= 0) continue;
            if (L->gradient && !L->url) {
                // gradients: paint directly, honoring the rounded clip
                paint_bg_layer(S, c, L, ox, oy, lx, ly, lw, lh, lr);
            } else paint_bg_layer(S, c, L, ox, oy, lx, ly, lw, lh, lr);
        }
        // inset shadows: a simple soft edge inside the padding box
        for (int i = 0; i < s->shadow_count; i++) {
            const struct shadow *sh = &s->shadows[i];
            if (!sh->inset || (sh->color >> 24) == 0) continue;
            int ix = ox + c->border.left, iy = oy + c->border.top, iw = w - c->border.left - c->border.right, ih = h - c->border.top - c->border.bottom;
            if (iw <= 0 || ih <= 0) continue;
            int irad[8];
            inner_radii(rad, &c->border, &c->pad, false, irad);
            clip_push_rrect(&S->g, ix, iy, iw, ih, irad);
            int blur = sh->blur > 0 ? sh->blur : 1;
            int steps = blur < 12 ? blur : 12;
            for (int k = 0; k < steps; k++) {
                uint32_t a = (sh->color >> 24) * (uint32_t)(steps - k) / (uint32_t)(steps + 1);
                uint32_t col = (a << 24) | (sh->color & 0xFFFFFF);
                int off_ = sh->spread + k * blur / steps;
                int dx = sh->x, dy = sh->y;
                // four edge strips offset by the shadow
                gfx_fill(&S->g, ix, iy, iw, (off_ + (dy > 0 ? dy : 0)) > 0 ? 1 + 0 : 0, 0);
                int top = off_ + dy, left = off_ + dx, right = off_ - dx, bottom = off_ - dy;
                if (top >= 0) gfx_fill(&S->g, ix, iy + top, iw, 1, col);
                if (bottom >= 0) gfx_fill(&S->g, ix, iy + ih - 1 - bottom, iw, 1, col);
                if (left >= 0) gfx_fill(&S->g, ix + left, iy, 1, ih, col);
                if (right >= 0) gfx_fill(&S->g, ix + iw - 1 - right, iy, 1, ih, col);
            }
            clip_pop(&S->g);
        }
        // borders
        if (c->border.top || c->border.right || c->border.bottom || c->border.left) {
            int bw4[4] = { c->border.top, c->border.right, c->border.bottom, c->border.left };
            uint8_t st[4];
            uint32_t col[4];
            for (int i = 0; i < 4; i++) { st[i] = s->border_style[i]; col[i] = cur(s, s->border_color[i]); }
            gfx_border(&S->g, ox, oy, w, h, rad, bw4, st, col);
        }
        // outline for the element with the keyboard focus
        if (c->node && c->node->focusable >= 0 && c->node->focusable == S->p->focus_cur && c->node->box == c && c->type != BT_INLINE)
            ring(S, ox, oy, w, h);
    }
}

// ---- Text ----

static void paint_text_frag(struct pp *S, const struct frag *fr, int ox, int oy) {
    struct box *tb = fr->box;
    const struct style *s = tb->style;
    if (tb->hidden || !tb->transformed) return;
    if (fr->text_len <= 0) return;
    int x = ox + fr->x, top = oy + fr->y;
    int base = top + fr->baseline;
    uint32_t col = cur(s, s->color);
    int font = font_for(s);
    gfx_text(&S->g, font, s->font_size, x * 64, base, tb->transformed + fr->text_off, fr->text_len, col, s->letter_spacing, s->word_spacing);
    int deco = s->text_decoration;
    if (deco) {
        uint32_t dc = (s->deco_color >> 24) ? cur(s, s->deco_color) : col;
        int px = s->font_size;
        int th = px / 14; if (th < 1) th = 1;
        // trim the trailing blank so the line stops at the last letter
        int w = fr->w;
        if (deco & DECO_UNDERLINE) {
            int ul = px / 12 + 1;
            gfx_fill(&S->g, x, base + ul, w, th, dc);
        }
        if (deco & DECO_OVERLINE) { int asc, desc, gap; wfont_vmetrics(font, px, &asc, &desc, &gap); gfx_fill(&S->g, x, base - asc, w, th, dc); }
        if (deco & DECO_LINETHROUGH) gfx_fill(&S->g, x, base - px * 3 / 10, w, th, dc);
    }
    if (tb->focusable >= 0 && tb->focusable == S->p->focus_cur) ring(S, x - 1, top - 1, fr->w + 2, fr->h + 2);
}

static void paint_inline_decor(struct pp *S, const struct frag *fr, int ox, int oy) {
    struct box *ib = fr->box;
    const struct style *s = ib->style;
    if (ib->hidden) return;
    int x = ox + fr->x, y = oy + fr->y, w = fr->w, h = fr->h;
    uint32_t bg = cur(s, s->bg_color);
    int rad[8] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) { int v = len_resolve(&s->radius[i], h); rad[i] = v < 0 ? 0 : v; }
    // a box split over several lines is square at the break
    if (!fr->first_piece) { rad[0] = rad[3] = 0; }
    if (!fr->last_piece) { rad[1] = rad[2] = 0; }
    if (bg >> 24) gfx_fill_rrect(&S->g, x, y, w, h, rad, bg);
    for (int i = 0; i < s->bg_count; i++) {
        const struct bg_layer *L = &s->bg[i];
        if (L->gradient && !L->url) {
            clip_push_rrect(&S->g, x, y, w, h, rad);
            gfx_gradient(&S->g, L->gradient, x, y, w, h, x, y, w, h, NULL, w, h);
            clip_pop(&S->g);
        }
    }
    int bw[4] = { ib->border.top, fr->last_piece ? ib->border.right : 0, ib->border.bottom, fr->first_piece ? ib->border.left : 0 };
    if (bw[0] || bw[1] || bw[2] || bw[3]) {
        uint8_t st[4]; uint32_t col[4];
        for (int i = 0; i < 4; i++) { st[i] = s->border_style[i]; col[i] = cur(s, s->border_color[i]); }
        gfx_border(&S->g, x, y, w, h, rad, bw, st, col);
    }
}

// ---- Replaced content and form controls ----

static const char *attr_or(const struct node *n, const char *name, const char *def) {
    const char *v = n ? node_attr(n, name) : NULL;
    return v ? v : def;
}

static void draw_string(struct pp *S, const struct style *s, int x, int base, const char *t, int len, uint32_t col) {
    gfx_text(&S->g, font_for(s), s->font_size, x * 64, base, t, len, col, 0, 0);
}

static int text_px(const struct style *s, const char *t, int len) {
    return (wfont_text_width(font_for(s), s->font_size, t, len) + 32) >> 6;
}

static void fit_image(const struct style *s, int cw, int ch, int iw, int ih, int *dw, int *dh, int *dx, int *dy) {
    int w = cw, h = ch;
    switch (s->object_fit) {
    case OF_CONTAIN: case OF_SCALE_DOWN: {
        long long a = (long long)cw * ih, b = (long long)ch * iw;
        if (a < b) { w = cw; h = (int)((long long)cw * ih / iw); } else { h = ch; w = (int)((long long)ch * iw / ih); }
        if (s->object_fit == OF_SCALE_DOWN && (iw < w || ih < h)) { w = iw; h = ih; }
        break; }
    case OF_COVER: {
        long long a = (long long)cw * ih, b = (long long)ch * iw;
        if (a > b) { w = cw; h = (int)((long long)cw * ih / iw); } else { h = ch; w = (int)((long long)ch * iw / ih); }
        break; }
    case OF_NONE: w = iw; h = ih; break;
    default: break;
    }
    if (w < 1) w = 1; if (h < 1) h = 1;
    int px = 50 * 100, py = 50 * 100;
    int ox = (cw - w) / 2, oy = (ch - h) / 2;
    const struct len *lx = &s->object_pos_x, *ly = &s->object_pos_y;
    if (lx->kind == L_VAL) { if (lx->pct && !lx->px) ox = (int)((long long)(cw - w) * lx->pct / 10000); else ox = len_resolve(lx, cw - w); }
    if (ly->kind == L_VAL) { if (ly->pct && !ly->px) oy = (int)((long long)(ch - h) * ly->pct / 10000); else oy = len_resolve(ly, ch - h); }
    (void)px; (void)py;
    *dw = w; *dh = h; *dx = ox; *dy = oy;
}

const uint32_t *web_box_svg(struct page *p, struct box *b, int w, int h);

static void draw_picture(struct pp *S, const uint32_t *pix, int pw, int ph, struct box *c, int ix, int iy, int cw, int ch, int natural_w, int natural_h) {
    const struct style *s = c->style;
    int dw, dh, dx, dy;
    fit_image(s, cw, ch, natural_w, natural_h, &dw, &dh, &dx, &dy);
    (void)pix; (void)pw; (void)ph;
    (void)dw; (void)dh; (void)dx; (void)dy; (void)ix; (void)iy;
}

static void paint_replaced(struct pp *S, struct box *c, int ox, int oy) {
    const struct style *s = c->style;
    if (c->hidden) return;
    int ix = ox + c->border.left + c->pad.left, iy = oy + c->border.top + c->pad.top;
    int cw = c->w - c->border.left - c->border.right - c->pad.left - c->pad.right;
    int ch = c->h - c->border.top - c->border.bottom - c->pad.top - c->pad.bottom;
    if (cw <= 0 || ch <= 0) goto focus;
    int rad[8], irad[8];
    radii_of(c, c->w, c->h, rad);
    inner_radii(rad, &c->border, &c->pad, true, irad);
    switch (c->replaced) {
    case RK_IMAGE: case RK_SVG: {
        const uint32_t *pix = NULL;
        int pw = 0, ph = 0, nw = 0, nh = 0;
        struct wimage *im = c->image;
        if (c->replaced == RK_SVG) {
            pix = web_box_svg(S->p, c, cw, ch);
            pw = cw; ph = ch; nw = cw; nh = ch;
        } else if (im && im->state == IMG_READY && im->px) {
            nw = im->natural_w > 0 ? im->natural_w : im->w; nh = im->natural_h > 0 ? im->natural_h : im->h;
            int dw, dh, dx, dy;
            fit_image(s, cw, ch, nw, nh, &dw, &dh, &dx, &dy);
            if ((im->svg || (dw <= im->w && dh <= im->h)) && (dw != im->w || dh != im->h)) pix = web_image_scaled(im, dw, dh);
            if (pix) { pw = dw; ph = dh; } else { pix = im->px; pw = im->w; ph = im->h; }
            clip_push_rrect(&S->g, ix, iy, cw, ch, irad);
            gfx_image(&S->g, pix, pw, ph, ix + dx, iy + dy, dw, dh, ix, iy, cw, ch, 0);
            clip_pop(&S->g);
            pix = NULL; nw = 0;
        } else if (im && im->state == IMG_FAILED) {
            // the picture couldn't be loaded: show its description in a box
            gfx_fill(&S->g, ix, iy, cw, ch, 0x1F808080u);
            const char *alt = c->alt;
            if (alt && *alt) {
                clip_push_rect(&S->g, ix, iy, cw, ch);
                int asc, desc, gap; wfont_vmetrics(font_for(s), s->font_size, &asc, &desc, &gap);
                draw_string(S, s, ix + 2, iy + asc + 2, alt, w_len(alt), cur(s, s->color));
                clip_pop(&S->g);
            }
        }
        if (pix && nw) {
            int dw, dh, dx, dy;
            fit_image(s, cw, ch, nw, nh, &dw, &dh, &dx, &dy);
            clip_push_rrect(&S->g, ix, iy, cw, ch, irad);
            gfx_image(&S->g, pix, pw, ph, ix + dx, iy + dy, dw, dh, ix, iy, cw, ch, 0);
            clip_pop(&S->g);
        }
        break; }
    case RK_INPUT_CHECK: case RK_INPUT_RADIO: {
        bool radio = c->replaced == RK_INPUT_RADIO;
        bool on = node_has_attr(c->node, "checked");
        int sz = cw < ch ? cw : ch;
        int x = ix + (cw - sz) / 2, y = iy + (ch - sz) / 2;
        int r6[8], r3[8]; uniform(r6, sz / 2); uniform(r3, 3);
        const int *rr = radio ? r6 : r3;
        if (on) {
            gfx_fill_rrect(&S->g, x, y, sz, sz, rr, ACCENT);
            if (radio) { int d = sz / 2; int rr2[8]; uniform(rr2, d / 2); gfx_fill_rrect(&S->g, x + (sz - d) / 2, y + (sz - d) / 2, d, d, rr2, 0xFFFFFFFFu); }
            else {
                int t = sz / 8 + 1;
                line(S, x + sz * 22 / 100, y + sz * 52 / 100, x + sz * 42 / 100, y + sz * 72 / 100, t, 0xFFFFFFFFu);
                line(S, x + sz * 42 / 100, y + sz * 72 / 100, x + sz * 78 / 100, y + sz * 28 / 100, t, 0xFFFFFFFFu);
            }
        } else {
            gfx_fill_rrect(&S->g, x, y, sz, sz, rr, 0xFF767676u);
            int in[8]; for (int q = 0; q < 8; q++) in[q] = rr[q] > 1 ? rr[q] - 1 : 0;
            gfx_fill_rrect(&S->g, x + 1, y + 1, sz - 2, sz - 2, in, 0xFFFFFFFFu);
        }
        break; }
    case RK_INPUT_TEXT: {
        const char *type = node_input_type(c->node);
        const char *val = attr_or(c->node, "value", "");
        bool pw_ = w_eq0(type, "password");
        char tmp[256];
        uint32_t col = cur(s, s->color);
        int len = w_len(val);
        if (len == 0) {
            val = attr_or(c->node, "placeholder", "");
            len = w_len(val);
            col = (col & 0xFF000000u) | 0x00757575u;
            if (s->bg_color && ((s->bg_color >> 16) & 255) < 90) col = (col & 0xFF000000u) | 0x00A0A0A0u;
        } else if (pw_) {
            int n = 0;
            const char *q = val, *e = val + len;
            while (q < e && n + 3 < (int)sizeof(tmp)) { w_utf8_next(&q, e); tmp[n++] = (char)0xE2; tmp[n++] = (char)0x80; tmp[n++] = (char)0xA2; }
            val = tmp; len = n;
        }
        int asc, desc, gap; wfont_vmetrics(font_for(s), s->font_size, &asc, &desc, &gap);
        int base = iy + (ch - (asc + desc)) / 2 + asc;
        clip_push_rect(&S->g, ix, iy, cw, ch);
        // when the text is longer than the box, show its end
        int tw = text_px(s, val, len);
        int x = ix;
        if (tw > cw && c->node->focusable == S->p->focus_cur) x = ix + cw - tw;
        draw_string(S, s, x, base, val, len, col);
        if (c->node->focusable >= 0 && c->node->focusable == S->p->focus_cur) gfx_fill(&S->g, x + tw + 1, base - asc, 1, asc + desc, cur(s, s->color));
        clip_pop(&S->g);
        break; }
    case RK_TEXTAREA: {
        char buf[1024];
        int n = node_text(c->node, buf, (int)sizeof(buf) - 1);
        const char *val = attr_or(c->node, "value", NULL);
        const char *t = val ? val : buf;
        int len = val ? w_len(val) : n;
        uint32_t col = cur(s, s->color);
        if (len == 0) { t = attr_or(c->node, "placeholder", ""); len = w_len(t); col = (col & 0xFF000000u) | 0x00757575u; }
        int asc, desc, gap; wfont_vmetrics(font_for(s), s->font_size, &asc, &desc, &gap);
        int lh = asc + desc + gap;
        clip_push_rect(&S->g, ix, iy, cw, ch);
        int y = iy + asc, i = 0;
        while (i <= len && y - asc < iy + ch) {
            int j = i;
            while (j < len && t[j] != '\n') j++;
            if (j > i) draw_string(S, s, ix, y, t + i, j - i, col);
            y += lh; i = j + 1;
        }
        clip_pop(&S->g);
        break; }
    case RK_SELECT: {
        const struct node *sel = c->node;
        const struct node *chosen = NULL;
        for (const struct node *o = sel->first; o && !chosen; o = o->next) {
            if (o->tag == T_OPTION && node_has_attr(o, "selected")) chosen = o;
            else if (o->tag == T_OPTGROUP) for (const struct node *q = o->first; q && !chosen; q = q->next) if (q->tag == T_OPTION && node_has_attr(q, "selected")) chosen = q;
        }
        if (!chosen) for (const struct node *o = sel->first; o && !chosen; o = o->next) {
            if (o->tag == T_OPTION) chosen = o;
            else if (o->tag == T_OPTGROUP) for (const struct node *q = o->first; q && !chosen; q = q->next) if (q->tag == T_OPTION) chosen = q;
        }
        char buf[200]; int n = 0;
        if (chosen) n = node_text(chosen, buf, (int)sizeof(buf) - 1);
        int asc, desc, gap; wfont_vmetrics(font_for(s), s->font_size, &asc, &desc, &gap);
        int base = iy + (ch - (asc + desc)) / 2 + asc;
        clip_push_rect(&S->g, ix, iy, cw - 16, ch);
        draw_string(S, s, ix, base, buf, n, cur(s, s->color));
        clip_pop(&S->g);
        // the little arrow
        int ax = ix + cw - 12, ay = iy + ch / 2 - 2;
        uint32_t ac = cur(s, s->color);
        for (int k = 0; k < 4; k++) gfx_fill(&S->g, ax + k, ay + k, 9 - 2 * k, 1, ac);
        break; }
    case RK_RANGE: {
        int mn = 0, mx = 100, v;
        const char *a = node_attr(c->node, "min"), *b = node_attr(c->node, "max"), *vv = node_attr(c->node, "value");
        if (a) mn = atoi(a); if (b) mx = atoi(b);
        v = vv ? atoi(vv) : (mn + mx) / 2;
        if (mx <= mn) mx = mn + 1;
        if (v < mn) v = mn; if (v > mx) v = mx;
        int ty = iy + ch / 2 - 2;
        int rr4[8]; uniform(rr4, 2);
        gfx_fill_rrect(&S->g, ix, ty, cw, 4, rr4, 0xFFBDBDBDu);
        int tx = ix + (int)((long long)(cw - 16) * (v - mn) / (mx - mn));
        gfx_fill_rrect(&S->g, ix, ty, tx - ix + 8, 4, rr4, ACCENT);
        int r8[8]; uniform(r8, 8);
        gfx_fill_rrect(&S->g, tx, iy + ch / 2 - 8, 16, 16, r8, ACCENT);
        break; }
    case RK_PROGRESS: {
        int val = 0, mx = 100;
        const char *a = node_attr(c->node, "value"), *b = node_attr(c->node, "max");
        if (a) val = atoi(a); if (b) mx = atoi(b);
        if (mx <= 0) mx = 100;
        int rr4[8]; uniform(rr4, ch / 2);
        gfx_fill_rrect(&S->g, ix, iy, cw, ch, rr4, 0xFFE0E0E0u);
        if (a) gfx_fill_rrect(&S->g, ix, iy, (int)((long long)cw * val / mx), ch, rr4, ACCENT);
        break; }
    case RK_COLOR: {
        const char *v = attr_or(c->node, "value", "#000000");
        uint32_t col = 0xFF000000u;
        if (v[0] == '#' && w_len(v) == 7) { uint32_t x = 0; for (int i = 1; i < 7; i++) x = x * 16 + (uint32_t)(w_hex(v[i]) < 0 ? 0 : w_hex(v[i])); col = 0xFF000000u | x; }
        gfx_fill(&S->g, ix + 2, iy + 2, cw - 4, ch - 4, col);
        break; }
    default: break;
    }
focus:
    if (c->node && c->node->focusable >= 0 && c->node->focusable == S->p->focus_cur && c->node->box == c) { /* ring drawn by paint_decor */ }
}

// ---- List markers ----

static bool first_line_pos(const struct box *c, int cx, int cy, int *x, int *base, const struct box **host) {
    if (c->ifc && c->line_count > 0 && c->lines) {
        *x = cx + c->border.left + c->pad.left;
        *base = cy + c->border.top + c->pad.top + c->lines[0].y + c->lines[0].baseline;
        *host = c;
        return true;
    }
    for (const struct box *k = c->first; k; k = k->next) {
        if (k->type != BT_BLOCK || k->inline_level || k->out_of_flow) continue;
        if (first_line_pos(k, cx + k->x, cy + k->y, x, base, host)) return true;
    }
    return false;
}

static void paint_marker(struct pp *S, struct box *c, int ox, int oy) {
    const struct style *s = c->style;
    if (!c->is_marker_host || s->list_style == LS_NONE || c->hidden) return;
    int x, base;
    const struct box *host;
    if (!first_line_pos(c, ox, oy, &x, &base, &host)) return;
    uint32_t col = cur(s, s->color);
    int px = s->font_size;
    int asc, desc, gap; wfont_vmetrics(font_for(s), px, &asc, &desc, &gap);
    switch (s->list_style) {
    case LS_DISC: case LS_CIRCLE: case LS_SQUARE: {
        int d = px * 3 / 8; if (d < 4) d = 4;
        int mx = x - d - px / 2;
        int my = base - (asc * 55 / 100) - d / 2 + 1;
        int r4[8]; uniform(r4, d / 2);
        if (s->list_style == LS_DISC) gfx_fill_rrect(&S->g, mx, my, d, d, r4, col);
        else if (s->list_style == LS_CIRCLE) {
            gfx_fill_rrect(&S->g, mx, my, d, d, r4, col);
            int in[8]; uniform(in, d / 2 - 1);
            uint32_t bgc = 0xFFFFFFFFu; (void)bgc;
            // hollow: draw the inner disc in the page colour
            uint32_t pc = S->p->canvas_color | 0xFF000000u;
            gfx_fill_rrect(&S->g, mx + 1, my + 1, d - 2, d - 2, in, pc);
        } else gfx_fill(&S->g, mx, my, d, d, col);
        break; }
    default: {
        char buf[32];
        int n = list_marker_text(s, c->list_index, buf, (int)sizeof(buf));
        if (n <= 0) break;
        int w = text_px(s, buf, n);
        gfx_text(&S->g, font_for(s), px, (x - w) * 64, base, buf, n, col, 0, 0);
        break; }
    }
}

// ---- Content of a block ----

static void paint_ifc(struct pp *S, struct box *c, int cx, int cy) {
    if (!c->frags) return;
    int ox = cx + c->border.left + c->pad.left, oy = cy + c->border.top + c->pad.top;
    int cx0, cy0, cx1, cy1;
    clip_bounds(&S->g, &cx0, &cy0, &cx1, &cy1);
    for (int i = 0; i < c->frag_count; i++) {
        const struct frag *fr = &c->frags[i];
        int fy = oy + fr->y;
        if (fy > cy1 + 4 || fy + fr->h < cy0 - 4) {
            if (fr->type != FRAG_BOX) continue;
        }
        switch (fr->type) {
        case FRAG_DECOR: paint_inline_decor(S, fr, ox, oy); break;
        case FRAG_TEXT: paint_text_frag(S, fr, ox, oy); break;
        case FRAG_BOX: {
            struct box *b = fr->box;
            if (deferred(b)) break;
            paint_atomic(S, b, cx + b->x + b->rel_x, cy + b->y + b->rel_y, NULL);
            break; }
        }
    }
}

static void block_phase(struct pp *S, struct box *c, int cx, int cy, int ph, struct ilist *L, bool self_decor) {
    if (ph == 0 && self_decor) paint_decor(S, c, cx, cy);
    bool clip = clips_overflow(c);
    if (clip) {
        int rad[8], ir[8];
        radii_of(c, c->w, c->h, rad);
        inner_radii(rad, &c->border, &c->pad, false, ir);
        clip_push_rrect(&S->g, cx + c->border.left, cy + c->border.top, c->w - c->border.left - c->border.right, c->h - c->border.top - c->border.bottom, ir);
        S->g.clips[S->g.nclip > MAX_CLIPS ? MAX_CLIPS - 1 : S->g.nclip - 1].owner = c;
    }
    bool container = box_is_container(c) && S->ncs < 95;
    if (container) { S->cs[S->ncs].b = c; S->cs[S->ncs].ox = cx; S->cs[S->ncs].oy = cy; S->ncs++; }
    if (ph == 2) paint_marker(S, c, cx, cy);
    if (c->ifc) {
        if (ph == -1 || ph == 1) walk(S, c, cx, cy, ph, L);
        else if (ph == 2) paint_ifc(S, c, cx, cy);
    } else walk(S, c, cx, cy, ph, L);
    if (ph == 2 && c->type == BT_REPLACED) paint_replaced(S, c, cx, cy);
    if (container) S->ncs--;
    if (clip) clip_pop(&S->g);
}

static void add_item(struct pp *S, struct ilist *L, struct box *c, int ox, int oy) {
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 16;
        L->v = realloc(L->v, (size_t)L->cap * sizeof(struct item));
    }
    struct item *it = &L->v[L->n];
    it->b = c; it->ox = ox; it->oy = oy;
    it->z = c->style->has_z ? c->style->z_index : 0;
    it->seq = ++S->tree_seq;
    // clips that apply: all of the current ones, except those of boxes between an absolutely positioned box and its container
    int n = S->g.nclip > MAX_CLIPS ? MAX_CLIPS : S->g.nclip;
    it->clips = n ? malloc((size_t)n * sizeof(struct clip)) : NULL;
    int k = 0;
    for (int i = 0; i < n; i++) {
        const struct clip *cl = &S->g.clips[i];
        bool drop = false;
        if (c->style->position == POS_ABSOLUTE && cl->owner) {
            // is the clip's owner a descendant of the container? (the container is the top of the container stack)
            struct box *cont = S->ncs ? S->cs[S->ncs - 1].b : NULL;
            for (struct box *q = c->parent; q && q != cont; q = q->parent) if (q == cl->owner) { drop = true; break; }
        }
        if (!drop && it->clips) it->clips[k++] = *cl;
    }
    it->nclip = k;
    L->n++;
}

static int item_cmp(const void *a, const void *b) {
    const struct item *x = a, *y = b;
    if (x->z != y->z) return x->z < y->z ? -1 : 1;
    return x->seq < y->seq ? -1 : (x->seq > y->seq);
}

static void subtree_bounds(const struct box *b, int ox, int oy, int *x0, int *y0, int *x1, int *y1, int depth) {
    if (ox < *x0) *x0 = ox; if (oy < *y0) *y0 = oy;
    if (ox + b->w > *x1) *x1 = ox + b->w; if (oy + b->h > *y1) *y1 = oy + b->h;
    if (depth > 40 || clips_overflow(b)) return;
    for (const struct box *c = b->first; c; c = c->next) {
        if (c->type == BT_TEXT || c->type == BT_BR) continue;
        if (c->type == BT_INLINE) { subtree_bounds(c, ox, oy, x0, y0, x1, y1, depth + 1); continue; }
        if (c->style->position == POS_FIXED) continue;
        subtree_bounds(c, ox + c->x, oy + c->y, x0, y0, x1, y1, depth + 1);
    }
}

static void paint_items(struct pp *S, struct ilist *L, bool negative) {
    for (int i = 0; i < L->n; i++) {
        struct item *it = &L->v[i];
        if ((it->z < 0) != negative) continue;
        // swap in the clips that applied where it was found
        struct clip saved[MAX_CLIPS];
        int sn = S->g.nclip > MAX_CLIPS ? MAX_CLIPS : S->g.nclip;
        memcpy(saved, S->g.clips, (size_t)sn * sizeof(struct clip));
        int snc = S->g.nclip;
        if (it->nclip) memcpy(S->g.clips, it->clips, (size_t)it->nclip * sizeof(struct clip));
        S->g.nclip = it->nclip;
        struct box *b = it->b;
        if (b->style->opacity < 255) {
            int x0 = it->ox - 40, y0 = it->oy - 40, x1 = it->ox + b->w + 40, y1 = it->oy + b->h + 40;
            subtree_bounds(b, it->ox, it->oy, &x0, &y0, &x1, &y1, 0);
            x0 -= 40; y0 -= 40; x1 += 40; y1 += 40;
            uint32_t *buf; int sx, sy, sw, sh;
            gfx_group_begin(&S->g, x0, y0, x1 - x0, y1 - y0, &buf, &sx, &sy, &sw, &sh);
            paint_atomic(S, b, it->ox, it->oy, NULL);
            gfx_group_end(&S->g, buf, sx, sy, sw, sh, b->style->opacity);
        } else paint_atomic(S, b, it->ox, it->oy, NULL);
        S->g.nclip = snc;
        memcpy(S->g.clips, saved, (size_t)sn * sizeof(struct clip));
    }
}

static void free_items(struct ilist *L) {
    for (int i = 0; i < L->n; i++) free(L->v[i].clips);
    free(L->v);
    L->v = NULL; L->n = L->cap = 0;
}

// Paint one box and everything inside it as a unit (a stacking context).
// `fixed_root`: the page root also paints the fixed boxes (as an ilist to add to).
static void paint_atomic(struct pp *S, struct box *c, int cx, int cy, struct ilist *fixed_root) {
    struct ilist L = { 0 };
    if (fixed_root) { L = *fixed_root; }
    paint_decor(S, c, cx, cy);
    block_phase(S, c, cx, cy, -1, &L, false);
    if (L.n > 1) qsort(L.v, (size_t)L.n, sizeof(struct item), item_cmp);
    paint_items(S, &L, true);
    block_phase(S, c, cx, cy, 0, &L, false);
    block_phase(S, c, cx, cy, 1, &L, false);
    block_phase(S, c, cx, cy, 2, &L, false);
    paint_items(S, &L, false);
    free_items(&L);
}

// phase -1 gathers positioned boxes, 0 draws backgrounds, 1 draws floats, 2 draws text and pictures.
static void walk(struct pp *S, struct box *b, int bx, int by, int phase, struct ilist *L) {
    for (struct box *c = b->first; c; c = c->next) {
        if (c->type == BT_TEXT || c->type == BT_BR) continue;
        const struct style *s = c->style;
        if (s->position == POS_FIXED) continue;
        int cx, cy;
        if (c->out_of_flow && !c->is_float) {
            int ox = S->ncs ? S->cs[S->ncs - 1].ox : 0, oy = S->ncs ? S->cs[S->ncs - 1].oy : 0;
            cx = ox + c->x + c->rel_x; cy = oy + c->y + c->rel_y;
        } else if (c->type == BT_INLINE) {
            cx = bx; cy = by;
        } else {
            cx = bx + c->x + c->rel_x; cy = by + c->y + c->rel_y;
        }
        if (c->type == BT_INLINE) {
            if (phase == -1 || phase == 1) walk(S, c, bx, by, phase, L);
            continue;
        }
        if (deferred(c)) {
            if (phase == -1) add_item(S, L, c, cx, cy);
            continue;
        }
        if (c->is_float) {
            if (phase == 1) paint_atomic(S, c, cx, cy, NULL);
            continue;
        }
        if (c->inline_level) continue;          // drawn with its line (paint_ifc)
        block_phase(S, c, cx, cy, phase, L, true);
    }
}

static void clear_seq(struct box *b, int depth) {
    b->stack_order = 0;
    if (depth > 400) return;
    for (struct box *c = b->first; c; c = c->next) clear_seq(c, depth + 1);
}

void paint_page(struct page *p, struct gui_canvas *c, int scroll_y) {
    struct pp S;
    memset(&S, 0, sizeof(S));
    S.p = p;
    S.g.p = p; S.g.c = c; S.g.scroll_y = scroll_y; S.g.group_alpha = 255;
    uint32_t bg = p->canvas_color;
    if ((bg >> 24) == 0) bg = p->dark ? 0xFF1B1B1Fu : 0xFFFFFFFFu;
    uint32_t fill = bg & 0xFFFFFF;
    for (int i = 0; i < c->width * c->height; i++) c->pixels[i] = fill;
    struct box *rb = p->root_box;
    if (!rb) return;
    clear_seq(rb, 0);
    S.cs[0].b = rb; S.cs[0].ox = 0; S.cs[0].oy = 0; S.ncs = 1;
    // fixed boxes ride along as positioned items of the root
    struct ilist fx = { 0 };
    for (struct box *a = p->fixed_first; a; a = a->abs_next) {
        if (fx.n == fx.cap) { fx.cap = fx.cap ? fx.cap * 2 : 8; fx.v = realloc(fx.v, (size_t)fx.cap * sizeof(struct item)); }
        struct item *it = &fx.v[fx.n++];
        it->b = a; it->ox = a->x + a->rel_x; it->oy = scroll_y + a->y + a->rel_y;
        it->z = a->style->has_z ? a->style->z_index : 0;
        it->seq = 1000000 + ++S.tree_seq;
        it->clips = NULL; it->nclip = 0;
    }
    S.ncs = 0;
    S.cs[S.ncs].b = rb; S.cs[S.ncs].ox = 0; S.cs[S.ncs].oy = 0; S.ncs = 1;
    paint_atomic(&S, rb, 0, 0, fx.n ? &fx : NULL);
    free(fx.v);
    p->paint_seq = S.seq;
}

void web_page_paint(struct page *p, struct gui_canvas *c, int scroll_y) { paint_page(p, c, scroll_y); }

// ---- Which element is at this spot? ----

struct hit { int seq; struct node *node; int focus; bool text; };

static void hit_walk(const struct box *b, int x, int y, struct hit *best, int depth) {
    if (depth > 400) return;
    if (b->type != BT_INLINE && b->type != BT_TEXT && b->type != BT_BR && b->stack_order > 0 && !b->hidden && !b->style->pointer_events_none) {
        if (x >= b->ax && x < b->ax + b->w && y >= b->ay && y < b->ay + b->h) {
            if (b->stack_order >= best->seq) { best->seq = b->stack_order; best->node = b->node; best->focus = b->focusable; best->text = false; }
        }
        if (b->ifc && b->frags) {
            int ox = b->ax + b->border.left + b->pad.left, oy = b->ay + b->border.top + b->pad.top;
            for (int i = 0; i < b->frag_count; i++) {
                const struct frag *fr = &b->frags[i];
                if (fr->type != FRAG_TEXT) continue;
                if (x >= ox + fr->x && x < ox + fr->x + fr->w && y >= oy + fr->y && y < oy + fr->y + fr->h) {
                    if (b->stack_order >= best->seq) {
                        best->seq = b->stack_order; best->node = fr->box->node; best->focus = fr->focusable; best->text = true;
                    }
                }
            }
        }
    }
    for (const struct box *c = b->first; c; c = c->next) hit_walk(c, x, y, best, depth + 1);
}

// `x`, `y` are page coordinates (scroll already added). Returns the element under the point and, through `focus`, its focus index (or -1).
struct node *web_page_hit(struct page *p, int x, int y, int *focus) {
    struct hit best = { 0, NULL, -1, false };
    if (p->root_box) hit_walk(p->root_box, x, y, &best, 0);
    if (focus) *focus = best.focus;
    return best.node;
}

// The rectangle (page coordinates) of a focusable element, for scrolling it into view. Returns false if it wasn't drawn.
bool web_page_focus_rect(struct page *p, int focus_id, int *x, int *y, int *w, int *h) {
    bool found = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    struct box *stack[512]; int sp = 0;
    if (!p->root_box) return false;
    stack[sp++] = p->root_box;
    while (sp) {
        struct box *b = stack[--sp];
        if (b->type != BT_INLINE && b->type != BT_TEXT && b->type != BT_BR && b->stack_order > 0) {
            if (b->node && b->node->focusable == focus_id && b->node->box == b) {
                x0 = b->ax; y0 = b->ay; x1 = b->ax + b->w; y1 = b->ay + b->h; found = true; break;
            }
            if (b->ifc && b->frags) {
                int ox = b->ax + b->border.left + b->pad.left, oy = b->ay + b->border.top + b->pad.top;
                for (int i = 0; i < b->frag_count; i++) {
                    const struct frag *fr = &b->frags[i];
                    if (fr->focusable != focus_id || fr->type != FRAG_TEXT) continue;
                    int fx0 = ox + fr->x, fy0 = oy + fr->y, fx1 = fx0 + fr->w, fy1 = fy0 + fr->h;
                    if (!found) { x0 = fx0; y0 = fy0; x1 = fx1; y1 = fy1; found = true; }
                    else { if (fx0 < x0) x0 = fx0; if (fy0 < y0) y0 = fy0; if (fx1 > x1) x1 = fx1; if (fy1 > y1) y1 = fy1; }
                }
            }
        }
        for (struct box *c = b->first; c && sp < 511; c = c->next) stack[sp++] = c;
    }
    if (!found) return false;
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    return true;
}
