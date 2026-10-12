// web_gfx.c - drawing.

#include "web_gfx.h"

static inline uint32_t blend(uint32_t dst, uint32_t src_rgb, int a) {
    if (a >= 255) return src_rgb & 0xFFFFFF;
    if (a <= 0) return dst;
    uint32_t sr = (src_rgb >> 16) & 255, sg = (src_rgb >> 8) & 255, sb = src_rgb & 255;
    uint32_t dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;
    uint32_t r = (sr * (uint32_t)a + dr * (uint32_t)(255 - a) + 127) / 255;
    uint32_t g = (sg * (uint32_t)a + dg * (uint32_t)(255 - a) + 127) / 255;
    uint32_t b = (sb * (uint32_t)a + db * (uint32_t)(255 - a) + 127) / 255;
    return (r << 16) | (g << 8) | b;
}

static int len_to_px_pct(const struct len *l, int base, int def) {
    if (l->kind != L_VAL) return def;
    return len_resolve(l, base);
}

// ---- Integer square root ----
static int isqrt(int v) {
    if (v <= 0) return 0;
    int r = 0, bit = 1 << 28;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static int isqrt64(long long v) {
    if (v <= 0) return 0;
    long long r = 0, bit = 1LL << 60;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return (int)r;
}
static inline bool rad_any(const int *r) { for (int i = 0; i < 8; i++) if (r[i]) return true; return false; }

static void radii_fit(int w, int h, const int in[8], int out[8]);

// ---- Clips ----

void clip_bounds(struct pctx *P, int *x0, int *y0, int *x1, int *y1) {
    int ax0 = 0, ay0 = P->scroll_y, ax1 = P->c->width, ay1 = P->scroll_y + P->c->height;
    for (int i = 0; i < P->nclip; i++) {
        const struct clip *c = &P->clips[i];
        if (c->x0 > ax0) ax0 = c->x0; if (c->y0 > ay0) ay0 = c->y0;
        if (c->x1 < ax1) ax1 = c->x1; if (c->y1 < ay1) ay1 = c->y1;
    }
    *x0 = ax0; *y0 = ay0; *x1 = ax1; *y1 = ay1;
}

bool clip_empty(struct pctx *P) {
    int x0, y0, x1, y1;
    clip_bounds(P, &x0, &y0, &x1, &y1);
    return x0 >= x1 || y0 >= y1;
}

void clip_push_rect(struct pctx *P, int x, int y, int w, int h) {
    if (P->nclip >= MAX_CLIPS) { P->nclip++; return; }
    struct clip *c = &P->clips[P->nclip++];
    memset(c, 0, sizeof(*c));
    c->x0 = x; c->y0 = y; c->x1 = x + w; c->y1 = y + h;
}

void clip_push_rrect(struct pctx *P, int x, int y, int w, int h, const int rad[8]) {
    if (P->nclip >= MAX_CLIPS) { P->nclip++; return; }
    struct clip *c = &P->clips[P->nclip++];
    memset(c, 0, sizeof(*c));
    c->x0 = x; c->y0 = y; c->x1 = x + w; c->y1 = y + h;
    if (rad && rad_any(rad)) {
        c->round = true; c->rx = x; c->ry = y; c->rw = w; c->rh = h;
        radii_fit(w, h, rad, c->rad);
    }
}

void clip_pop(struct pctx *P) { if (P->nclip > 0) P->nclip--; }

// Coverage (0..255) of pixel (px, py) by a rounded rectangle. rad: x radii of the four corners, then their y radii.
static int rr_cover(int x, int y, int w, int h, const int rad[8], int px, int py) {
    if (px < x || py < y || px >= x + w || py >= y + h) return 0;
    int lx = px - x, ly = py - y;
    int rx, ry, cx, cy;
    if (lx < rad[0] && ly < rad[4]) { rx = rad[0]; ry = rad[4]; cx = rx; cy = ry; }
    else if (lx >= w - rad[1] && ly < rad[5]) { rx = rad[1]; ry = rad[5]; cx = w - rx; cy = ry; }
    else if (lx >= w - rad[2] && ly >= h - rad[6]) { rx = rad[2]; ry = rad[6]; cx = w - rx; cy = h - ry; }
    else if (lx < rad[3] && ly >= h - rad[7]) { rx = rad[3]; ry = rad[7]; cx = rx; cy = h - ry; }
    else return 255;
    if (rx <= 0 || ry <= 0) return 255;
    long long dx = (long long)lx * 16 + 8 - (long long)cx * 16, dy = (long long)ly * 16 + 8 - (long long)cy * 16;
    long long dys = dy * rx / ry;                 // squash the ellipse into a circle of radius rx
    int d = isqrt64(dx * dx + dys * dys);
    int diff = rx * 16 - d;
    if (diff >= 8) return 255;
    if (diff <= -8) return 0;
    return (diff + 8) * 255 / 16;
}

static inline int clips_cover(struct pctx *P, int x, int y) {
    int cov = 255;
    for (int i = 0; i < P->nclip && i < MAX_CLIPS; i++) {
        const struct clip *c = &P->clips[i];
        if (!c->round) continue;
        int k = rr_cover(c->rx, c->ry, c->rw, c->rh, c->rad, x, y);
        if (k == 0) return 0;
        if (k < 255) cov = cov * k / 255;
    }
    return cov;
}

static bool any_round(struct pctx *P) {
    for (int i = 0; i < P->nclip && i < MAX_CLIPS; i++) if (P->clips[i].round) return true;
    return false;
}

static inline void put(struct pctx *P, int x, int y, uint32_t rgb, int a) {
    uint32_t *px = &P->c->pixels[(y - P->scroll_y) * P->c->width + x];
    *px = blend(*px, rgb, a);
}

// ---- Rectangles ----

void gfx_fill(struct pctx *P, int x, int y, int w, int h, uint32_t color) {
    int a = (int)(color >> 24);
    if (P->group_alpha != 255) a = a * P->group_alpha / 255;
    if (a == 0 || w <= 0 || h <= 0) return;
    int cx0, cy0, cx1, cy1;
    clip_bounds(P, &cx0, &cy0, &cx1, &cy1);
    if (x < cx0) { w -= cx0 - x; x = cx0; }
    if (y < cy0) { h -= cy0 - y; y = cy0; }
    if (x + w > cx1) w = cx1 - x;
    if (y + h > cy1) h = cy1 - y;
    if (w <= 0 || h <= 0) return;
    bool round = any_round(P);
    uint32_t rgb = color & 0xFFFFFF;
    for (int yy = y; yy < y + h; yy++) {
        uint32_t *row = &P->c->pixels[(yy - P->scroll_y) * P->c->width];
        if (!round && a >= 255) { for (int xx = x; xx < x + w; xx++) row[xx] = rgb; }
        else if (!round) { for (int xx = x; xx < x + w; xx++) row[xx] = blend(row[xx], rgb, a); }
        else {
            for (int xx = x; xx < x + w; xx++) {
                int cov = clips_cover(P, xx, yy);
                if (cov) row[xx] = blend(row[xx], rgb, a * cov / 255);
            }
        }
    }
}

static void radii_fit(int w, int h, const int in[8], int out[8]) {
    long long f = 10000;
    #define CHK(a, b, len) do { long long sm = (long long)(a) + (b); if (sm > (len) && sm > 0) { long long ff = (long long)(len) * 10000 / sm; if (ff < f) f = ff; } } while (0)
    CHK(in[0], in[1], w); CHK(in[3], in[2], w); CHK(in[4], in[7], h); CHK(in[5], in[6], h);
    #undef CHK
    for (int i = 0; i < 8; i++) out[i] = (int)((long long)in[i] * f / 10000);
}

void gfx_fill_rrect(struct pctx *P, int x, int y, int w, int h, const int radin[8], uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int rad[8];
    if (!radin) memset(rad, 0, sizeof(rad)); else radii_fit(w, h, radin, rad);
    if (!rad_any(rad)) { gfx_fill(P, x, y, w, h, color); return; }
    int a = (int)(color >> 24);
    if (P->group_alpha != 255) a = a * P->group_alpha / 255;
    if (a == 0) return;
    int cx0, cy0, cx1, cy1;
    clip_bounds(P, &cx0, &cy0, &cx1, &cy1);
    int ox = x, oy = y, ow = w, oh = h;
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    bool round = any_round(P);
    uint32_t rgb = color & 0xFFFFFF;
    int maxr = 0;
    for (int i = 0; i < 8; i++) if (rad[i] > maxr) maxr = rad[i];
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = &P->c->pixels[(yy - P->scroll_y) * P->c->width];
        bool in_corner_band = (yy - oy) < maxr || (oy + oh - 1 - yy) < maxr;
        for (int xx = x0; xx < x1; xx++) {
            int cov = 255;
            if (in_corner_band && ((xx - ox) < maxr || (ox + ow - 1 - xx) < maxr)) cov = rr_cover(ox, oy, ow, oh, rad, xx, yy);
            if (!cov) continue;
            if (round) { int k = clips_cover(P, xx, yy); cov = cov * k / 255; if (!cov) continue; }
            row[xx] = blend(row[xx], rgb, a * cov / 255);
        }
    }
}

