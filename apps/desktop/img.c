// img.c - picture decoders: PNG, JPEG (baseline and progressive), GIF, BMP.
// Integer arithmetic only. Output is 0xAARRGGBB, not premultiplied.

#include "web.h"

// ---------- shared ----------

// Make a picture smaller by a whole factor, averaging.
static bool shrink_to(struct wimage *o, int max_dim) {
    if (max_dim <= 0) return true;
    int big = o->w > o->h ? o->w : o->h;
    if (big <= max_dim) return true;
    int f = (big + max_dim - 1) / max_dim;
    int nw = (o->w + f - 1) / f, nh = (o->h + f - 1) / f;
    uint32_t *n = malloc((size_t)nw * nh * 4);
    if (!n) return false;
    for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) {
        unsigned long long a = 0, r = 0, g = 0, b = 0; int cnt = 0;
        for (int yy = y * f; yy < y * f + f && yy < o->h; yy++) for (int xx = x * f; xx < x * f + f && xx < o->w; xx++) {
            uint32_t c = o->px[(size_t)yy * o->w + xx]; unsigned al = c >> 24;
            a += al; r += ((c >> 16) & 255) * al; g += ((c >> 8) & 255) * al; b += (c & 255) * al; cnt++;
        }
        n[(size_t)y * nw + x] = a ? (((uint32_t)(a / cnt)) << 24) | ((uint32_t)(r / a) << 16) | ((uint32_t)(g / a) << 8) | (uint32_t)(b / a) : 0;
    }
    free(o->px); o->px = n; o->w = nw; o->h = nh;
    return true;
}

// ---------- inflate ----------

struct inf { const uint8_t *in; size_t n, pos; uint32_t bits; int nbits; uint8_t *out; size_t cap, len; bool err; };
struct huff { uint16_t count[16]; uint16_t sym[320]; };

static int inf_bit(struct inf *s) {
    if (s->nbits == 0) {
        if (s->pos >= s->n) { s->err = true; return 0; }
        s->bits = s->in[s->pos++]; s->nbits = 8;
    }
    int b = s->bits & 1; s->bits >>= 1; s->nbits--;
    return b;
}
static int inf_bits(struct inf *s, int n) { int v = 0; for (int i = 0; i < n; i++) v |= inf_bit(s) << i; return v; }

static void huff_build(struct huff *h, const uint8_t *len, int n) {
    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    uint16_t offs[16]; offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
}

static int huff_decode(struct inf *s, const struct huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= inf_bit(s);
        int count = h->count[len];
        if (code - count < first) return h->sym[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
        if (s->err) return -1;
    }
    return -1;
}

static bool inf_put(struct inf *s, uint8_t b) {
    if (s->len >= s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 65536;
        uint8_t *no = realloc(s->out, nc);
        if (!no) return false;
        s->out = no; s->cap = nc;
    }
    s->out[s->len++] = b;
    return true;
}

