// web_page.c - one web page: loading, files to fetch, layout, addresses.

#include "web.h"
#include "web_css.h"
#include "web_lay.h"

// ================= Addresses =================

static void url_copy(char *out, int max, const char *s, int n) {
    if (n >= max) n = max - 1;
    if (n < 0) n = 0;
    memcpy(out, s, (size_t)n);
    out[n] = '\0';
}

// Where does the path start (after scheme://host)? Returns the index of the first '/', ? or # after the host, or the length.
static int url_authority_end(const char *u) {
    int i = 0;
    while (u[i] && u[i] != ':' && u[i] != '/' && u[i] != '?' && u[i] != '#') i++;
    if (u[i] != ':') return -1;
    i++;
    if (u[i] == '/' && u[i + 1] == '/') {
        i += 2;
        while (u[i] && u[i] != '/' && u[i] != '?' && u[i] != '#') i++;
        return i;
    }
    return i;      // e.g. "mailto:x" or "data:..."
}

static bool has_scheme(const char *u) {
    int i = 0;
    if (!w_is_alpha(u[0])) return false;
    while (w_is_alnum(u[i]) || u[i] == '+' || u[i] == '-' || u[i] == '.') i++;
    return u[i] == ':' && i > 1;
}

void web_url_resolve(const char *base, const char *link, char *out, int max) {
    out[0] = '\0';
    if (!link) return;
    // trim spaces
    while (w_is_space(*link)) link++;
    int ll = (int)strlen(link);
    while (ll > 0 && w_is_space(link[ll - 1])) ll--;
    char lk[2200];
    if (ll > (int)sizeof(lk) - 1) ll = (int)sizeof(lk) - 1;
    memcpy(lk, link, (size_t)ll); lk[ll] = '\0';
    if (has_scheme(lk)) {
        // normalise "http:/x"? no: keep
        url_copy(out, max, lk, ll);
        return;
    }
    if (!base || !has_scheme(base)) { url_copy(out, max, lk, ll); return; }
    int ae = url_authority_end(base);
    if (ae < 0) { url_copy(out, max, lk, ll); return; }
    char scheme[16]; int sl = 0;
    while (base[sl] != ':' && sl < 15) { scheme[sl] = base[sl]; sl++; }
    scheme[sl] = '\0';
    if (lk[0] == '#' ) {
        // same document, new fragment
        int be = 0; while (base[be] && base[be] != '#') be++;
        char tmp[2300]; int n = be < 2200 ? be : 2200; memcpy(tmp, base, (size_t)n); memcpy(tmp + n, lk, (size_t)ll + 1);
        url_copy(out, max, tmp, n + ll);
        return;
    }
    if (lk[0] == '/' && lk[1] == '/') {
        char tmp[2300]; int n = snprintf(tmp, sizeof(tmp), "%s:%s", scheme, lk);
        url_copy(out, max, tmp, n);
        return;
    }
    char path[2300];
    int hostpart = ae;           // base[0..ae) = scheme://host
    if (lk[0] == '/') {
        int n = snprintf(path, sizeof(path), "%.*s%s", hostpart, base, lk);
        if (n >= (int)sizeof(path)) n = (int)sizeof(path) - 1;
        // fall through to dot-segment removal below
    } else if (lk[0] == '?') {
        int be = hostpart; while (base[be] && base[be] != '?' && base[be] != '#') be++;
        snprintf(path, sizeof(path), "%.*s%s", be, base, lk);
    } else {
        // relative path: strip the last segment of the base path
        int be = hostpart; while (base[be] && base[be] != '?' && base[be] != '#') be++;
        int slash = be;
        while (slash > hostpart && base[slash - 1] != '/') slash--;
        if (slash == hostpart) snprintf(path, sizeof(path), "%.*s/%s", hostpart, base, lk);
        else snprintf(path, sizeof(path), "%.*s%s", slash, base, lk);
    }
    // remove . and .. segments in the path part
    int ps = hostpart;
    int pe = ps; while (path[pe] && path[pe] != '?' && path[pe] != '#') pe++;
    char res[2300];
    int rn = 0;
    memcpy(res, path, (size_t)ps); rn = ps;
    int segs[128]; int ns = 0;
    int i = ps;
    while (i < pe) {
        // path[i] == '/'
        int j = i + 1;
        while (j < pe && path[j] != '/') j++;
        int sl2 = j - i - 1;
        if (sl2 == 1 && path[i + 1] == '.') { if (j >= pe) { res[rn++] = '/'; } }
        else if (sl2 == 2 && path[i + 1] == '.' && path[i + 2] == '.') {
            if (ns > 0) { rn = segs[--ns]; }
            if (j >= pe) res[rn++] = '/';
        } else {
            if (ns < 128) segs[ns++] = rn;
            if (rn + (j - i) < (int)sizeof(res) - 8) { memcpy(res + rn, path + i, (size_t)(j - i)); rn += j - i; }
        }
        i = j;
    }
    if (rn == ps) res[rn++] = '/';
    int tail = (int)strlen(path + pe);
    if (rn + tail < (int)sizeof(res)) { memcpy(res + rn, path + pe, (size_t)tail); rn += tail; }
    url_copy(out, max, res, rn);
}