void gfx_hline(struct pctx *P, int x, int y, int w, int thickness, uint32_t color) { gfx_fill(P, x, y, w, thickness, color); }

// ---- Borders ----

static uint32_t shade(uint32_t c, int pct) {
    uint32_t a = c & 0xFF000000u;
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    r = r * pct / 100; g = g * pct / 100; b = b * pct / 100;
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return a | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t side_color(int side, int style, uint32_t color) {
    // groove / ridge / inset / outset use light and dark versions
    bool top_left = side == 0 || side == 3;
    switch (style) {
    case BS_INSET: return top_left ? shade(color, 60) : color;
    case BS_OUTSET: return top_left ? color : shade(color, 60);
    case BS_GROOVE: return top_left ? shade(color, 60) : color;
    case BS_RIDGE: return top_left ? color : shade(color, 60);
    }
    return color;
}

static void draw_side(struct pctx *P, int side, int x, int y, int w, int h, int bw, int style, uint32_t color, int inner0, int inner1) {
    // straight stretch of one border side: x,y,w,h is the strip. `inner0/inner1` lets dashes begin after corners
    (void)inner0; (void)inner1;
    if (bw <= 0 || w <= 0 || h <= 0) return;
    uint32_t col = side_color(side, style, color);
    switch (style) {
    case BS_NONE: case BS_HIDDEN: return;
    case BS_DASHED: case BS_DOTTED: {
        bool horiz = side == 0 || side == 2;
        int len = horiz ? w : h;
        int dash = style == BS_DOTTED ? (bw > 1 ? bw : 1) : (bw * 3 > 3 ? bw * 3 : 3);
        int gap = style == BS_DOTTED ? dash : dash;
        for (int pos = 0; pos < len; pos += dash + gap) {
            int l = dash; if (pos + l > len) l = len - pos;
            if (horiz) gfx_fill(P, x + pos, y, l, h, col); else gfx_fill(P, x, y + pos, w, l, col);
        }
        return;
    }
    case BS_DOUBLE: {
        if (bw < 3) { gfx_fill(P, x, y, w, h, col); return; }
        int t = bw / 3;
        bool horiz = side == 0 || side == 2;
        if (horiz) {
            if (side == 0) { gfx_fill(P, x, y, w, t, col); gfx_fill(P, x, y + h - t, w, t, col); }
            else { gfx_fill(P, x, y, w, t, col); gfx_fill(P, x, y + h - t, w, t, col); }
        } else { gfx_fill(P, x, y, t, h, col); gfx_fill(P, x + w - t, y, t, h, col); }
        return;
    }
    case BS_GROOVE: case BS_RIDGE: {
        bool horiz = side == 0 || side == 2;
        uint32_t c1 = side_color(side, style, color), c2 = side_color(side ^ 2, style, color);
        if (horiz) { gfx_fill(P, x, y, w, h / 2 + (h & 1 ? 0 : 0), c1); gfx_fill(P, x, y + h / 2, w, h - h / 2, c2); }
        else { gfx_fill(P, x, y, w / 2, h, c1); gfx_fill(P, x + w / 2, y, w - w / 2, h, c2); }
        return;
    }
    default: gfx_fill(P, x, y, w, h, col); return;
    }
}

void gfx_border(struct pctx *P, int x, int y, int w, int h, const int radin[8], const int bw[4], const uint8_t style[4], const uint32_t color[4]) {
    if (w <= 0 || h <= 0) return;
    int rad[8];
    if (radin) radii_fit(w, h, radin, rad); else memset(rad, 0, sizeof(rad));
    bool any = bw[0] || bw[1] || bw[2] || bw[3];
    if (!any) return;
    bool round = rad_any(rad);
    if (!round) {
        // top, bottom full width; left/right between them, with mitred corners drawn as diagonal splits
        for (int side = 0; side < 4; side++) {
            int t = bw[side];
            if (t <= 0 || (color[side] >> 24) == 0 || style[side] == BS_NONE || style[side] == BS_HIDDEN) continue;
            bool mitre = bw[0] > 0 || bw[1] > 0 || bw[2] > 0 || bw[3] > 0;
            (void)mitre;
            // do the common case: a straight strip, corners belong to top/bottom unless colors differ
            bool same_color = color[0] == color[1] && color[1] == color[2] && color[2] == color[3] && style[0] == style[1] && style[1] == style[2] && style[2] == style[3];
            if (same_color || side == 0 || side == 2) {
                if (side == 0) draw_side(P, 0, x, y, w, t, t, style[0], color[0], 0, 0);
                else if (side == 2) draw_side(P, 2, x, y + h - t, w, t, t, style[2], color[2], 0, 0);
                else if (side == 1) draw_side(P, 1, x + w - t, y + bw[0], t, h - bw[0] - bw[2], t, style[1], color[1], 0, 0);
                else draw_side(P, 3, x, y + bw[0], t, h - bw[0] - bw[2], t, style[3], color[3], 0, 0);
            } else {
                // different colors: left and right strips between the horizontal ones, with a diagonal at the corners
                if (side == 1) draw_side(P, 1, x + w - t, y + bw[0], t, h - bw[0] - bw[2], t, style[1], color[1], 0, 0);
                else draw_side(P, 3, x, y + bw[0], t, h - bw[0] - bw[2], t, style[3], color[3], 0, 0);
            }
        }
        // corner joins where a vertical border meets a horizontal one with a different color: split diagonally
        bool diag = !(color[0] == color[1] && color[1] == color[2] && color[2] == color[3]);
        if (diag) {
            // top-left (sides 3 and 0), top-right (1 and 0), bottom-right (1 and 2), bottom-left (3 and 2)
            for (int c = 0; c < 4; c++) {
                int hs = (c < 2) ? 0 : 2, vs = (c == 0 || c == 3) ? 3 : 1;
                int cw = bw[vs], ch = bw[hs];
                if (cw <= 0 || ch <= 0) continue;
                int cx = (vs == 3) ? x : x + w - cw, cy = (hs == 0) ? y : y + h - ch;
                for (int yy = 0; yy < ch; yy++) for (int xx = 0; xx < cw; xx++) {
                    // which side owns this pixel: compare position against the diagonal
                    int fx = (vs == 3) ? xx : cw - 1 - xx, fy = (hs == 0) ? yy : ch - 1 - yy;
                    bool horiz_owns = (long long)fy * cw < (long long)fx * ch;
                    int owner = horiz_owns ? hs : vs;
                    if (style[owner] == BS_NONE || (color[owner] >> 24) == 0) continue;
                    gfx_fill(P, cx + xx, cy + yy, 1, 1, side_color(owner, style[owner], color[owner]));
                }
            }
        }
        return;
    }
    // rounded: ring between outer and inner rounded rectangles
    int inner_rad[8];
    int ix = x + bw[3], iy = y + bw[0], iw = w - bw[3] - bw[1], ih = h - bw[0] - bw[2];
    inner_rad[0] = rad[0] - bw[3]; inner_rad[4] = rad[4] - bw[0];
    inner_rad[1] = rad[1] - bw[1]; inner_rad[5] = rad[5] - bw[0];
    inner_rad[2] = rad[2] - bw[1]; inner_rad[6] = rad[6] - bw[2];
    inner_rad[3] = rad[3] - bw[3]; inner_rad[7] = rad[7] - bw[2];
    for (int i = 0; i < 4; i++) { if (inner_rad[i] < 0 || inner_rad[i + 4] < 0) inner_rad[i] = inner_rad[i + 4] = 0; }
    int cx0, cy0, cx1, cy1;
    clip_bounds(P, &cx0, &cy0, &cx1, &cy1);
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    bool clipround = any_round(P);
    int maxr = 0;
    for (int i = 0; i < 8; i++) if (rad[i] > maxr) maxr = rad[i];
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            int ly = yy - y, lx = xx - x;
            bool in_band_y = ly < (maxr > bw[0] ? maxr : bw[0]) || (h - 1 - ly) < (maxr > bw[2] ? maxr : bw[2]);
            bool on_strip = lx < bw[3] || (w - 1 - lx) < bw[1] || ly < bw[0] || (h - 1 - ly) < bw[2];
            bool in_corner = (lx < maxr || (w - 1 - lx) < maxr) && in_band_y;
            if (!on_strip && !in_corner) continue;
            int outer = rr_cover(x, y, w, h, rad, xx, yy);
            if (!outer) continue;
            int inner = (xx >= ix && xx < ix + iw && yy >= iy && yy < iy + ih) ? rr_cover(ix, iy, iw, ih, inner_rad, xx, yy) : 0;
            int cov = outer - inner * outer / 255;
            if (cov <= 0) continue;
            // which side's colour? by position relative to the box centre diagonals
            int dxl = lx, dxr = w - 1 - lx, dyt = ly, dyb = h - 1 - ly;
            int side;
            // normalise distances by border widths so thick sides win near their edge
            long long st = (long long)dyt * 1000 / (bw[0] ? bw[0] : 1000), sb = (long long)dyb * 1000 / (bw[2] ? bw[2] : 1000);
            long long sl = (long long)dxl * 1000 / (bw[3] ? bw[3] : 1000), sr = (long long)dxr * 1000 / (bw[1] ? bw[1] : 1000);
            long long best = st; side = 0;
            if (sr < best) { best = sr; side = 1; }
            if (sb < best) { best = sb; side = 2; }
            if (sl < best) { best = sl; side = 3; }
            if (style[side] == BS_NONE || style[side] == BS_HIDDEN) continue;
            uint32_t col = side_color(side, style[side], color[side]);
            int a = (int)(col >> 24);
            if (P->group_alpha != 255) a = a * P->group_alpha / 255;
            a = a * cov / 255;
            if (clipround) a = a * clips_cover(P, xx, yy) / 255;
            if (a > 0) put(P, xx, yy, col, a);
        }
    }
}