static const uint16_t lbase[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint16_t lext[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t dbase[] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint16_t dext[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static bool inflate_codes(struct inf *s, const struct huff *lc, const struct huff *dc) {
    for (;;) {
        int sym = huff_decode(s, lc);
        if (sym < 0 || s->err) return false;
        if (sym < 256) { if (!inf_put(s, (uint8_t)sym)) return false; }
        else if (sym == 256) return true;
        else {
            sym -= 257;
            if (sym >= 29) return false;
            int len = lbase[sym] + inf_bits(s, lext[sym]);
            int ds = huff_decode(s, dc);
            if (ds < 0 || ds >= 30) return false;
            size_t dist = dbase[ds] + (size_t)inf_bits(s, dext[ds]);
            if (dist > s->len) return false;
            for (int i = 0; i < len; i++) if (!inf_put(s, s->out[s->len - dist])) return false;
        }
    }
}

static bool zlib_inflate(const uint8_t *data, size_t n, uint8_t **out, size_t *outn) {
    struct inf s; memset(&s, 0, sizeof(s));
    if (n < 2) return false;
    s.in = data + 2; s.n = n - 2;     // skip the zlib header
    bool last;
    do {
        last = inf_bit(&s);
        int type = inf_bits(&s, 2);
        if (s.err) goto fail;
        if (type == 0) {
            s.nbits = 0;
            if (s.pos + 4 > s.n) goto fail;
            unsigned len = s.in[s.pos] | (s.in[s.pos + 1] << 8);
            s.pos += 4;
            if (s.pos + len > s.n) goto fail;
            for (unsigned i = 0; i < len; i++) if (!inf_put(&s, s.in[s.pos++])) goto fail;
        } else if (type == 1 || type == 2) {
            static struct huff lc, dc;
            uint8_t lens[320];
            if (type == 1) {
                for (int i = 0; i < 144; i++) lens[i] = 8;
                for (int i = 144; i < 256; i++) lens[i] = 9;
                for (int i = 256; i < 280; i++) lens[i] = 7;
                for (int i = 280; i < 288; i++) lens[i] = 8;
                huff_build(&lc, lens, 288);
                for (int i = 0; i < 30; i++) lens[i] = 5;
                huff_build(&dc, lens, 30);
            } else {
                int nlen = inf_bits(&s, 5) + 257, ndist = inf_bits(&s, 5) + 1, ncode = inf_bits(&s, 4) + 4;
                static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                uint8_t cl[19]; memset(cl, 0, sizeof(cl));
                for (int i = 0; i < ncode; i++) cl[order[i]] = (uint8_t)inf_bits(&s, 3);
                struct huff ch; huff_build(&ch, cl, 19);
                int idx = 0;
                memset(lens, 0, sizeof(lens));
                while (idx < nlen + ndist) {
                    int sym = huff_decode(&s, &ch);
                    if (sym < 0) goto fail;
                    if (sym < 16) lens[idx++] = (uint8_t)sym;
                    else {
                        int rep, val = 0;
                        if (sym == 16) { if (idx == 0) goto fail; val = lens[idx - 1]; rep = 3 + inf_bits(&s, 2); }
                        else if (sym == 17) rep = 3 + inf_bits(&s, 3);
                        else rep = 11 + inf_bits(&s, 7);
                        if (idx + rep > nlen + ndist) goto fail;
                        while (rep--) lens[idx++] = (uint8_t)val;
                    }
                }
                huff_build(&lc, lens, nlen);
                huff_build(&dc, lens + nlen, ndist);
            }
            if (!inflate_codes(&s, &lc, &dc)) goto fail;
        } else goto fail;
    } while (!last);
    *out = s.out; *outn = s.len;
    return true;
fail:
    // keep what we got: a damaged picture still shows its top part
    if (s.len > 0) { *out = s.out; *outn = s.len; return true; }
    free(s.out);
    return false;
}

// ---------- PNG ----------

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

bool img_decode_png(const uint8_t *d, int n, int max_dim, struct wimage *o) {
    if (n < 33 || memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return false;
    int pos = 8, W = 0, H = 0, depth = 0, ctype = 0, interlace = 0;
    uint8_t pal[256 * 4]; int npal = 0;
    bool has_trns_rgb = false; int trns_r = 0, trns_g = 0, trns_b = 0;
    memset(pal, 255, sizeof(pal));
    uint8_t *idat = NULL; size_t idat_n = 0, idat_cap = 0;
    while (pos + 8 <= n) {
        uint32_t len = be32(d + pos); const uint8_t *t = d + pos + 4; const uint8_t *c = d + pos + 8;
        if ((size_t)pos + 12 + len > (size_t)n) { if (memcmp(t, "IDAT", 4)) break; len = (uint32_t)(n - pos - 8); }
        if (!memcmp(t, "IHDR", 4) && len >= 13) { W = (int)be32(c); H = (int)be32(c + 4); depth = c[8]; ctype = c[9]; interlace = c[12]; }
        else if (!memcmp(t, "PLTE", 4)) { npal = (int)len / 3; if (npal > 256) npal = 256; for (int i = 0; i < npal; i++) { pal[i * 4] = c[i * 3]; pal[i * 4 + 1] = c[i * 3 + 1]; pal[i * 4 + 2] = c[i * 3 + 2]; } }
        else if (!memcmp(t, "tRNS", 4)) {
            if (ctype == 3) for (uint32_t i = 0; i < len && i < 256; i++) pal[i * 4 + 3] = c[i];
            else if (ctype == 0 && len >= 2) { has_trns_rgb = true; trns_r = (c[0] << 8) | c[1]; }
            else if (ctype == 2 && len >= 6) { has_trns_rgb = true; trns_r = (c[0] << 8) | c[1]; trns_g = (c[2] << 8) | c[3]; trns_b = (c[4] << 8) | c[5]; }
        } else if (!memcmp(t, "IDAT", 4)) {
            if (idat_n + len > idat_cap) { idat_cap = (idat_n + len) * 2; uint8_t *ni = realloc(idat, idat_cap); if (!ni) { free(idat); return false; } idat = ni; }
            memcpy(idat + idat_n, c, len); idat_n += len;
        } else if (!memcmp(t, "IEND", 4)) break;
        pos += 12 + (int)len;
    }
    if (W <= 0 || H <= 0 || W > 30000 || H > 30000 || (long long)W * H > 40000000LL || !idat) { free(idat); return false; }
    uint8_t *raw; size_t rawn;
    bool ok = zlib_inflate(idat, idat_n, &raw, &rawn);
    free(idat);
    if (!ok) return false;
    int chans = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : 4;
    int bpp_bits = chans * depth, bpp = (bpp_bits + 7) / 8;
    uint32_t *px = malloc((size_t)W * H * 4);
    if (!px) { free(raw); return false; }
    memset(px, 0, (size_t)W * H * 4);
    // passes
    static const int ax0[7] = { 0, 4, 0, 2, 0, 1, 0 }, ay0[7] = { 0, 0, 4, 0, 2, 0, 1 }, adx[7] = { 8, 8, 4, 4, 2, 2, 1 }, ady[7] = { 8, 8, 8, 4, 4, 2, 2 };
    int passes = interlace ? 7 : 1;
    size_t off = 0;
    for (int ps = 0; ps < passes; ps++) {
        int x0 = interlace ? ax0[ps] : 0, y0 = interlace ? ay0[ps] : 0, dx = interlace ? adx[ps] : 1, dy = interlace ? ady[ps] : 1;
        int pw = (W - x0 + dx - 1) / dx, ph = (H - y0 + dy - 1) / dy;
        if (pw <= 0 || ph <= 0) continue;
        size_t stride = ((size_t)pw * bpp_bits + 7) / 8;
        uint8_t *prev = calloc(stride + 8, 1), *cur = calloc(stride + 8, 1);
        for (int y = 0; y < ph; y++) {
            if (off + 1 + stride > rawn) { memset(cur, 0, stride); }
            else {
                int ft = raw[off];
                const uint8_t *src = raw + off + 1;
                for (size_t i = 0; i < stride; i++) {
                    int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= (size_t)bpp ? prev[i - bpp] : 0, v = src[i];
                    switch (ft) { case 1: v += a; break; case 2: v += b; break; case 3: v += (a + b) / 2; break; case 4: v += paeth(a, b, c); break; }
                    cur[i] = (uint8_t)v;
                }
                off += 1 + stride;
            }
            for (int x = 0; x < pw; x++) {
                uint32_t col;
                int r, g, b, a = 255;
                if (depth == 8) {
                    const uint8_t *q = cur + (size_t)x * chans;
                    switch (ctype) {
                    case 0: r = g = b = q[0]; if (has_trns_rgb && q[0] == trns_r) a = 0; break;
                    case 2: r = q[0]; g = q[1]; b = q[2]; if (has_trns_rgb && r == trns_r && g == trns_g && b == trns_b) a = 0; break;
                    case 3: r = pal[q[0] * 4]; g = pal[q[0] * 4 + 1]; b = pal[q[0] * 4 + 2]; a = pal[q[0] * 4 + 3]; break;
                    case 4: r = g = b = q[0]; a = q[1]; break;
                    default: r = q[0]; g = q[1]; b = q[2]; a = q[3]; break;
                    }
                } else if (depth == 16) {
                    const uint8_t *q = cur + (size_t)x * chans * 2;
                    switch (ctype) {
                    case 0: r = g = b = q[0]; if (has_trns_rgb && ((q[0] << 8) | q[1]) == trns_r) a = 0; break;
                    case 2: r = q[0]; g = q[2]; b = q[4]; if (has_trns_rgb && ((q[0] << 8) | q[1]) == trns_r && ((q[2] << 8) | q[3]) == trns_g && ((q[4] << 8) | q[5]) == trns_b) a = 0; break;
                    case 4: r = g = b = q[0]; a = q[2]; break;
                    default: r = q[0]; g = q[2]; b = q[4]; a = q[6]; break;
                    }
                } else {
                    // 1, 2, 4 bit: grey or palette
                    int bitpos = x * depth, v = (cur[bitpos >> 3] >> (8 - depth - (bitpos & 7))) & ((1 << depth) - 1);
                    if (ctype == 3) { r = pal[v * 4]; g = pal[v * 4 + 1]; b = pal[v * 4 + 2]; a = pal[v * 4 + 3]; }
                    else { int mx = (1 << depth) - 1; r = g = b = v * 255 / mx; if (has_trns_rgb && v == trns_r) a = 0; }
                }
                col = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                px[(size_t)(y0 + y * dy) * W + (x0 + x * dx)] = col;
            }
            uint8_t *t = prev; prev = cur; cur = t;
        }
        free(prev); free(cur);
    }
    free(raw);
    o->px = px; o->w = W; o->h = H; o->natural_w = W; o->natural_h = H;
    o->has_alpha = ctype == 4 || ctype == 6 || ctype == 3 || has_trns_rgb;
    shrink_to(o, max_dim);
    return true;
}

// ---------- JPEG ----------

struct jhuff { uint8_t bits[17]; uint8_t vals[256]; int32_t maxcode[18]; int32_t valptr[17]; int32_t mincode[17]; bool set; };
struct jcomp { int id, h, v, tq; int bw, bh; int16_t *coef; uint8_t *plane; int pw, ph; int dcpred; };
struct jpg {
    const uint8_t *d; int n, pos;
    int W, H, ncomp; bool progressive, adobe; int adobe_tf;
    struct jcomp c[4]; int hmax, vmax, mcux, mcuy;
    uint16_t qt[4][64];
    struct jhuff dc[4], ac[4];
    int restart;
    // bit reader
    uint32_t acc; int nb; bool hit_marker; int eobrun;
};

static const uint8_t zz[64] = { 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };

static void jhuff_make(struct jhuff *h) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k; h->mincode[l] = code;
        code += h->bits[l]; k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? (code - 1) : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7fff;
    h->set = true;
}

static int j_getbit(struct jpg *j) {
    if (j->nb == 0) {
        int b = 0;
        if (!j->hit_marker && j->pos < j->n) {
            b = j->d[j->pos++];
            if (b == 0xFF) {
                int b2 = j->pos < j->n ? j->d[j->pos] : 0;
                if (b2 == 0) j->pos++;
                else { j->hit_marker = true; j->pos--; b = 0; }
            }
        }
        j->acc = (uint32_t)b; j->nb = 8;
    }
    j->nb--;
    return (j->acc >> j->nb) & 1;
}
static int j_getbits(struct jpg *j, int n) { int v = 0; while (n--) v = (v << 1) | j_getbit(j); return v; }
static int j_extend(int v, int t) { return t && v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }

static int j_decode(struct jpg *j, const struct jhuff *h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | j_getbit(j);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) return h->vals[h->valptr[l] + code - h->mincode[l]];
    }
    return 0;
}

static void j_reset(struct jpg *j) {
    j->nb = 0; j->acc = 0; j->eobrun = 0; j->hit_marker = false;
    for (int i = 0; i < 4; i++) j->c[i].dcpred = 0;
    // skip to and over the RSTn marker
    while (j->pos + 1 < j->n && !(j->d[j->pos] == 0xFF && j->d[j->pos + 1] >= 0xD0 && j->d[j->pos + 1] <= 0xD7)) {
        if (j->d[j->pos] == 0xFF && j->d[j->pos + 1] != 0 && j->d[j->pos + 1] != 0xFF) return;   // some other marker: leave it
        j->pos++;
    }
    if (j->pos + 1 < j->n) j->pos += 2;
}

static void j_block(struct jpg *j, struct jcomp *c, int16_t *b, int td, int ta, int ss, int se, int ah, int al) {
    if (!j->progressive) {
        int t = j_decode(j, &j->dc[td]);
        int diff = t ? j_extend(j_getbits(j, t), t) : 0;
        c->dcpred += diff; b[0] = (int16_t)c->dcpred;
        for (int k = 1; k < 64;) {
            int rs = j_decode(j, &j->ac[ta]), r = rs >> 4, s = rs & 15;
            if (s == 0) { if (r == 15) { k += 16; continue; } break; }
            k += r; if (k > 63) break;
            b[zz[k++]] = (int16_t)j_extend(j_getbits(j, s), s);
        }
        return;
    }
    if (ss == 0) {
        if (ah == 0) { int t = j_decode(j, &j->dc[td]); int diff = t ? j_extend(j_getbits(j, t), t) : 0; c->dcpred += diff; b[0] = (int16_t)(c->dcpred * (1 << al)); }
        else if (j_getbit(j)) b[0] |= (int16_t)(1 << al);
        return;
    }
    if (ah == 0) {
        if (j->eobrun > 0) { j->eobrun--; return; }
        for (int k = ss; k <= se;) {
            int rs = j_decode(j, &j->ac[ta]), r = rs >> 4, s = rs & 15;
            if (s == 0) {
                if (r < 15) { j->eobrun = (1 << r) - 1; if (r) j->eobrun += j_getbits(j, r); break; }
                k += 16; continue;
            }
            k += r; if (k > 63) break;
            b[zz[k++]] = (int16_t)(j_extend(j_getbits(j, s), s) * (1 << al));
        }
        return;
    }
    // refinement
    int p1 = 1 << al, m1 = -1 * (1 << al);
    int k = ss;
    if (j->eobrun <= 0) {
        for (; k <= se; k++) {
            int rs = j_decode(j, &j->ac[ta]), r = rs >> 4, s = rs & 15, val = 0;
            if (s == 0) {
                if (r < 15) { j->eobrun = (1 << r); if (r) j->eobrun += j_getbits(j, r); break; }
            } else val = j_getbit(j) ? p1 : m1;
            while (k <= se) {
                int16_t *q = &b[zz[k]];
                if (*q) { if (j_getbit(j)) { if ((*q & p1) == 0) *q = (int16_t)(*q >= 0 ? *q + p1 : *q + m1); } }
                else { if (r == 0) { if (val) *q = (int16_t)val; break; } r--; }
                k++;
            }
            if (j->hit_marker && j->nb == 0) break;
        }
    }
    if (j->eobrun > 0) {
        for (; k <= se; k++) {
            int16_t *q = &b[zz[k]];
            if (*q && j_getbit(j)) { if ((*q & p1) == 0) *q = (int16_t)(*q >= 0 ? *q + p1 : *q + m1); }
        }
        j->eobrun--;
    }
}

#define IDCT_1D(s0,s1,s2,s3,s4,s5,s6,s7) \
    int t0,t1,t2,t3,p1,p2,p3,p4,p5,x0,x1,x2,x3; \
    p2 = s2; p3 = s6; p1 = (p2+p3) * 2217; t2 = p1 + p3 * -7568; t3 = p1 + p2 * 3135; \
    p2 = s0; p3 = s4; t0 = (p2+p3) * 4096; t1 = (p2-p3) * 4096; \
    x0 = t0+t3; x3 = t0-t3; x1 = t1+t2; x2 = t1-t2; \
    t0 = s7; t1 = s5; t2 = s3; t3 = s1; \
    p3 = t0+t2; p4 = t1+t3; p1 = t0+t3; p2 = t1+t2; p5 = (p3+p4) * 4816; \
    t0 *= 1223; t1 *= 8410; t2 *= 12586; t3 *= 6149; \
    p1 = p5 + p1 * -3686; p2 = p5 + p2 * -10498; p3 *= -8035; p4 *= -1598; \
    t3 += p1+p4; t2 += p2+p3; t1 += p2+p4; t0 += p1+p3;

static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

static void idct(uint8_t *out, int stride, const int16_t *data, const uint16_t *q) {
    int val[64], tmp[64];
    for (int i = 0; i < 64; i++) tmp[i] = data[i] * q[i];     // q is in zigzag-undone order (natural)
    int *v = val; const int *d = tmp;
    for (int i = 0; i < 8; i++, d++, v++) {
        if (!d[8] && !d[16] && !d[24] && !d[32] && !d[40] && !d[48] && !d[56]) {
            int dc = d[0] * 4; v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dc;
        } else {
            IDCT_1D(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56])
            x0 += 512; x1 += 512; x2 += 512; x3 += 512;
            v[0] = (x0+t3) >> 10; v[56] = (x0-t3) >> 10; v[8] = (x1+t2) >> 10; v[48] = (x1-t2) >> 10;
            v[16] = (x2+t1) >> 10; v[40] = (x2-t1) >> 10; v[24] = (x3+t0) >> 10; v[32] = (x3-t0) >> 10;
        }
    }
    v = val;
    for (int i = 0; i < 8; i++, v += 8, out += stride) {
        IDCT_1D(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
        x0 += 65536 + (128 << 17); x1 += 65536 + (128 << 17); x2 += 65536 + (128 << 17); x3 += 65536 + (128 << 17);
        out[0] = clamp8((x0+t3) >> 17); out[7] = clamp8((x0-t3) >> 17);
        out[1] = clamp8((x1+t2) >> 17); out[6] = clamp8((x1-t2) >> 17);
        out[2] = clamp8((x2+t1) >> 17); out[5] = clamp8((x2-t1) >> 17);
        out[3] = clamp8((x3+t0) >> 17); out[4] = clamp8((x3-t0) >> 17);
    }
}

static bool jpg_scan(struct jpg *j, int len) {
    const uint8_t *p = j->d + j->pos;
    int ns = p[0];
    if (ns < 1 || ns > 4 || len < 4 + ns * 2) return false;
    int cs[4], td[4], ta[4];
    for (int i = 0; i < ns; i++) {
        int id = p[1 + i * 2], t = p[2 + i * 2], k;
        for (k = 0; k < j->ncomp; k++) if (j->c[k].id == id) break;
        if (k == j->ncomp) k = i < j->ncomp ? i : 0;
        cs[i] = k; td[i] = (t >> 4) & 3; ta[i] = t & 3;
    }
    int ss = p[1 + ns * 2], se = p[2 + ns * 2], ah = p[3 + ns * 2] >> 4, al = p[3 + ns * 2] & 15;
    if (!j->progressive) { ss = 0; se = 63; ah = al = 0; }
    j->pos += len - 2 + 0;     // skip the header; len included its own 2 bytes (pos was after length)
    j->nb = 0; j->acc = 0; j->eobrun = 0; j->hit_marker = false;
    for (int i = 0; i < 4; i++) j->c[i].dcpred = 0;
    int count = 0;
    if (ns == 1) {
        struct jcomp *c = &j->c[cs[0]];
        int bw = (((j->W * c->h + j->hmax - 1) / j->hmax) + 7) / 8, bh = (((j->H * c->v + j->vmax - 1) / j->vmax) + 7) / 8;
        for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
            if (j->restart && count && count % j->restart == 0) j_reset(j);
            j_block(j, c, c->coef + ((size_t)by * c->bw + bx) * 64, td[0], ta[0], ss, se, ah, al);
            count++;
        }
    } else {
        for (int my = 0; my < j->mcuy; my++) for (int mx = 0; mx < j->mcux; mx++) {
            if (j->restart && count && count % j->restart == 0) j_reset(j);
            for (int i = 0; i < ns; i++) {
                struct jcomp *c = &j->c[cs[i]];
                for (int v = 0; v < c->v; v++) for (int h = 0; h < c->h; h++)
                    j_block(j, c, c->coef + ((size_t)(my * c->v + v) * c->bw + mx * c->h + h) * 64, td[i], ta[i], ss, se, ah, al);
            }
            count++;
        }
    }
    // move to the next marker
    while (j->pos + 1 < j->n && !(j->d[j->pos] == 0xFF && j->d[j->pos + 1] != 0 && !(j->d[j->pos + 1] >= 0xD0 && j->d[j->pos + 1] <= 0xD7) && j->d[j->pos + 1] != 0xFF)) j->pos++;
    return true;
}