// ================= data: addresses =================

static int b64_val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

uint8_t *web_data_url(const char *url, int *length, char *mime, int mime_max) {
    if (!w_starts(url, "data:")) return NULL;
    const char *q = url + 5;
    const char *comma = strchr(q, ',');
    if (!comma) return NULL;
    bool b64 = false;
    int mn = 0;
    if (mime && mime_max > 0) mime[0] = '\0';
    const char *meta_end = comma;
    for (const char *k = q; k < meta_end; k++) {
        if (*k == ';') {
            if (meta_end - k >= 7 && w_eq(k + 1, 6, "base64")) b64 = true;
            break;
        }
        if (mime && mn < mime_max - 1) mime[mn++] = *k;
    }
    if (mime && mime_max > 0) mime[mn] = '\0';
    const char *d = comma + 1;
    int dl = (int)strlen(d);
    uint8_t *out = malloc((size_t)dl + 4);
    if (!out) return NULL;
    int n = 0;
    if (b64) {
        int acc = 0, bits = 0;
        for (int i = 0; i < dl; i++) {
            int c = (unsigned char)d[i];
            if (c == '%' && i + 2 < dl && w_hex(d[i + 1]) >= 0 && w_hex(d[i + 2]) >= 0) { c = w_hex(d[i + 1]) * 16 + w_hex(d[i + 2]); i += 2; }
            int v = b64_val(c);
            if (v < 0) continue;
            acc = (acc << 6) | v; bits += 6;
            if (bits >= 8) { bits -= 8; out[n++] = (uint8_t)((acc >> bits) & 255); }
        }
    } else {
        for (int i = 0; i < dl; i++) {
            if (d[i] == '%' && i + 2 < dl && w_hex(d[i + 1]) >= 0 && w_hex(d[i + 2]) >= 0) { out[n++] = (uint8_t)(w_hex(d[i + 1]) * 16 + w_hex(d[i + 2])); i += 2; }
            else out[n++] = (uint8_t)d[i];
        }
    }
    out[n] = '\0';
    *length = n;
    return out;
}

// ================= The cache =================
//
// Style sheets and pictures that have been fetched are kept here, by address,
// so that a page that is built again (a script changed it) or another page of
// the same site doesn't fetch them again. Pictures are kept already decoded; a
// page's picture then points at the cache's pixels (`shared`), so a page must
// be freed before the cache is trimmed or cleared.

struct centry {
    char *url;
    uint8_t kind;               // enum res_kind
    bool failed;
    char *data; int len;        // style sheets
    uint32_t *px; int w, h, nw, nh; bool alpha; struct svg_doc *svg;   // pictures
    size_t bytes;
    struct centry *next;
};
static struct centry *cache_head, *cache_tail;
static size_t cache_bytes;
#define CACHE_MAX (40u * 1024u * 1024u)
#define CACHE_ONE_MAX (8u * 1024u * 1024u)

static struct centry *cache_find(const char *url, int kind) {
    for (struct centry *e = cache_head; e; e = e->next)
        if ((e->kind == RES_IMAGE) == (kind == RES_IMAGE) && strcmp(e->url, url) == 0) return e;
    return NULL;
}