// ---- Gradients ----

static void stop_positions(const struct gradient *g, int len_px, int out[MAX_STOPS], int stop_px_out[MAX_STOPS]) {
    // positions in thousandths of the gradient line, filled in for stops without one
    int n = g->stop_count;
    long long pos[MAX_STOPS];
    bool has[MAX_STOPS];
    for (int i = 0; i < n; i++) {
        has[i] = g->stops[i].has_pos;
        if (has[i]) {
            const struct len *l = &g->stops[i].pos;
            long long v = (long long)l->px * 1000 / (len_px ? len_px : 1) + (long long)l->pct / 10;
            if (l->expr) v = (long long)len_resolve(l, len_px) * 1000 / (len_px ? len_px : 1);
            pos[i] = v;
        }
    }
    if (!has[0]) { pos[0] = 0; has[0] = true; }
    if (!has[n - 1]) { pos[n - 1] = 1000; has[n - 1] = true; }
    // monotonic
    long long last = pos[0];
    for (int i = 1; i < n; i++) if (has[i]) { if (pos[i] < last) pos[i] = last; last = pos[i]; }
    int i = 0;
    while (i < n) {
        if (has[i]) { i++; continue; }
        int j = i;
        while (j < n && !has[j]) j++;
        long long a = pos[i - 1], b = pos[j];
        for (int k = i; k < j; k++) pos[k] = a + (b - a) * (k - i + 1) / (j - i + 1);
        i = j;
    }
    for (int k = 0; k < n; k++) { out[k] = (int)pos[k]; }
    (void)stop_px_out;
}