bool img_decode_jpeg(const uint8_t *d, int n, int max_dim, struct wimage *o) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    struct jpg *j = calloc(1, sizeof(*j));
    if (!j) return false;
    j->d = d; j->n = n; j->pos = 2;
    bool got_sof = false, got_scan = false, ok = false;
    while (j->pos + 4 <= n) {
        if (d[j->pos] != 0xFF) { j->pos++; continue; }
        int m = d[j->pos + 1];
        if (m == 0xFF || m == 0 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { j->pos += (m == 0xFF) ? 1 : 2; continue; }
        if (m == 0xD9) break;
        if (m == 0xD8) { j->pos += 2; continue; }
        int len = (d[j->pos + 2] << 8) | d[j->pos + 3];
        const uint8_t *p = d + j->pos + 4;
        int plen = len - 2;
        if (j->pos + 2 + len > n) { if (m != 0xDA) break; }
        if (m == 0xDB) {
            int k = 0;
            while (k < plen) {
                int pq = p[k] >> 4, tq = p[k] & 3; k++;
                for (int i = 0; i < 64 && k < plen; i++) { int v = pq ? (p[k] << 8 | p[k + 1]) : p[k]; k += pq ? 2 : 1; j->qt[tq][zz[i]] = (uint16_t)v; }
            }
        } else if (m == 0xC4) {
            int k = 0;
            while (k + 17 <= plen) {
                int tc = p[k] >> 4, th = p[k] & 3;
                struct jhuff *h = tc ? &j->ac[th] : &j->dc[th];
                memset(h, 0, sizeof(*h));
                int total = 0;
                for (int i = 1; i <= 16; i++) { h->bits[i] = p[k + i]; total += p[k + i]; }
                k += 17;
                if (total > 256 || k + total > plen) break;
                memcpy(h->vals, p + k, (size_t)total); k += total;
                jhuff_make(h);
            }
        } else if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            if (plen < 6) goto done;
            j->progressive = m == 0xC2;
            if (p[0] != 8) goto done;
            j->H = (p[1] << 8) | p[2]; j->W = (p[3] << 8) | p[4]; j->ncomp = p[5];
            if (j->W <= 0 || j->H <= 0 || (long long)j->W * j->H > 40000000LL || j->ncomp < 1 || j->ncomp > 4 || plen < 6 + 3 * j->ncomp) goto done;
            j->hmax = j->vmax = 1;
            for (int i = 0; i < j->ncomp; i++) {
                j->c[i].id = p[6 + i * 3]; j->c[i].h = p[7 + i * 3] >> 4; j->c[i].v = p[7 + i * 3] & 15; j->c[i].tq = p[8 + i * 3] & 3;
                if (j->c[i].h < 1 || j->c[i].h > 4 || j->c[i].v < 1 || j->c[i].v > 4) goto done;
                if (j->c[i].h > j->hmax) j->hmax = j->c[i].h;
                if (j->c[i].v > j->vmax) j->vmax = j->c[i].v;
            }
            j->mcux = (j->W + 8 * j->hmax - 1) / (8 * j->hmax); j->mcuy = (j->H + 8 * j->vmax - 1) / (8 * j->vmax);
            for (int i = 0; i < j->ncomp; i++) {
                j->c[i].bw = j->mcux * j->c[i].h; j->c[i].bh = j->mcuy * j->c[i].v;
                j->c[i].coef = calloc((size_t)j->c[i].bw * j->c[i].bh * 64, sizeof(int16_t));
                if (!j->c[i].coef) goto done;
            }
            got_sof = true;
        } else if (m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8 && m != 0xCC)) {
            goto done;       // lossless / arithmetic coding: not supported
        } else if (m == 0xDD) { if (plen >= 2) j->restart = (p[0] << 8) | p[1]; }
        else if (m == 0xEE && plen >= 12 && !memcmp(p, "Adobe", 5)) { j->adobe = true; j->adobe_tf = p[11]; }
        else if (m == 0xDA) {
            if (!got_sof) goto done;
            j->pos += 4;
            j->pos -= 2 - 2;        // pos is at the scan header (after the length field)
            // jpg_scan expects pos at the header start and len = header length including the length field
            if (!jpg_scan(j, len)) goto done;
            got_scan = true;
            continue;
        }
        j->pos += 2 + len;
    }
    if (!got_sof || !got_scan) goto done;
    // IDCT into planes
    for (int i = 0; i < j->ncomp; i++) {
        struct jcomp *c = &j->c[i];
        c->pw = c->bw * 8; c->ph = c->bh * 8;
        c->plane = malloc((size_t)c->pw * c->ph);
        if (!c->plane) goto done;
        for (int by = 0; by < c->bh; by++) for (int bx = 0; bx < c->bw; bx++)
            idct(c->plane + (size_t)by * 8 * c->pw + bx * 8, c->pw, c->coef + ((size_t)by * c->bw + bx) * 64, j->qt[c->tq]);
        free(c->coef); c->coef = NULL;
    }
    {
        int big = j->W > j->H ? j->W : j->H;
        int f = (max_dim > 0 && big > max_dim) ? (big + max_dim - 1) / max_dim : 1;
        int ow = (j->W + f - 1) / f, oh = (j->H + f - 1) / f;
        uint32_t *px = malloc((size_t)ow * oh * 4);
        if (!px) goto done;
        for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) {
            long sum[4] = { 0, 0, 0, 0 }; int cnt = 0;
            for (int yy = y * f; yy < y * f + f && yy < j->H; yy++) for (int xx = x * f; xx < x * f + f && xx < j->W; xx++) {
                int v[4];
                for (int i = 0; i < j->ncomp; i++) {
                    struct jcomp *c = &j->c[i];
                    int sx = xx * c->h / j->hmax, sy = yy * c->v / j->vmax;
                    v[i] = c->plane[(size_t)sy * c->pw + sx];
                }
                int r, g, b;
                if (j->ncomp >= 3) {
                    int Y = v[0], cb = v[1] - 128, cr = v[2] - 128;
                    bool ycc = !(j->adobe && j->adobe_tf == 0) && !(j->ncomp == 3 && j->c[0].id == 'R' && j->c[1].id == 'G');
                    if (ycc) { r = Y + ((91881 * cr) >> 16); g = Y - ((22554 * cb + 46802 * cr) >> 16); b = Y + ((116130 * cb) >> 16); }
                    else { r = v[0]; g = v[1]; b = v[2]; }
                    r = clamp8(r); g = clamp8(g); b = clamp8(b);
                    if (j->ncomp == 4) {
                        int k = v[3];
                        // Adobe CMYK is stored inverted
                        if (j->adobe_tf == 2) { r = 255 - r; g = 255 - g; b = 255 - b; }
                        if (j->adobe) { r = r * k / 255; g = g * k / 255; b = b * k / 255; }
                        else { r = r * (255 - k) / 255; g = g * (255 - k) / 255; b = b * (255 - k) / 255; }
                        if (j->adobe_tf == 2) { /* already inverted back above */ }
                    }
                } else { r = g = b = v[0]; }
                sum[0] += r; sum[1] += g; sum[2] += b; cnt++;
            }
            px[(size_t)y * ow + x] = 0xFF000000u | ((uint32_t)(sum[0] / cnt) << 16) | ((uint32_t)(sum[1] / cnt) << 8) | (uint32_t)(sum[2] / cnt);
        }
        o->px = px; o->w = ow; o->h = oh; o->natural_w = j->W; o->natural_h = j->H; o->has_alpha = false;
        ok = true;
    }