static void cache_drop_head(void) {
    struct centry *e = cache_head;
    if (!e) return;
    cache_head = e->next;
    if (!cache_head) cache_tail = NULL;
    cache_bytes -= e->bytes;
    free(e->url); free(e->data); free(e->px);
    if (e->svg) svg_free(e->svg);
    free(e);
}

void web_cache_clear(void) { while (cache_head) cache_drop_head(); cache_bytes = 0; }
void web_cache_trim(void) { while (cache_bytes > CACHE_MAX && cache_head) cache_drop_head(); }

static struct centry *cache_new(const char *url, int kind) {
    struct centry *e = calloc(1, sizeof(struct centry));
    if (!e) return NULL;
    e->url = malloc(strlen(url) + 1);
    if (!e->url) { free(e); return NULL; }
    memcpy(e->url, url, strlen(url) + 1);
    e->kind = (uint8_t)kind;
    e->bytes = sizeof(*e) + strlen(url);
    if (cache_tail) cache_tail->next = e; else cache_head = e;
    cache_tail = e;
    cache_bytes += e->bytes;
    return e;
}

static void cache_add_failed(const char *url, int kind) {
    if (cache_find(url, kind)) return;
    struct centry *e = cache_new(url, kind);
    if (e) e->failed = true;
}

static void cache_add_sheet(const char *url, const void *data, int len) {
    if (len > 1024 * 1024 || cache_find(url, RES_STYLESHEET)) return;
    struct centry *e = cache_new(url, RES_STYLESHEET);
    if (!e) return;
    e->data = malloc((size_t)len + 1);
    if (!e->data) { e->failed = true; return; }
    memcpy(e->data, data, (size_t)len);
    e->data[len] = '\0';
    e->len = len;
    e->bytes += (size_t)len;
    cache_bytes += (size_t)len;
}

// ================= Pictures =================

struct wimage *web_image_get(struct page *p, const char *url) {
    char key[2100];
    int kl = 0;
    while (url[kl] && url[kl] != '#' && kl < 2090) { key[kl] = url[kl]; kl++; }
    if (w_starts(url, "data:")) { kl = (int)strlen(url); if (kl > 2090) kl = 2090; memcpy(key, url, (size_t)kl); }
    key[kl] = '\0';
    // data: addresses can be huge; compare cheaply
    for (struct wimage *im = p->images; im; im = im->next) if (im->url && strcmp(im->url, key) == 0) return im;
    struct wimage *im = w_alloc(&p->arena, sizeof(struct wimage));
    if (!im) return NULL;
    // for data: addresses, keep the whole thing (it is the picture)
    im->url = w_strdup(&p->arena, key, kl);
    im->state = IMG_WAITING;
    im->next = p->images; p->images = im;
    if (w_starts(key, "data:")) {
        int n; char mime[64];
        uint8_t *bytes = web_data_url(url, &n, mime, sizeof(mime));
        if (bytes) {
            struct wimage *dec = img_decode(bytes, n, 2400);
            free(bytes);
            if (dec) {
                im->w = dec->w; im->h = dec->h; im->natural_w = dec->natural_w; im->natural_h = dec->natural_h;
                im->px = dec->px; im->has_alpha = dec->has_alpha; im->svg = dec->svg;
                im->state = IMG_READY;
                free(dec);
            } else im->state = IMG_FAILED;
        } else im->state = IMG_FAILED;
        return im;
    }
    struct centry *ce = cache_find(im->url, RES_IMAGE);
    if (ce) {
        if (ce->failed || !ce->px) { im->state = IMG_FAILED; return im; }
        im->w = ce->w; im->h = ce->h; im->natural_w = ce->nw; im->natural_h = ce->nh;
        im->px = ce->px; im->has_alpha = ce->alpha; im->svg = ce->svg;
        im->shared = true;
        im->state = IMG_READY;
        return im;
    }
    struct resource *r = web_queue_resource(p, im->url, RES_IMAGE, NULL);
    if (r) r->image = im;
    return im;
}

void web_image_finish(struct wimage *img) { (void)img; }