static uint32_t gradient_color(const struct gradient *g, const int *pos, long long t /* thousandths */, int* out_a) {
    int n = g->stop_count;
    if (t <= pos[0]) { uint32_t c = g->stops[0].color; *out_a = (int)(c >> 24); return c; }
    if (t >= pos[n - 1]) { uint32_t c = g->stops[n - 1].color; *out_a = (int)(c >> 24); return c; }
    int i = 1;
    while (i < n - 1 && t > pos[i]) i++;
    long long p0 = pos[i - 1], p1 = pos[i];
    uint32_t c0 = g->stops[i - 1].color, c1 = g->stops[i].color;
    if (p1 <= p0) { *out_a = (int)(c1 >> 24); return c1; }
    long long f = (t - p0) * 1000 / (p1 - p0);          // 0..1000
    // premultiplied interpolation
    int a0 = (int)(c0 >> 24), a1 = (int)(c1 >> 24);
    int r0 = (int)((c0 >> 16) & 255) * a0, g0 = (int)((c0 >> 8) & 255) * a0, b0 = (int)(c0 & 255) * a0;
    int r1 = (int)((c1 >> 16) & 255) * a1, g1 = (int)((c1 >> 8) & 255) * a1, b1 = (int)(c1 & 255) * a1;
    long long a = (a0 * (1000 - f) + a1 * f) / 1000;
    long long r = (r0 * (1000 - f) + r1 * f) / 1000, gg = (g0 * (1000 - f) + g1 * f) / 1000, b = (b0 * (1000 - f) + b1 * f) / 1000;
    *out_a = (int)a;
    if (a == 0) return 0;
    int rr = (int)(r / a), ggg = (int)(gg / a), bb = (int)(b / a);
    if (rr > 255) rr = 255; if (ggg > 255) ggg = 255; if (bb > 255) bb = 255;
    return ((uint32_t)rr << 16) | ((uint32_t)ggg << 8) | (uint32_t)bb;
}