done:
    for (int i = 0; i < 4; i++) { free(j->c[i].coef); free(j->c[i].plane); }
    free(j);
    return ok;
}

// ---------- GIF ----------

bool img_decode_gif(const uint8_t *d, int n, int max_dim, struct wimage *o) {
    if (n < 13 || memcmp(d, "GIF8", 4)) return false;
    int W = d[6] | (d[7] << 8), H = d[8] | (d[9] << 8);
    if (W <= 0 || H <= 0 || (long long)W * H > 30000000LL) return false;
    int pos = 13;
    uint8_t gpal[768]; int gn = 0;
    if (d[10] & 0x80) { gn = 2 << (d[10] & 7); if (pos + gn * 3 > n) return false; memcpy(gpal, d + pos, (size_t)gn * 3); pos += gn * 3; }
    uint32_t *px = calloc((size_t)W * H, 4);
    if (!px) return false;
    int trans = -1;
    bool done = false;
    while (pos < n && !done) {
        int b = d[pos++];
        if (b == 0x21) {
            int label = pos < n ? d[pos++] : 0;
            if (label == 0xF9 && pos + 5 < n && d[pos] >= 4) { if (d[pos + 1] & 1) trans = d[pos + 4]; else trans = -1; }
            while (pos < n && d[pos]) pos += d[pos] + 1;
            pos++;
        } else if (b == 0x2C) {
            if (pos + 9 > n) break;
            int ix = d[pos] | (d[pos + 1] << 8), iy = d[pos + 2] | (d[pos + 3] << 8), iw = d[pos + 4] | (d[pos + 5] << 8), ih = d[pos + 6] | (d[pos + 7] << 8);
            int flags = d[pos + 8]; pos += 9;
            uint8_t lpal[768]; const uint8_t *pal = gpal; int pn = gn;
            if (flags & 0x80) { pn = 2 << (flags & 7); if (pos + pn * 3 > n) break; memcpy(lpal, d + pos, (size_t)pn * 3); pos += pn * 3; pal = lpal; }
            bool interlaced = flags & 0x40;
            if (pos >= n) break;
            int mincode = d[pos++];
            if (mincode < 2 || mincode > 8) break;
            // gather sub-blocks
            uint8_t *data = malloc((size_t)n - pos + 1); size_t dn = 0;
            if (!data) break;
            while (pos < n && d[pos]) { int bl = d[pos]; if (pos + 1 + bl > n) bl = n - pos - 1; memcpy(data + dn, d + pos + 1, (size_t)bl); dn += bl; pos += d[pos] + 1; }
            // LZW
            int clear = 1 << mincode, eoi = clear + 1, next = clear + 2, size = mincode + 1;
            static uint16_t prefix[4096]; static uint8_t suffix[4096], stack[4097];
            uint32_t acc = 0; int nb = 0; size_t ip = 0; int old = -1; uint8_t first = 0;
            int x = 0, y = 0, pass = 0;
            for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (uint8_t)i; }
            bool end = false;
            while (!end) {
                while (nb < size && ip < dn) { acc |= (uint32_t)data[ip++] << nb; nb += 8; }
                if (nb < size) break;
                int code = (int)(acc & ((1u << size) - 1)); acc >>= size; nb -= size;
                if (code == clear) { next = clear + 2; size = mincode + 1; old = -1; continue; }
                if (code == eoi) break;
                int sp = 0, cur = code;
                if (old < 0) { if (code >= clear) break; stack[sp++] = (uint8_t)code; first = (uint8_t)code; old = code; goto emit; }
                if (code >= next) { if (code > next) break; stack[sp++] = first; cur = old; }
                while (cur >= clear && sp < 4096) { stack[sp++] = suffix[cur]; cur = prefix[cur]; }
                stack[sp++] = (uint8_t)cur; first = (uint8_t)cur;
                if (next < 4096) { prefix[next] = (uint16_t)old; suffix[next] = first; next++; if (next == (1 << size) && size < 12) size++; }
                old = code;
            emit:
                while (sp > 0) {
                    int idx = stack[--sp];
                    if (y >= ih) { end = true; break; }
                    int yy = y;
                    if (interlaced) yy = pass == 0 ? y * 8 : pass == 1 ? 4 + y * 8 : pass == 2 ? 2 + y * 4 : 1 + y * 2;
                    int X = ix + x, Y = iy + yy;
                    if (idx != trans && idx < pn && X < W && Y < H && Y >= 0)
                        px[(size_t)Y * W + X] = 0xFF000000u | ((uint32_t)pal[idx * 3] << 16) | ((uint32_t)pal[idx * 3 + 1] << 8) | pal[idx * 3 + 2];
                    if (++x >= iw) {
                        x = 0; y++;
                        if (interlaced) {
                            int lim = pass == 0 ? (ih + 7) / 8 : pass == 1 ? (ih + 3) / 8 : pass == 2 ? (ih + 1) / 4 : ih / 2;
                            if (y >= lim) { y = 0; pass++; if (pass > 3) { end = true; break; } }
                        }
                    }
                }
            }
            free(data);
            pos++;      // block terminator
            done = true;     // first frame only
        } else if (b == 0x3B) break;
        else break;
    }
    o->px = px; o->w = W; o->h = H; o->natural_w = W; o->natural_h = H; o->has_alpha = trans >= 0;
    shrink_to(o, max_dim);
    return true;
}

