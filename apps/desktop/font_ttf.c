// font_ttf.c - draws letters from the built-in TrueType fonts.
//
// The hard part (reading TrueType files and turning their curves into
// pixels) is stb_truetype, by Sean Barrett (public domain). It uses floating
// point, so this one file is built with floating point switched on; the rest
// of the browser only sees whole numbers (see web_font.h).

#include "web_font.h"
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(x, u) ((void)(u), malloc(x))
#define STBTT_free(x, u) ((void)(u), free(x))
#define STBTT_assert(x) ((void)0)
#define STBTT_ifloor(x) ((int)floor(x))
#define STBTT_iceil(x) ((int)ceil(x))
#define STBTT_sqrt(x) sqrt(x)
#define STBTT_pow(x, y) pow(x, y)
#define STBTT_fmod(x, y) fmod(x, y)
#define STBTT_cos(x) cos(x)
#define STBTT_acos(x) acos(x)
#define STBTT_fabs(x) fabs(x)
#define STBTT_strlen(x) strlen(x)
#define STBTT_memcpy memcpy
#define STBTT_memset memset
#include "stb_truetype.h"

// The font files (fonts.S): pairs of (address, size).
extern const struct { const unsigned char *data; unsigned long size; } wfont_data_table[];

enum { F_SANS_R, F_SANS_B, F_SANS_I, F_SANS_BI, F_SERIF_R, F_SERIF_B, F_SERIF_I, F_MONO_R, F_MONO_B,
       F_SYM_R, F_SYM_B, F_COUNT };

static stbtt_fontinfo infos[F_COUNT];
static bool loaded[F_COUNT], failed[F_COUNT];
static int upem[F_COUNT];
static int vasc[F_COUNT], vdesc[F_COUNT], vgap[F_COUNT];

static bool load(int f) {
    if (loaded[f]) return true;
    if (failed[f]) return false;
    const unsigned char *data = wfont_data_table[f].data;
    int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || !stbtt_InitFont(&infos[f], data, offset)) { failed[f] = true; return false; }
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&infos[f], &asc, &desc, &gap);
    vasc[f] = asc; vdesc[f] = desc; vgap[f] = gap;
    // units per em: scale for 1000 pixels tells us.
    float s = stbtt_ScaleForMappingEmToPixels(&infos[f], 1000.0f);
    upem[f] = (int)(1000.0f / s + 0.5f);
    loaded[f] = true;
    return true;
}

int wfont_pick(int family, bool bold, bool italic) {
    switch (family) {
    case WF_SERIF: return bold ? F_SERIF_B : (italic ? F_SERIF_I : F_SERIF_R);
    case WF_MONO: return bold ? F_MONO_B : F_MONO_R;
    default: return bold ? (italic ? F_SANS_BI : F_SANS_B) : (italic ? F_SANS_I : F_SANS_R);
    }
}

// ---- Which font and glyph draws a character ----

struct resolved { uint32_t cp; int8_t font; uint16_t glyph; int advance; };   // advance in font units
#define RES_SLOTS 4096
static struct resolved res_cache[F_COUNT][RES_SLOTS / 8];

static bool is_invisible(uint32_t cp) {
    return cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0x2060 || cp == 0xFEFF || cp == 0xAD
        || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x202A && cp <= 0x202E) || cp == 0x200E || cp == 0x200F;
}

static const struct resolved *resolve(int font, uint32_t cp) {
    unsigned slot = (cp * 2654435761u >> 8) % (RES_SLOTS / 8);
    struct resolved *r = &res_cache[font][slot];
    if (r->cp == cp + 1) return r;
    // Not cached: look it up.
    r->cp = cp + 1;
    r->font = (int8_t)font;
    r->glyph = 0;
    r->advance = 0;
    if (!load(font)) return r;
    uint32_t look = cp;
    if (cp == 0xA0 || cp == 0x2009 || cp == 0x2002 || cp == 0x2003 || cp == 0x202F || cp == 0x2007 || cp == 0x2008 || cp == 0x200A) look = ' ';
    if (is_invisible(cp)) { r->glyph = 0; r->advance = 0; r->font = -1; return r; }
    int g = stbtt_FindGlyphIndex(&infos[font], (int)look);
    int use = font;
    if (g == 0) {
        int sym = (font == F_SANS_B || font == F_SANS_BI || font == F_SERIF_B || font == F_MONO_B) ? F_SYM_B : F_SYM_R;
        if (load(sym)) {
            int g2 = stbtt_FindGlyphIndex(&infos[sym], (int)look);
            if (g2) { g = g2; use = sym; }
        }
    }
    if (g == 0) {
        // Nothing has it: a gap about half an em wide with nothing drawn.
        r->font = -2;
        r->advance = upem[font] / 2;
        return r;
    }
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&infos[use], g, &adv, &lsb);
    if (cp == '\t') adv = upem[use] / 2;
    // Advances are measured in the font that drew them, scaled to this font's em.
    r->font = (int8_t)use;
    r->glyph = (uint16_t)g;
    r->advance = adv * upem[font] / upem[use];
    return r;
}