const uint32_t *web_image_scaled(struct wimage *img, int w, int h) {
    if (!img || !img->px || w <= 0 || h <= 0) return NULL;
    if (w == img->w && h == img->h) return img->px;
    if (img->scaled && img->sw == w && img->sh == h) return img->scaled;
    if (img->svg) {
        // a vector picture is drawn again at the size it is shown
        if ((long long)w * h > 12000000LL) return NULL;
        uint32_t *o = calloc((size_t)w * h, 4);
        if (!o) return NULL;
        svg_render(img->svg, o, w, h, 0xFF000000u);
        free(img->scaled);
        img->scaled = o; img->sw = w; img->sh = h;
        return o;
    }
    if (w > img->w || h > img->h) return NULL;
    if ((long long)w * h > 12000000LL) return NULL;
    uint32_t *out = malloc((size_t)w * h * sizeof(uint32_t));
    if (!out) return NULL;
    // area average, weighting colours by alpha
    int iw = img->w, ih = img->h;
    for (int y = 0; y < h; y++) {
        int sy0 = (int)((long long)y * ih / h), sy1 = (int)((long long)(y + 1) * ih / h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < w; x++) {
            int sx0 = (int)((long long)x * iw / w), sx1 = (int)((long long)(x + 1) * iw / w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned long long a = 0, r = 0, g = 0, b = 0;
            int cnt = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint32_t *row = img->px + (size_t)yy * iw;
                for (int xx = sx0; xx < sx1; xx++) {
                    uint32_t c = row[xx];
                    unsigned al = c >> 24;
                    a += al; r += ((c >> 16) & 255) * al; g += ((c >> 8) & 255) * al; b += (c & 255) * al; cnt++;
                }
            }
            if (a == 0) { out[(size_t)y * w + x] = 0; continue; }
            uint32_t oa = (uint32_t)(a / (unsigned long long)cnt);
            out[(size_t)y * w + x] = (oa << 24) | ((uint32_t)(r / a) << 16) | ((uint32_t)(g / a) << 8) | (uint32_t)(b / a);
        }
    }
    free(img->scaled);
    img->scaled = out; img->sw = w; img->sh = h;
    return out;
}

void img_free_pixels(struct wimage *img) {
    if (!img->shared) free(img->px);
    img->px = NULL;
    free(img->scaled); img->scaled = NULL;
    if (img->svg && !img->shared) svg_free(img->svg);
    img->svg = NULL;
}

// ================= Files to fetch =================

struct resource *web_queue_resource(struct page *p, const char *url, int kind, struct sheet *sheet) {
    if (!url || !*url) return NULL;
    if (kind == RES_IMAGE) {
        for (struct resource *r = p->resources; r; r = r->next) if (r->kind == RES_IMAGE && strcmp(r->url, url) == 0) return r;
    }
    struct resource *r = w_alloc(&p->arena, sizeof(struct resource));
    if (!r) return NULL;
    r->url = w_strdup(&p->arena, url, (int)strlen(url));
    r->kind = (uint8_t)kind;
    r->state = RS_WANTED;
    r->sheet = sheet;
    r->order = p->resource_count++;
    if (p->last_resource) p->last_resource->next = r; else p->resources = r;
    p->last_resource = r;
    if (kind != RES_IMAGE && !w_starts(url, "data:")) {
        struct centry *ce = cache_find(url, kind);
        if (ce) {
            r->state = RS_REQUESTED;
            if (ce->failed) web_resource_arrived(p, r, NULL, 0, NULL);
            else web_resource_arrived(p, r, ce->data, ce->len, NULL);
            return r;
        }
    }
    if (w_starts(url, "data:") && kind != RES_IMAGE) {
        int n; char mime[64];
        uint8_t *bytes = web_data_url(url, &n, mime, sizeof(mime));
        r->state = RS_REQUESTED;
        web_resource_arrived(p, r, bytes, n, mime);
        free(bytes);
    }
    return r;
}