// ---------- BMP ----------

bool img_decode_bmp(const uint8_t *d, int n, int max_dim, struct wimage *o) {
    if (n < 54 || d[0] != 'B' || d[1] != 'M') return false;
    uint32_t off = d[10] | (d[11] << 8) | (d[12] << 16) | ((uint32_t)d[13] << 24);
    int hs = d[14] | (d[15] << 8);
    int W, H, bpp, comp = 0, ncol = 0;
    if (hs == 12) { W = d[18] | (d[19] << 8); H = d[20] | (d[21] << 8); bpp = d[24] | (d[25] << 8); }
    else { W = (int)be32(d + 18); W = d[18] | (d[19] << 8) | (d[20] << 16) | (d[21] << 24); H = d[22] | (d[23] << 8) | (d[24] << 16) | (d[25] << 24); bpp = d[28] | (d[29] << 8); comp = d[30]; ncol = d[46] | (d[47] << 8); }
    bool flip = H > 0; if (H < 0) H = -H;
    if (W <= 0 || H <= 0 || (long long)W * H > 30000000LL || comp > 3) return false;
    if (comp == 1 || comp == 2) return false;
    int ps = hs == 12 ? 3 : 4;
    const uint8_t *pal = d + 14 + hs;
    if (bpp <= 8 && ncol == 0) ncol = 1 << bpp;
    uint32_t *px = malloc((size_t)W * H * 4);
    if (!px) return false;
    size_t stride = (((size_t)W * bpp + 31) / 32) * 4;
    bool alpha = false;
    for (int y = 0; y < H; y++) {
        size_t ro = off + stride * (size_t)y;
        int Y = flip ? H - 1 - y : y;
        for (int x = 0; x < W; x++) {
            uint32_t c = 0xFF000000u;
            if (ro + stride > (size_t)n) { px[(size_t)Y * W + x] = 0; continue; }
            const uint8_t *r = d + ro;
            if (bpp == 24) c |= ((uint32_t)r[x * 3 + 2] << 16) | ((uint32_t)r[x * 3 + 1] << 8) | r[x * 3];
            else if (bpp == 32) { c = ((uint32_t)r[x * 4 + 3] << 24) | ((uint32_t)r[x * 4 + 2] << 16) | ((uint32_t)r[x * 4 + 1] << 8) | r[x * 4]; if (r[x * 4 + 3]) alpha = true; }
            else if (bpp <= 8) {
                int bit = x * bpp, v = (r[bit >> 3] >> (8 - bpp - (bit & 7))) & ((1 << bpp) - 1);
                if (v < ncol) c |= ((uint32_t)pal[v * ps + 2] << 16) | ((uint32_t)pal[v * ps + 1] << 8) | pal[v * ps];
            } else if (bpp == 16) { int v = r[x * 2] | (r[x * 2 + 1] << 8); c |= ((uint32_t)((v >> 10) & 31) * 255 / 31 << 16) | ((uint32_t)((v >> 5) & 31) * 255 / 31 << 8) | ((v & 31) * 255 / 31); }
            px[(size_t)Y * W + x] = c;
        }
    }
    if (bpp == 32 && !alpha) for (size_t i = 0; i < (size_t)W * H; i++) px[i] |= 0xFF000000u;
    o->px = px; o->w = W; o->h = H; o->natural_w = W; o->natural_h = H; o->has_alpha = alpha;
    shrink_to(o, max_dim);
    return true;
}