void wfont_vmetrics(int font, int px, int *ascent, int *descent, int *line_gap) {
    if (!load(font)) { *ascent = px; *descent = px / 4; *line_gap = 0; return; }
    int a = (vasc[font] * px * 2 + upem[font]) / (2 * upem[font]);
    int d = (-vdesc[font] * px * 2 + upem[font]) / (2 * upem[font]);
    int g = (vgap[font] * px * 2 + upem[font]) / (2 * upem[font]);
    *ascent = a; *descent = d; *line_gap = g;
}

int wfont_advance(int font, int px, uint32_t cp) {
    if (cp == '\n' || cp == '\r') return 0;
    const struct resolved *r = resolve(font, cp);
    if (!load(font)) return px * 32;
    return (int)((int64_t)r->advance * px * 64 / upem[font]);
}

bool wfont_has(int font, uint32_t cp) {
    const struct resolved *r = resolve(font, cp);
    return r->font >= 0 && r->glyph != 0;
}

int wfont_text_width(int font, int px, const char *text, int length) {
    const char *p = text, *end = text + length;
    int total = 0;
    while (p < end) total += wfont_advance(font, px, w_utf8_next(&p, end));
    return total;
}

// ---- Letter pictures ----

struct gentry { uint32_t key_a, key_b; struct wglyph g; bool used; };
#define GSLOTS 8192
static struct gentry *gtable;
static int gcount;
static struct w_arena garena;

const struct wglyph *wfont_glyph(int font, int px, uint32_t cp) {
    if (px < 1) px = 1;
    if (px > 400) px = 400;
    const struct resolved *r = resolve(font, cp);
    if (r->font < 0 || r->glyph == 0) return NULL;
    int use = r->font;
    if (!gtable) {
        gtable = calloc(GSLOTS, sizeof(struct gentry));
        if (!gtable) return NULL;
    }
    uint32_t ka = (uint32_t)r->glyph | ((uint32_t)use << 24), kb = (uint32_t)px;
    unsigned h = (ka * 2654435761u ^ kb * 40503u) % GSLOTS;
    for (int probe = 0; probe < 32; probe++) {
        struct gentry *e = &gtable[(h + probe) % GSLOTS];
        if (!e->used) { h = (h + probe) % GSLOTS; break; }
        if (e->key_a == ka && e->key_b == kb) return &e->g;
        if (probe == 31) { h = (h + probe) % GSLOTS; goto flush; }
    }
    if (gcount > GSLOTS / 2 || garena.total > 12u * 1024 * 1024) {
    flush:
        memset(gtable, 0, sizeof(struct gentry) * GSLOTS);
        w_arena_free(&garena);
        gcount = 0;
        h = (ka * 2654435761u ^ kb * 40503u) % GSLOTS;
    }
    struct gentry *e = &gtable[h];
    float scale = stbtt_ScaleForMappingEmToPixels(&infos[use], (float)px);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&infos[use], r->glyph, scale, scale, &x0, &y0, &x1, &y1);
    int w = x1 - x0, hh = y1 - y0;
    e->used = true;
    e->key_a = ka; e->key_b = kb;
    e->g.x = x0; e->g.y = y0; e->g.w = w; e->g.h = hh;
    e->g.alpha = NULL;
    if (w > 0 && hh > 0) {
        uint8_t *bits = w_alloc(&garena, (size_t)w * hh);
        if (bits) {
            stbtt_MakeGlyphBitmap(&infos[use], bits, w, hh, w, scale, scale, r->glyph);
            e->g.alpha = bits;
        }
    }
    gcount++;
    return &e->g;
}