// Direction vectors in 1/1024 for angle in tenths of degrees: sin/cos table by 1 degree
static const short sin_tab[91] = { 0,18,36,54,71,89,107,125,143,160,178,195,213,230,248,265,282,299,316,333,350,367,384,400,416,433,449,465,481,496,512,527,543,558,573,587,602,616,630,644,658,672,685,698,711,724,737,749,761,773,784,796,807,818,828,839,849,859,868,878,887,896,904,912,920,928,935,943,949,956,962,968,974,979,984,989,994,998,1002,1005,1008,1011,1014,1016,1018,1020,1021,1022,1023,1023,1024 };

static int isin(int deg10) {
    int d = ((deg10 % 3600) + 3600) % 3600;
    int deg = d / 10;
    int q = deg / 90, r = deg % 90;
    int v;
    switch (q) { case 0: v = sin_tab[r]; break; case 1: v = sin_tab[90 - r]; break; case 2: v = -sin_tab[r]; break; default: v = -sin_tab[90 - r]; break; }
    return v;
}
static int icos(int deg10) { return isin(deg10 + 900); }

void gfx_gradient(struct pctx *P, const struct gradient *g, int x, int y, int w, int h, int cl_x, int cl_y, int cl_w, int cl_h, const int radin[8], int tile_w, int tile_h) {
    if (!g || g->stop_count == 0 || w <= 0 || h <= 0) return;
    int rad[8] = { 0 };
    if (radin) radii_fit(cl_w, cl_h, radin, rad);
    int cx0, cy0, cx1, cy1;
    clip_bounds(P, &cx0, &cy0, &cx1, &cy1);
    int x0 = cl_x > cx0 ? cl_x : cx0, y0 = cl_y > cy0 ? cl_y : cy0;
    int x1 = cl_x + cl_w < cx1 ? cl_x + cl_w : cx1, y1 = cl_y + cl_h < cy1 ? cl_y + cl_h : cy1;
    if (x0 >= x1 || y0 >= y1) return;
    bool clipround = any_round(P);
    bool boxround = rad_any(rad);
    int pos[MAX_STOPS];
    int n = g->stop_count;
    long long dxv = 0, dyv = 0, len1024 = 1;
    int len_px;
    int ccx = x + w / 2, ccy = y + h / 2;
    if (!g->radial) {
        int ang = g->angle;
        if (g->to_corner) {
            // the angle that makes the 50% line pass through the other two corners
            int sx = g->corner_x, sy = g->corner_y;
            // direction perpendicular to the diagonal from the opposite corners
            long long vx = (long long)h * sx, vy = -(long long)w * sy;      // (up is -y)
            // convert to an angle by choosing the vector (sx*h, sy*w) rotated; simpler: use ratio
            dxv = (long long)sx * h; dyv = (long long)sy * w;
            // normalise to length 1024
            long long l = isqrt((int)((dxv * dxv + dyv * dyv) / 16)) * 4;      // sqrt(dx^2+dy^2), reduced to avoid overflow
            if (l == 0) l = 1;
            dxv = dxv * 1024 / l; dyv = dyv * 1024 / l;
            (void)vx; (void)vy;
        } else {
            dxv = isin(ang); dyv = -icos(ang);
        }
        long long ax = dxv < 0 ? -dxv : dxv, ay = dyv < 0 ? -dyv : dyv;
        len1024 = (ax * w + ay * h);                // gradient line length times 1024
        len_px = (int)((len1024 + 512) / 1024);
        if (len_px < 1) len_px = 1;
    } else {
        // ellipse / circle radii
        long long rx = w / 2, ry = h / 2;
        len_px = 1;
        (void)rx; (void)ry;
    }
    // radial geometry
    long long rrx = 1, rry = 1;
    int gcx = ccx, gcy = ccy;
    if (g->radial) {
        gcx = x + len_to_px_pct(&g->cx, w, w / 2); gcy = y + len_to_px_pct(&g->cy, h, h / 2);
        long long dl = gcx - x, dr = x + w - gcx, dt = gcy - y, db = y + h - gcy;
        if (dl < 0) dl = -dl; if (dr < 0) dr = -dr; if (dt < 0) dt = -dt; if (db < 0) db = -db;
        if (g->extent == 4) {
            rrx = len_resolve(&g->rx, w); rry = len_resolve(&g->ry, h);
            if (g->rx.kind == L_VAL && g->rx.pct && !g->rx.px) rrx = (long long)w * g->rx.pct / 10000;
            if (g->ry.kind == L_VAL && g->ry.pct && !g->ry.px) rry = (long long)h * g->ry.pct / 10000;
        } else if (g->extent == 1 || g->extent == 3) {
            long long mx = g->extent == 1 ? (dl < dr ? dl : dr) : (dl > dr ? dl : dr);
            long long my = g->extent == 1 ? (dt < db ? dt : db) : (dt > db ? dt : db);
            if (g->shape == 1) { long long m = g->extent == 1 ? (mx < my ? mx : my) : (mx > my ? mx : my); rrx = rry = m; }
            else { rrx = mx; rry = my; }
        } else {
            long long mx = g->extent == 2 ? (dl < dr ? dl : dr) : (dl > dr ? dl : dr);
            long long my = g->extent == 2 ? (dt < db ? dt : db) : (dt > db ? dt : db);
            if (g->shape == 1) { long long d = isqrt((int)(mx * mx + my * my)); rrx = rry = d; }
            else { rrx = mx * 1414 / 1000; rry = my * 1414 / 1000; }
        }
        if (rrx < 1) rrx = 1; if (rry < 1) rry = 1;
    }
    stop_positions(g, g->radial ? (int)rrx : len_px, pos, NULL);
    (void)tile_w; (void)tile_h;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            // inside the (possibly rounded) box?
            int cov = 255;
            if (boxround) { cov = rr_cover(cl_x, cl_y, cl_w, cl_h, rad, xx, yy); if (!cov) continue; }
            long long t;
            if (!g->radial) {
                long long px = (xx - ccx) * 2 + 1, py = (yy - ccy) * 2 + 1;       // half pixels
                long long dot = (px * dxv + py * dyv) / 2;                         // in 1/1024 px
                t = 500 + dot * 1000 / (len1024 ? len1024 : 1);
                // add a half-pixel offset consistent with the start of the line
            } else {
                // distance from the centre, in thousandths of the radius (half-pixel units to sample pixel centres)
                long long dx2 = (long long)(xx - gcx) * 2 + 1, dy2 = (long long)(yy - gcy) * 2 + 1;
                long long ex = dx2 * 1000 / (rrx * 2), ey = dy2 * 1000 / (rry * 2);
                t = isqrt64(ex * ex + ey * ey);
            }
            if (g->repeating && pos[n - 1] > pos[0]) {
                long long span = pos[n - 1] - pos[0];
                t = pos[0] + (((t - pos[0]) % span) + span) % span;
            }
            int a;
            uint32_t col = gradient_color(g, pos, t, &a);
            if (P->group_alpha != 255) a = a * P->group_alpha / 255;
            a = a * cov / 255;
            if (clipround) a = a * clips_cover(P, xx, yy) / 255;
            if (a > 0) put(P, xx, yy, col, a);
        }
    }
}