// ---------- front door ----------

__attribute__((weak)) bool img_decode_webp(const uint8_t *data, int length, int max_dim, struct wimage *out) { (void)data; (void)length; (void)max_dim; (void)out; return false; }

struct wimage *img_decode(const void *data, int length, int max_dim) {
    const uint8_t *d = data;
    if (length < 8) return NULL;
    struct wimage *o = calloc(1, sizeof(struct wimage));
    if (!o) return NULL;
    bool ok = false;
    if (d[0] == 0x89 && d[1] == 'P') ok = img_decode_png(d, length, max_dim, o);
    else if (d[0] == 0xFF && d[1] == 0xD8) ok = img_decode_jpeg(d, length, max_dim, o);
    else if (d[0] == 'G' && d[1] == 'I' && d[2] == 'F') ok = img_decode_gif(d, length, max_dim, o);
    else if (d[0] == 'B' && d[1] == 'M') ok = img_decode_bmp(d, length, max_dim, o);
    else if (length > 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) ok = img_decode_webp(d, length, max_dim, o);
    else {
        // vector pictures
        int k = 0;
        while (k < length && k < 512 && (d[k] == ' ' || d[k] == '\n' || d[k] == '\r' || d[k] == '\t' || d[k] == 0xEF || d[k] == 0xBB || d[k] == 0xBF)) k++;
        if (k < length && d[k] == '<') {
            struct svg_doc *sd = svg_parse_text((const char *)d, length);
            if (sd) {
                int w, h; svg_size(sd, &w, &h);
                if (w <= 0) w = 300; if (h <= 0) h = 150;
                o->svg = sd; o->natural_w = w; o->natural_h = h; o->w = w; o->h = h;
                // rasterised at its own size now; web_image_scaled redraws it for other sizes
                int rw = w, rh = h;
                if (rw > 2400) { rh = rh * 2400 / rw; rw = 2400; }
                o->px = calloc((size_t)rw * rh, 4);
                if (o->px) { svg_render(sd, o->px, rw, rh, 0xFF000000u); o->w = rw; o->h = rh; o->has_alpha = true; ok = true; }
            }
        }
    }
    if (!ok) { free(o->px); free(o); return NULL; }
    return o;
}