struct resource *web_next_resource(struct page *p) {
    // style sheets first: nothing can be laid out right until they are in
    for (int pass = 0; pass < 2; pass++) {
        for (struct resource *r = p->resources; r; r = r->next) {
            if (r->state != RS_WANTED) continue;
            bool sheet = r->kind != RES_IMAGE;
            if ((pass == 0) != sheet) continue;
            r->state = RS_REQUESTED;
            if (r->image) r->image->state = IMG_LOADING;
            return r;
        }
    }
    return NULL;
}

void web_resource_arrived(struct page *p, struct resource *r, const void *data, int length, const char *content_type) {
    (void)content_type;
    if (!data || length <= 0) {
        r->state = RS_FAILED;
        if (!w_starts(r->url, "data:")) cache_add_failed(r->url, r->kind);
        if (r->image) r->image->state = IMG_FAILED;
        if (r->kind != RES_IMAGE) p->needs_layout = true;
        return;
    }
    r->state = RS_DONE;
    if (r->kind == RES_IMAGE) {
        struct wimage *im = r->image;
        if (!im) return;
        struct wimage *dec = img_decode(data, length, 2400);
        if (!dec) { im->state = IMG_FAILED; cache_add_failed(r->url, RES_IMAGE); return; }
        if (!im->shared) free(im->px);
        free(im->scaled); im->scaled = NULL;
        im->w = dec->w; im->h = dec->h; im->natural_w = dec->natural_w; im->natural_h = dec->natural_h;
        im->px = dec->px; im->has_alpha = dec->has_alpha; im->svg = dec->svg;
        im->shared = false;
        im->state = IMG_READY;
        free(dec);
        size_t pb = (size_t)im->w * (size_t)im->h * 4;
        if (im->px && pb <= CACHE_ONE_MAX && !cache_find(r->url, RES_IMAGE)) {
            struct centry *ce = cache_new(r->url, RES_IMAGE);
            if (ce) {
                ce->px = im->px; ce->w = im->w; ce->h = im->h; ce->nw = im->natural_w; ce->nh = im->natural_h;
                ce->alpha = im->has_alpha; ce->svg = im->svg; ce->bytes += pb; cache_bytes += pb;
                im->shared = true;
            }
        }
        p->needs_layout = true;
        return;
    }
    // a style sheet (or an @import)
    if (!r->sheet) return;
    char *copy = w_alloc(&p->arena, (size_t)length + 1);
    if (!copy) return;
    memcpy(copy, data, (size_t)length);
    cache_add_sheet(r->url, data, length);
    // windows-1252 style sheets: rare; take it as is
    css_sheet_fill(p, r->sheet, copy, length, r->url);
    p->needs_layout = true;
}

bool web_styles_ready(const struct page *p) {
    for (const struct resource *r = p->resources; r; r = r->next)
        if (r->kind != RES_IMAGE && (r->state == RS_WANTED || r->state == RS_REQUESTED)) return false;
    return true;
}

int web_pending_count(const struct page *p, bool images_too) {
    int n = 0;
    for (const struct resource *r = p->resources; r; r = r->next) {
        if (r->state != RS_WANTED && r->state != RS_REQUESTED) continue;
        if (r->kind == RES_IMAGE && !images_too) continue;
        n++;
    }
    return n;
}

// ================= Loading =================

struct page *web_page_new(void) {
    struct page *p = calloc(1, sizeof(struct page));
    if (p) p->focus_cur = -1;
    return p;
}

void web_page_free(struct page *p) {
    if (!p) return;
    for (struct wimage *im = p->images; im; im = im->next) img_free_pixels(im);
    svg_cache_free(p);
    free(p->focus_nodes);
    w_arena_free(&p->larena);
    w_arena_free(&p->arena);
    css_reset(p);
    free(p);
}

static const struct { uint16_t cp; } cp1252_hi[32] = {
    {0x20AC},{0x81},{0x201A},{0x0192},{0x201E},{0x2026},{0x2020},{0x2021},{0x02C6},{0x2030},{0x0160},{0x2039},{0x0152},{0x8D},{0x017D},{0x8F},
    {0x90},{0x2018},{0x2019},{0x201C},{0x201D},{0x2022},{0x2013},{0x2014},{0x02DC},{0x2122},{0x0161},{0x203A},{0x0153},{0x9D},{0x017E},{0x0178}
};