// ---- Shadows ----

static void box_blur_h(uint8_t *src, uint8_t *dst, int w, int h, int r) {
    if (r < 1) { memcpy(dst, src, (size_t)w * h); return; }
    int win = 2 * r + 1;
    for (int y = 0; y < h; y++) {
        const uint8_t *s = src + (size_t)y * w;
        uint8_t *d = dst + (size_t)y * w;
        int sum = 0;
        for (int x = -r; x <= r; x++) { int xx = x < 0 ? 0 : (x >= w ? w - 1 : x); sum += (x >= 0 && x < w) ? s[xx] : 0; }
        for (int x = 0; x < w; x++) {
            d[x] = (uint8_t)(sum / win);
            int add = x + r + 1, sub = x - r;
            sum += (add < w ? s[add] : 0) - (sub >= 0 ? s[sub] : 0);
        }
    }
}

static void box_blur_v(uint8_t *src, uint8_t *dst, int w, int h, int r) {
    if (r < 1) { memcpy(dst, src, (size_t)w * h); return; }
    int win = 2 * r + 1;
    for (int x = 0; x < w; x++) {
        int sum = 0;
        for (int y = 0; y <= r && y < h; y++) sum += src[(size_t)y * w + x];
        for (int y = 0; y < h; y++) {
            dst[(size_t)y * w + x] = (uint8_t)(sum / win);
            int add = y + r + 1, sub = y - r;
            sum += (add < h ? src[(size_t)add * w + x] : 0) - (sub >= 0 ? src[(size_t)sub * w + x] : 0);
        }
    }
}

void gfx_shadow(struct pctx *P, int x, int y, int w, int h, const int radin[8], const struct shadow *sh) {
    if (sh->inset) return;
    if ((sh->color >> 24) == 0) return;
    int sx = x + sh->x - sh->spread, sy = y + sh->y - sh->spread;
    int sw = w + 2 * sh->spread, shh = h + 2 * sh->spread;
    if (sw <= 0 || shh <= 0) return;
    int blur = sh->blur;
    int ext = blur;
    if (ext > 120) ext = 120;
    int cx0, cy0, cx1, cy1;
    clip_bounds(P, &cx0, &cy0, &cx1, &cy1);
    int rad[8] = { 0 };
    if (radin) for (int i = 0; i < 8; i++) { rad[i] = radin[i] ? radin[i] + sh->spread : 0; if (rad[i] < 0) rad[i] = 0; }
    if (radin) radii_fit(sw, shh, rad, rad);
    // region to work on: the shadow's reach, within the visible area
    int rx0 = sx - ext, ry0 = sy - ext, rx1 = sx + sw + ext, ry1 = sy + shh + ext;
    if (rx0 < cx0) rx0 = cx0; if (ry0 < cy0) ry0 = cy0;
    if (rx1 > cx1) rx1 = cx1; if (ry1 > cy1) ry1 = cy1;
    if (rx0 >= rx1 || ry0 >= ry1) return;
    int mw = rx1 - rx0, mh = ry1 - ry0;
    if ((long long)mw * mh > 4000000) return;
    // the mask needs the margin for the blur around the working region too
    int pad = ext;
    int bw_ = mw + 2 * pad, bh_ = mh + 2 * pad;
    if ((long long)bw_ * bh_ > 8000000) return;
    uint8_t *a = calloc((size_t)bw_ * bh_, 1), *b = malloc((size_t)bw_ * bh_);
    if (!a || !b) { free(a); free(b); return; }
    // draw the shape: (sx, sy, sw, shh) in mask coordinates
    int ox = rx0 - pad, oy = ry0 - pad;
    for (int yy = 0; yy < bh_; yy++) {
        int py = oy + yy;
        if (py < sy || py >= sy + shh) continue;
        for (int xx = 0; xx < bw_; xx++) {
            int px = ox + xx;
            if (px < sx || px >= sx + sw) continue;
            int cov = rad_any(rad) ? rr_cover(sx, sy, sw, shh, rad, px, py) : 255;
            a[(size_t)yy * bw_ + xx] = (uint8_t)cov;
        }
    }
    if (blur > 0) {
        // three box blurs approximate a gaussian with sigma = blur / 2
        int r = (blur * 100 / 2 * 100 / 188 + 50) / 100;
        if (r < 1) r = 1;
        if (r > pad) r = pad;
        box_blur_h(a, b, bw_, bh_, r); box_blur_v(b, a, bw_, bh_, r);
        box_blur_h(a, b, bw_, bh_, r); box_blur_v(b, a, bw_, bh_, r);
        box_blur_h(a, b, bw_, bh_, r); box_blur_v(b, a, bw_, bh_, r);
    }
    bool clipround = any_round(P);
    int col_a = (int)(sh->color >> 24);
    if (P->group_alpha != 255) col_a = col_a * P->group_alpha / 255;
    int bradi[8] = { 0 };
    if (radin) radii_fit(w, h, radin, bradi);
    for (int yy = ry0; yy < ry1; yy++) {
        for (int xx = rx0; xx < rx1; xx++) {
            int m = a[(size_t)(yy - oy) * bw_ + (xx - ox)];
            if (!m) continue;
            // the shadow is not drawn under the box itself
            if (xx >= x && xx < x + w && yy >= y && yy < y + h) {
                int cov = rad_any(bradi) ? rr_cover(x, y, w, h, bradi, xx, yy) : 255;
                if (cov >= 255) continue;
                m = m * (255 - cov) / 255;
                if (!m) continue;
            }
            int al = col_a * m / 255;
            if (clipround) al = al * clips_cover(P, xx, yy) / 255;
            if (al > 0) put(P, xx, yy, sh->color & 0xFFFFFF, al);
        }
    }
    free(a); free(b);
}

// ---- Text ----

void gfx_text(struct pctx *P, int font, int px, int x64, int baseline, const char *text, int len, uint32_t color, int letter_spacing, int word_spacing) {
    int ca = (int)(color >> 24);
    if (P->group_alpha != 255) ca = ca * P->group_alpha / 255;
    if (ca == 0) return;
    int cx0, cy0, cx1, cy1;
    clip_bounds(P, &cx0, &cy0, &cx1, &cy1);
    int asc, desc, gap;
    wfont_vmetrics(font, px, &asc, &desc, &gap);
    if (baseline + desc + 2 < cy0 || baseline - asc - 2 >= cy1) return;
    bool clipround = any_round(P);
    uint32_t rgb = color & 0xFFFFFF;
    const char *q = text, *e = text + len;
    int pen = x64;
    while (q < e) {
        uint32_t cp = w_utf8_next(&q, e);
        const struct wglyph *gl = wfont_glyph(font, px, cp);
        int adv = wfont_advance(font, px, cp);
        if (gl && gl->w > 0 && gl->h > 0 && gl->alpha) {
            int gx = ((pen + 32) >> 6) + gl->x, gy = baseline + gl->y;
            for (int yy = 0; yy < gl->h; yy++) {
                int py = gy + yy;
                if (py < cy0 || py >= cy1) continue;
                const uint8_t *row = gl->alpha + yy * gl->w;
                uint32_t *dst = &P->c->pixels[(py - P->scroll_y) * P->c->width];
                for (int xx = 0; xx < gl->w; xx++) {
                    int a = row[xx];
                    if (!a) continue;
                    int pxl = gx + xx;
                    if (pxl < cx0 || pxl >= cx1) continue;
                    a = a * ca / 255;
                    if (clipround) a = a * clips_cover(P, pxl, py) / 255;
                    if (a > 0) dst[pxl] = blend(dst[pxl], rgb, a);
                }
            }
        }
        pen += adv + letter_spacing + (cp == ' ' ? word_spacing : 0);
    }
}

// ---- Pictures ----