static bool valid_utf8(const unsigned char *s, int n) {
    for (int i = 0; i < n;) {
        unsigned c = s[i];
        if (c < 0x80) { i++; continue; }
        int extra = (c >= 0xF0 && c < 0xF5) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC2) ? 1 : -1;
        if (c >= 0xF5 || extra < 0 || i + extra >= n) return false;
        for (int k = 1; k <= extra; k++) if ((s[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

static bool charset_is_latin1(const char *html, int len) {
    int lim = len < 4096 ? len : 4096;
    for (int i = 0; i + 8 < lim; i++) {
        if (w_eq(html + i, 7, "charset")) {
            int j = i + 7;
            while (j < lim && (w_is_space(html[j]) || html[j] == '=' || html[j] == '"' || html[j] == '\'')) j++;
            if (w_starts(html + j, "utf-8") || w_starts(html + j, "utf8")) return false;
            if (w_starts(html + j, "iso-8859-1") || w_starts(html + j, "latin1") || w_starts(html + j, "windows-1252") || w_starts(html + j, "us-ascii") || w_starts(html + j, "iso-8859-15"))
                return true;
            return false;
        }
    }
    return !valid_utf8((const unsigned char *)html, len);
}

static char *latin1_to_utf8(const char *s, int n, int *out_n) {
    char *o = malloc((size_t)n * 3 + 4);
    if (!o) return NULL;
    int k = 0;
    for (int i = 0; i < n; i++) {
        unsigned c = (unsigned char)s[i];
        uint32_t cp = c;
        if (c >= 0x80 && c < 0xA0) cp = cp1252_hi[c - 0x80].cp;
        k += w_utf8_put(cp, o + k);
    }
    o[k] = '\0';
    *out_n = k;
    return o;
}

static bool media_is_print_only(const char *m) {
    if (!m || !*m) return false;
    bool print = false;
    for (const char *q = m; *q; q++) if (w_starts(q, "print")) print = true;
    if (!print) return false;
    for (const char *q = m; *q; q++) if (w_starts(q, "screen") || w_starts(q, "all")) return false;
    return true;
}

static void add_sheet(struct page *p, struct sheet *sh) {
    sh->order = ++p->sheet_count;
    sh->next = NULL;
    if (p->last_sheet) p->last_sheet->next = sh; else p->sheets = sh;
    p->last_sheet = sh;
}

// The raw text inside an element (<style>).
static char *raw_text(struct page *p, const struct node *n, int *len, const char *media) {
    int total = 0;
    for (const struct node *c = n->first; c; c = c->next) if (c->type == N_TEXT) total += c->text_len;
    int ml = (media && *media) ? (int)strlen(media) : 0;
    char *buf = w_alloc(&p->arena, (size_t)total + (size_t)ml + 16);
    if (!buf) { *len = 0; return NULL; }
    int k = 0;
    if (ml) { memcpy(buf, "@media ", 7); k = 7; memcpy(buf + k, media, (size_t)ml); k += ml; buf[k++] = '{'; }
    for (const struct node *c = n->first; c; c = c->next) if (c->type == N_TEXT) { memcpy(buf + k, c->text, (size_t)c->text_len); k += c->text_len; }
    if (ml) buf[k++] = '}';
    *len = k;
    return buf;
}

static void collect(struct page *p, struct node *n, int depth) {
    if (depth > 200) return;
    for (struct node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEMENT) continue;
        if (c->tag == T_STYLE) {
            const char *type = node_attr(c, "type");
            const char *media = node_attr(c, "media");
            if ((type && *type && !w_starts(type, "text/css")) || media_is_print_only(media)) continue;
            bool wrap = media && *media && !w_eq0(media, "all") && !w_eq0(media, "screen");
            struct sheet *sh = css_sheet_new(p);
            int len; char *txt = raw_text(p, c, &len, wrap ? media : NULL);
            sh->owner = c;
            add_sheet(p, sh);
            if (txt) css_sheet_fill(p, sh, txt, len, p->base_url);
            continue;
        }
        if (c->tag == T_LINK) {
            const char *rel = node_attr(c, "rel"), *href = node_attr(c, "href"), *media = node_attr(c, "media");
            if (!rel || !href || !*href || node_has_attr(c, "disabled")) continue;
            bool is_sheet = false, alt = false;
            for (const char *q = rel; *q; q++) { if (w_starts(q, "stylesheet")) is_sheet = true; if (w_starts(q, "alternate")) alt = true; }
            if (!is_sheet || alt || media_is_print_only(media)) continue;
            char full[2048];
            web_url_resolve(p->base_url, href, full, sizeof(full));
            struct sheet *sh = css_sheet_new(p);
            sh->owner = c;
            sh->base_url = w_strdup(&p->arena, full, (int)strlen(full));
            add_sheet(p, sh);
            struct resource *r = web_queue_resource(p, sh->base_url, RES_STYLESHEET, sh);
            (void)r;
            // media="(min-width: ...)" on the link: remembered by wrapping the text when it arrives
            continue;
        }
        collect(p, c, depth + 1);
    }
}

void web_page_load(struct page *p, const char *html, int length, const char *doc_url, bool scripting) {
    css_reset(p);
    p->scripting = scripting;
    p->doc_url = w_strdup(&p->arena, doc_url ? doc_url : "", doc_url ? (int)strlen(doc_url) : 0);
    p->base_url = p->doc_url;
    char *conv = NULL;
    if (charset_is_latin1(html, length)) {
        int n;
        conv = latin1_to_utf8(html, length, &n);
        if (conv) { html = conv; length = n; }
    }
    p->root = web_parse_html(p, html, length, scripting);
    free(conv);
    if (!p->root) return;
    // <base href>
    if (p->head) for (struct node *c = p->head->first; c; c = c->next) {
        if (c->tag == T_BASE) {
            const char *h = node_attr(c, "href");
            if (h && *h) { char full[2048]; web_url_resolve(p->doc_url, h, full, sizeof(full)); p->base_url = w_strdup(&p->arena, full, (int)strlen(full)); }
            break;
        }
    }
    collect(p, p->root, 0);
    p->needs_layout = true;
    p->generation++;
}

// ================= Layout =================

static void reset_nodes(struct node *n) {
    // iterative walk
    struct node *c = n;
    while (c) {
        c->style = NULL; c->before = c->after = NULL; c->box = NULL; c->focusable = -1;
        if (c->first) { c = c->first; continue; }
        while (c && !c->next) { c = c->parent; if (c == n->parent) return; }
        if (!c) break;
        c = c->next;
    }
}

void web_page_layout(struct page *p, int width, int height, bool dark) {
    if (!p->root) return;
    w_arena_free(&p->larena);
    reset_nodes(p->root);
    p->dark = dark;
    p->viewport_w = width; p->viewport_h = height;
    p->root_box = NULL;
    style_compute_all(p);
    layout_page(p, width, height);
    p->needs_layout = false;
}

// ================= Pictures drawn from <svg> (filled in by the vector picture code) =================

__attribute__((weak)) const uint32_t *web_box_svg(struct page *p, struct box *b, int w, int h) { (void)p; (void)b; (void)w; (void)h; return NULL; }
__attribute__((weak)) struct svg_doc *svg_parse_text(const char *text, int length) { (void)text; (void)length; return NULL; }
__attribute__((weak)) struct svg_doc *svg_from_node(struct page *p, struct node *n) { (void)p; (void)n; return NULL; }
__attribute__((weak)) void svg_size(struct svg_doc *d, int *w, int *h) { (void)d; *w = *h = 0; }
__attribute__((weak)) void svg_free(struct svg_doc *d) { (void)d; }
__attribute__((weak)) bool svg_node_size(struct node *n, int *w, int *h) { (void)n; (void)w; (void)h; return false; }
__attribute__((weak)) void svg_cache_free(struct page *p) { (void)p; }
__attribute__((weak)) void svg_render(struct svg_doc *d, uint32_t *out, int w, int h, uint32_t cc) { (void)d; (void)out; (void)w; (void)h; (void)cc; }
__attribute__((weak)) struct wimage *img_decode(const void *data, int length, int max_dim) { (void)data; (void)length; (void)max_dim; return NULL; }