void gfx_image(struct pctx *P, const uint32_t *pix, int iw, int ih, int x, int y, int w, int h, int cx, int cy, int cw, int ch, uint32_t tint_alpha) {
    if (!pix || w <= 0 || h <= 0 || iw <= 0 || ih <= 0) return;
    int cx0, cy0, cx1, cy1;
    clip_bounds(P, &cx0, &cy0, &cx1, &cy1);
    int x0 = x > cx ? x : cx, y0 = y > cy ? y : cy;
    int x1 = x + w < cx + cw ? x + w : cx + cw, y1 = y + h < cy + ch ? y + h : cy + ch;
    if (x0 < cx0) x0 = cx0; if (y0 < cy0) y0 = cy0; if (x1 > cx1) x1 = cx1; if (y1 > cy1) y1 = cy1;
    if (x0 >= x1 || y0 >= y1) return;
    bool clipround = any_round(P);
    int galpha = P->group_alpha;
    (void)tint_alpha;
    bool direct = iw == w && ih == h;
    bool smooth = !direct && (w > iw || h > ih);
    for (int yy = y0; yy < y1; yy++) {
        // source row (in 1/256 pixels for smooth sampling)
        long long fy = 0; int sy0 = 0, sy1 = 0, wy = 0;
        if (direct) sy0 = yy - y;
        else if (smooth) {
            fy = (((long long)(yy - y) * 2 + 1) * ih * 256) / (2 * h) - 128;
            if (fy < 0) fy = 0;
            sy0 = (int)(fy >> 8); wy = (int)(fy & 255); sy1 = sy0 + 1;
            if (sy0 >= ih) sy0 = ih - 1; if (sy1 >= ih) sy1 = ih - 1;
        } else { sy0 = (int)(((long long)(yy - y) * 2 + 1) * ih / (2 * h)); if (sy0 >= ih) sy0 = ih - 1; }
        const uint32_t *r0 = pix + (size_t)sy0 * iw, *r1 = pix + (size_t)sy1 * iw;
        uint32_t *dst = &P->c->pixels[(yy - P->scroll_y) * P->c->width];
        for (int xx = x0; xx < x1; xx++) {
            uint32_t p;
            if (smooth) {
                long long fx = (((long long)(xx - x) * 2 + 1) * iw * 256) / (2 * w) - 128;
                if (fx < 0) fx = 0;
                int sx0 = (int)(fx >> 8), wx = (int)(fx & 255), sx1 = sx0 + 1;
                if (sx0 >= iw) sx0 = iw - 1; if (sx1 >= iw) sx1 = iw - 1;
                uint32_t c00 = r0[sx0], c10 = r0[sx1], c01 = r1[sx0], c11 = r1[sx1];
                // weights (x 65536)
                int w00 = (256 - wx) * (256 - wy), w10 = wx * (256 - wy), w01 = (256 - wx) * wy, w11 = wx * wy;
                unsigned long long A = 0, R = 0, G = 0, B = 0;
                uint32_t cs[4] = { c00, c10, c01, c11 }; int ws[4] = { w00, w10, w01, w11 };
                for (int k = 0; k < 4; k++) {
                    unsigned al = cs[k] >> 24; unsigned long long wa = (unsigned long long)ws[k] * al;
                    A += wa; R += wa * ((cs[k] >> 16) & 255); G += wa * ((cs[k] >> 8) & 255); B += wa * (cs[k] & 255);
                }
                if (A == 0) continue;
                unsigned a8 = (unsigned)(A >> 16);
                p = (a8 << 24) | ((uint32_t)(R / A) << 16) | ((uint32_t)(G / A) << 8) | (uint32_t)(B / A);
            } else {
                int sx = direct ? xx - x : (int)(((long long)(xx - x) * 2 + 1) * iw / (2 * w));
                if (sx >= iw) sx = iw - 1;
                p = r0[sx];
            }
            int a = (int)(p >> 24);
            if (a == 0) continue;
            if (galpha != 255) a = a * galpha / 255;
            if (clipround) a = a * clips_cover(P, xx, yy) / 255;
            if (a > 0) dst[xx] = blend(dst[xx], p & 0xFFFFFF, a);
        }
    }
}

// ---- Opacity groups ----

void gfx_group_begin(struct pctx *P, int x, int y, int w, int h, uint32_t **saved, int *sx, int *sy, int *sw, int *sh) {
    int cx0, cy0, cx1, cy1;
    clip_bounds(P, &cx0, &cy0, &cx1, &cy1);
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    *saved = NULL;
    if (x0 >= x1 || y0 >= y1) { *sx = *sy = *sw = *sh = 0; return; }
    *sx = x0; *sy = y0; *sw = x1 - x0; *sh = y1 - y0;
    uint32_t *buf = malloc((size_t)(*sw) * (*sh) * sizeof(uint32_t));
    if (!buf) { *sw = *sh = 0; return; }
    for (int yy = 0; yy < *sh; yy++) memcpy(buf + (size_t)yy * (*sw), &P->c->pixels[(y0 + yy - P->scroll_y) * P->c->width + x0], (size_t)(*sw) * sizeof(uint32_t));
    *saved = buf;
}

void gfx_group_end(struct pctx *P, uint32_t *saved, int sx, int sy, int sw, int sh, int alpha) {
    if (!saved) return;
    for (int yy = 0; yy < sh; yy++) {
        uint32_t *dst = &P->c->pixels[(sy + yy - P->scroll_y) * P->c->width + sx];
        const uint32_t *old = saved + (size_t)yy * sw;
        for (int xx = 0; xx < sw; xx++) dst[xx] = blend(old[xx], dst[xx], alpha);
    }
    free(saved);
}
