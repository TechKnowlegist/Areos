// web_dom.c - reading HTML into a tree of nodes.
//
// HTML is text with tags in angle brackets. This reads it the way browsers
// do, roughly: it knows which tags are empty (<br>, <img>), which end other
// tags by starting (a new <li> ends the last one), and that <script> and
// <style> hold plain text. It also adds the <html>, <head> and <body> a page
// forgot to say.

#include "web.h"

// ---- Tags ----

static const struct { const char *name; uint8_t tag; } tag_table[] = {
    { "a", T_A }, { "abbr", T_ABBR }, { "address", T_ADDRESS }, { "area", T_AREA }, { "article", T_ARTICLE },
    { "aside", T_ASIDE }, { "audio", T_AUDIO }, { "b", T_B }, { "base", T_BASE }, { "big", T_BIG },
    { "blockquote", T_BLOCKQUOTE }, { "body", T_BODY }, { "br", T_BR }, { "button", T_BUTTON },
    { "canvas", T_CANVAS }, { "caption", T_CAPTION }, { "center", T_CENTER }, { "cite", T_CITE },
    { "code", T_CODE }, { "col", T_COL }, { "colgroup", T_COLGROUP }, { "datalist", T_DATALIST }, { "dd", T_DD },
    { "del", T_DEL }, { "details", T_DETAILS }, { "dfn", T_DFN }, { "dialog", T_DIALOG }, { "div", T_DIV },
    { "dl", T_DL }, { "dt", T_DT }, { "em", T_EM }, { "embed", T_EMBED }, { "fieldset", T_FIELDSET },
    { "figcaption", T_FIGCAPTION }, { "figure", T_FIGURE }, { "font", T_FONT }, { "footer", T_FOOTER },
    { "form", T_FORM }, { "frame", T_FRAME }, { "frameset", T_FRAMESET }, { "h1", T_H1 }, { "h2", T_H2},
    { "h3", T_H3 }, { "h4", T_H4 }, { "h5", T_H5 }, { "h6", T_H6 }, { "head", T_HEAD }, { "header", T_HEADER },
    { "hgroup", T_HGROUP }, { "hr", T_HR }, { "html", T_HTML }, { "i", T_I }, { "iframe", T_IFRAME },
    { "img", T_IMG }, { "input", T_INPUT }, { "ins", T_INS }, { "kbd", T_KBD }, { "label", T_LABEL },
    { "legend", T_LEGEND }, { "li", T_LI }, { "link", T_LINK }, { "main", T_MAIN }, { "map", T_MAP },
    { "mark", T_MARK }, { "menu", T_MENU }, { "meta", T_META }, { "meter", T_METER }, { "nav", T_NAV },
    { "nobr", T_NOBR }, { "noframes", T_NOFRAMES }, { "noscript", T_NOSCRIPT }, { "object", T_OBJECT },
    { "ol", T_OL }, { "optgroup", T_OPTGROUP }, { "option", T_OPTION }, { "output", T_OUTPUT }, { "p", T_P },
    { "param", T_PARAM }, { "picture", T_PICTURE }, { "pre", T_PRE }, { "progress", T_PROGRESS }, { "q", T_Q },
    { "s", T_S }, { "samp", T_SAMP }, { "script", T_SCRIPT }, { "section", T_SECTION }, { "select", T_SELECT },
    { "slot", T_SLOT }, { "small", T_SMALL }, { "source", T_SOURCE }, { "span", T_SPAN }, { "strike", T_STRIKE },
    { "strong", T_STRONG }, { "style", T_STYLE }, { "sub", T_SUB }, { "summary", T_SUMMARY }, { "sup", T_SUP },
    { "svg", T_SVG }, { "table", T_TABLE }, { "tbody", T_TBODY }, { "td", T_TD }, { "template", T_TEMPLATE },
    { "textarea", T_TEXTAREA }, { "tfoot", T_TFOOT }, { "th", T_TH }, { "thead", T_THEAD }, { "time", T_TIME },
    { "title", T_TITLE }, { "tr", T_TR }, { "track", T_TRACK }, { "tt", T_TT }, { "u", T_U }, { "ul", T_UL },
    { "var", T_VAR }, { "video", T_VIDEO }, { "wbr", T_WBR },
};

enum tag tag_from_name(const char *name, int length) {
    for (unsigned i = 0; i < sizeof(tag_table) / sizeof(tag_table[0]); i++) {
        if (w_eq(name, length, tag_table[i].name)) return (enum tag)tag_table[i].tag;
    }
    return T_OTHER;
}

// ---- Entities (&amp; and friends) ----

static const struct { const char *name; uint32_t cp; } entity_table[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
    { "iexcl", 0xA1 }, { "cent", 0xA2 }, { "pound", 0xA3 }, { "curren", 0xA4 }, { "yen", 0xA5 }, { "brvbar", 0xA6 },
    { "sect", 0xA7 }, { "uml", 0xA8 }, { "copy", 0xA9 }, { "ordf", 0xAA }, { "laquo", 0xAB }, { "not", 0xAC },
    { "shy", 0xAD }, { "reg", 0xAE }, { "macr", 0xAF }, { "deg", 0xB0 }, { "plusmn", 0xB1 }, { "sup2", 0xB2 },
    { "sup3", 0xB3 }, { "acute", 0xB4 }, { "micro", 0xB5 }, { "para", 0xB6 }, { "middot", 0xB7 }, { "cedil", 0xB8 },
    { "sup1", 0xB9 }, { "ordm", 0xBA }, { "raquo", 0xBB }, { "frac14", 0xBC }, { "frac12", 0xBD }, { "frac34", 0xBE },
    { "iquest", 0xBF }, { "Agrave", 0xC0 }, { "Aacute", 0xC1 }, { "Acirc", 0xC2 }, { "Atilde", 0xC3 }, { "Auml", 0xC4 },
    { "Aring", 0xC5 }, { "AElig", 0xC6 }, { "Ccedil", 0xC7 }, { "Egrave", 0xC8 }, { "Eacute", 0xC9 }, { "Ecirc", 0xCA },
    { "Euml", 0xCB }, { "Igrave", 0xCC }, { "Iacute", 0xCD }, { "Icirc", 0xCE }, { "Iuml", 0xCF }, { "ETH", 0xD0 },
    { "Ntilde", 0xD1 }, { "Ograve", 0xD2 }, { "Oacute", 0xD3 }, { "Ocirc", 0xD4 }, { "Otilde", 0xD5 }, { "Ouml", 0xD6 },
    { "times", 0xD7 }, { "Oslash", 0xD8 }, { "Ugrave", 0xD9 }, { "Uacute", 0xDA }, { "Ucirc", 0xDB }, { "Uuml", 0xDC },
    { "Yacute", 0xDD }, { "THORN", 0xDE }, { "szlig", 0xDF }, { "agrave", 0xE0 }, { "aacute", 0xE1 }, { "acirc", 0xE2 },
    { "atilde", 0xE3 }, { "auml", 0xE4 }, { "aring", 0xE5 }, { "aelig", 0xE6 }, { "ccedil", 0xE7 }, { "egrave", 0xE8 },
    { "eacute", 0xE9 }, { "ecirc", 0xEA }, { "euml", 0xEB }, { "igrave", 0xEC }, { "iacute", 0xED }, { "icirc", 0xEE },
    { "iuml", 0xEF }, { "eth", 0xF0 }, { "ntilde", 0xF1 }, { "ograve", 0xF2 }, { "oacute", 0xF3 }, { "ocirc", 0xF4 },
    { "otilde", 0xF5 }, { "ouml", 0xF6 }, { "divide", 0xF7 }, { "oslash", 0xF8 }, { "ugrave", 0xF9 }, { "uacute", 0xFA },
    { "ucirc", 0xFB }, { "uuml", 0xFC }, { "yacute", 0xFD }, { "thorn", 0xFE }, { "yuml", 0xFF },
    { "OElig", 0x152 }, { "oelig", 0x153 }, { "Scaron", 0x160 }, { "scaron", 0x161 }, { "Yuml", 0x178 }, { "fnof", 0x192 },
    { "circ", 0x2C6 }, { "tilde", 0x2DC },
    { "Alpha", 0x391 }, { "Beta", 0x392 }, { "Gamma", 0x393 }, { "Delta", 0x394 }, { "Epsilon", 0x395 }, { "Zeta", 0x396 },
    { "Eta", 0x397 }, { "Theta", 0x398 }, { "Iota", 0x399 }, { "Kappa", 0x39A }, { "Lambda", 0x39B }, { "Mu", 0x39C },
    { "Nu", 0x39D }, { "Xi", 0x39E }, { "Omicron", 0x39F }, { "Pi", 0x3A0 }, { "Rho", 0x3A1 }, { "Sigma", 0x3A3 },
    { "Tau", 0x3A4 }, { "Upsilon", 0x3A5 }, { "Phi", 0x3A6 }, { "Chi", 0x3A7 }, { "Psi", 0x3A8 }, { "Omega", 0x3A9 },
    { "alpha", 0x3B1 }, { "beta", 0x3B2 }, { "gamma", 0x3B3 }, { "delta", 0x3B4 }, { "epsilon", 0x3B5 }, { "zeta", 0x3B6 },
    { "eta", 0x3B7 }, { "theta", 0x3B8 }, { "iota", 0x3B9 }, { "kappa", 0x3BA }, { "lambda", 0x3BB }, { "mu", 0x3BC },
    { "nu", 0x3BD }, { "xi", 0x3BE }, { "omicron", 0x3BF }, { "pi", 0x3C0 }, { "rho", 0x3C1 }, { "sigmaf", 0x3C2 },
    { "sigma", 0x3C3 }, { "tau", 0x3C4 }, { "upsilon", 0x3C5 }, { "phi", 0x3C6 }, { "chi", 0x3C7 }, { "psi", 0x3C8 },
    { "omega", 0x3C9 }, { "thetasym", 0x3D1 }, { "upsih", 0x3D2 }, { "piv", 0x3D6 },
    { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "thinsp", 0x2009 }, { "zwnj", 0x200C }, { "zwj", 0x200D }, { "lrm", 0x200E },
    { "rlm", 0x200F }, { "ndash", 0x2013 }, { "mdash", 0x2014 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
    { "sbquo", 0x201A }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bdquo", 0x201E }, { "dagger", 0x2020 },
    { "Dagger", 0x2021 }, { "bull", 0x2022 }, { "hellip", 0x2026 }, { "permil", 0x2030 }, { "prime", 0x2032 },
    { "Prime", 0x2033 }, { "lsaquo", 0x2039 }, { "rsaquo", 0x203A }, { "oline", 0x203E }, { "frasl", 0x2044 },
    { "euro", 0x20AC }, { "image", 0x2111 }, { "weierp", 0x2118 }, { "real", 0x211C }, { "trade", 0x2122 },
    { "alefsym", 0x2135 }, { "larr", 0x2190 }, { "uarr", 0x2191 }, { "rarr", 0x2192 }, { "darr", 0x2193 },
    { "harr", 0x2194 }, { "crarr", 0x21B5 }, { "lArr", 0x21D0 }, { "uArr", 0x21D1 }, { "rArr", 0x21D2 },
    { "dArr", 0x21D3 }, { "hArr", 0x21D4 }, { "forall", 0x2200 }, { "part", 0x2202 }, { "exist", 0x2203 },
    { "empty", 0x2205 }, { "nabla", 0x2207 }, { "isin", 0x2208 }, { "notin", 0x2209 }, { "ni", 0x220B },
    { "prod", 0x220F }, { "sum", 0x2211 }, { "minus", 0x2212 }, { "lowast", 0x2217 }, { "radic", 0x221A },
    { "prop", 0x221D }, { "infin", 0x221E }, { "ang", 0x2220 }, { "and", 0x2227 }, { "or", 0x2228 }, { "cap", 0x2229 },
    { "cup", 0x222A }, { "int", 0x222B }, { "there4", 0x2234 }, { "sim", 0x223C }, { "cong", 0x2245 },
    { "asymp", 0x2248 }, { "ne", 0x2260 }, { "equiv", 0x2261 }, { "le", 0x2264 }, { "ge", 0x2265 }, { "sub", 0x2282 },
    { "sup", 0x2283 }, { "nsub", 0x2284 }, { "sube", 0x2286 }, { "supe", 0x2287 }, { "oplus", 0x2295 },
    { "otimes", 0x2297 }, { "perp", 0x22A5 }, { "sdot", 0x22C5 }, { "lceil", 0x2308 }, { "rceil", 0x2309 },
    { "lfloor", 0x230A }, { "rfloor", 0x230B }, { "lang", 0x27E8 }, { "rang", 0x27E9 }, { "loz", 0x25CA },
    { "spades", 0x2660 }, { "clubs", 0x2663 }, { "hearts", 0x2665 }, { "diams", 0x2666 }, { "check", 0x2713 },
    { "star", 0x2606 }, { "starf", 0x2605 }, { "phone", 0x260E }, { "cross", 0x2717 }, { "hyphen", 0x2010 },
    { "dash", 0x2010 }, { "horbar", 0x2015 }, { "nldr", 0x2025 }, { "numsp", 0x2007 }, { "puncsp", 0x2008 },
    { "hairsp", 0x200A }, { "NewLine", '\n' }, { "Tab", '\t' }, { "colon", ':' }, { "comma", ',' }, { "period", '.' },
    { "semi", ';' }, { "excl", '!' }, { "quest", '?' }, { "num", '#' }, { "dollar", '$' }, { "percnt", '%' },
    { "lpar", '(' }, { "rpar", ')' }, { "ast", '*' }, { "plus", '+' }, { "sol", '/' }, { "equals", '=' },
    { "lsqb", '[' }, { "rsqb", ']' }, { "lbrace", '{' }, { "rbrace", '}' }, { "lcub", '{' }, { "rcub", '}' },
    { "vert", '|' }, { "verbar", '|' }, { "bsol", '\\' }, { "lowbar", '_' }, { "grave", '`' }, { "Hat", '^' },
    { "commat", '@' }, { "midast", '*' }, { "approx", 0x2248 }, { "leq", 0x2264 }, { "geq", 0x2265 },
    { "rightarrow", 0x2192 }, { "leftarrow", 0x2190 }, { "uparrow", 0x2191 }, { "downarrow", 0x2193 },
    { "checkmark", 0x2713 }, { "hearts", 0x2665 }, { "bigstar", 0x2605 }, { "blank", 0x2423 },
};

// Read what's after '&'. Returns bytes used (including '&'), or 0 if it isn't an entity.
static int read_entity(const char *s, int length, uint32_t *cp) {
    if (length < 3 || s[0] != '&') return 0;
    if (s[1] == '#') {
        int i = 2;
        uint32_t v = 0;
        bool any = false;
        if (i < length && (s[i] == 'x' || s[i] == 'X')) {
            i++;
            while (i < length && w_hex(s[i]) >= 0) { v = v * 16 + (uint32_t)w_hex(s[i]); i++; any = true; if (v > 0x10FFFF) v = 0x10FFFF; }
        } else {
            while (i < length && w_is_digit(s[i])) { v = v * 10 + (uint32_t)(s[i] - '0'); i++; any = true; if (v > 0x10FFFF) v = 0x10FFFF; }
        }
        if (!any) return 0;
        if (i < length && s[i] == ';') i++;
        if (v == 0 || (v >= 0xD800 && v <= 0xDFFF)) v = 0xFFFD;
        // Windows-1252 leftovers.
        static const uint16_t cp1252[32] = { 0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D,
                                             0x17D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A,
                                             0x153, 0x9D, 0x17E, 0x178 };
        if (v >= 0x80 && v < 0xA0) v = cp1252[v - 0x80];
        *cp = v;
        return i;
    }
    int n = 1;
    while (n < length && n < 12 && w_is_alnum(s[n])) n++;
    if (n == 1) return 0;
    bool semi = n < length && s[n] == ';';
    // Try the whole name, then (without the ;) shorter ones for the old no-semicolon forms.
    for (int len = n - 1; len >= 2; len--) {
        for (unsigned k = 0; k < sizeof(entity_table) / sizeof(entity_table[0]); k++) {
            const char *name = entity_table[k].name;
            int i = 0;
            while (i < len && name[i] == s[1 + i]) i++;
            if (i == len && name[i] == '\0') {
                if (len == n - 1 && semi) { *cp = entity_table[k].cp; return n + 1; }
                // without a semicolon, only the old Latin-1 names are allowed
                if (entity_table[k].cp < 0x100 && len == n - 1) { *cp = entity_table[k].cp; return n; }
                if (entity_table[k].cp < 0x100 && !semi) { *cp = entity_table[k].cp; return 1 + len; }
            }
        }
        if (!semi) continue;
        break;
    }
    return 0;
}

// Copy text into `out`, turning &...; into characters. Returns the new length.
static int decode_text(const char *s, int length, char *out, bool entities) {
    int o = 0;
    for (int i = 0; i < length;) {
        if (s[i] == '&' && entities) {
            uint32_t cp;
            int used = read_entity(s + i, length - i, &cp);
            if (used > 0) {
                o += w_utf8_put(cp, out + o);
                i += used;
                continue;
            }
        }
        if (s[i] == '\r') {
            out[o++] = '\n';
            if (i + 1 < length && s[i + 1] == '\n') i++;
            i++;
            continue;
        }
        out[o++] = s[i++];
    }
    out[o] = '\0';
    return o;
}

// ---- Nodes ----

const char *node_attr(const struct node *n, const char *name) {
    for (struct attr *a = n->attrs; a; a = a->next) {
        const char *x = a->name, *y = name;
        while (*x && *x == *y) { x++; y++; }
        if (*x == '\0' && *y == '\0') return a->value;
    }
    return NULL;
}

bool node_has_attr(const struct node *n, const char *name) { return node_attr(n, name) != NULL; }

void node_remove_attr(struct node *n, const char *name) {
    for (struct attr **pp = &n->attrs; *pp; pp = &(*pp)->next) {
        if (w_eq0((*pp)->name, name)) { *pp = (*pp)->next; return; }
    }
}

void node_set_attr(struct page *p, struct node *n, const char *name, const char *value) {
    for (struct attr *a = n->attrs; a; a = a->next) {
        if (w_eq0(a->name, name)) { a->value = w_strdup(&p->arena, value, w_len(value)); return; }
    }
    struct attr *a = w_alloc(&p->arena, sizeof(struct attr));
    a->name = w_strdup(&p->arena, name, w_len(name));
    a->value = w_strdup(&p->arena, value, w_len(value));
    a->next = NULL;
    struct attr **tail = &n->attrs;
    while (*tail) tail = &(*tail)->next;
    *tail = a;
}

static void append_child(struct node *parent, struct node *child) {
    child->parent = parent;
    child->prev = parent->last;
    child->next = NULL;
    if (parent->last) parent->last->next = child; else parent->first = child;
    parent->last = child;
}

struct node *node_new_element(struct page *p, const char *name) {
    struct node *n = w_alloc(&p->arena, sizeof(struct node));
    if (!n) return NULL;
    n->type = N_ELEMENT;
    int len = w_len(name);
    char *lower = w_strdup(&p->arena, name, len);
    for (int i = 0; i < len; i++) lower[i] = (char)w_lower((unsigned char)lower[i]);
    n->name = lower;
    n->tag = (uint8_t)tag_from_name(lower, len);
    n->focusable = -1;
    return n;
}

static struct node *new_text(struct page *p, const char *s, int len) {
    struct node *n = w_alloc(&p->arena, sizeof(struct node));
    if (!n) return NULL;
    n->type = N_TEXT;
    n->text = w_strdup(&p->arena, s, len);
    n->text_len = len;
    n->focusable = -1;
    return n;
}

static void node_text_walk(const struct node *n, char *out, int *len, int max, bool *space) {
    for (const struct node *c = n->first; c; c = c->next) {
        if (c->type == N_TEXT) {
            for (int i = 0; i < c->text_len && *len < max - 1; i++) {
                char ch = c->text[i];
                if (w_is_space(ch) || (unsigned char)ch == 0xC2) {
                    if ((unsigned char)ch == 0xC2 && !(i + 1 < c->text_len && (unsigned char)c->text[i + 1] == 0xA0)) { out[(*len)++] = ch; *space = false; continue; }
                    if ((unsigned char)ch == 0xC2) i++;
                    *space = true;
                    continue;
                }
                if (*space && *len > 0 && *len < max - 1) out[(*len)++] = ' ';
                *space = false;
                out[(*len)++] = ch;
            }
        } else if (c->type == N_ELEMENT && c->tag != T_SCRIPT && c->tag != T_STYLE && c->tag != T_SVG && c->tag != T_TEMPLATE) {
            if (c->tag == T_IMG) {
                const char *alt = node_attr(c, "alt");
                if (alt && *alt) {
                    if (*space && *len > 0 && *len < max - 1) out[(*len)++] = ' ';
                    *space = false;
                    for (int i = 0; alt[i] && *len < max - 1; i++) out[(*len)++] = alt[i];
                    *space = true;
                }
                continue;
            }
            node_text_walk(c, out, len, max, space);
        }
    }
}

int node_text(const struct node *n, char *out, int max) {
    int len = 0;
    bool space = false;
    node_text_walk(n, out, &len, max, &space);
    out[len] = '\0';
    return len;
}

// ---- The parser ----

#define MAX_DEPTH 400

struct parser {
    struct page *p;
    struct node *stack[MAX_DEPTH + 8];
    int depth;
    bool scripting;
    int mode;                   // 0 head, 1 body
    struct node *html, *head, *body;
    char *text_buf;
    int text_len, text_cap;
    bool in_svg_depth;
};

static bool is_void(enum tag t, const char *name, bool in_svg) {
    (void)name; (void)in_svg;
    switch (t) {
    case T_AREA: case T_BASE: case T_BR: case T_COL: case T_EMBED: case T_HR: case T_IMG: case T_INPUT:
    case T_LINK: case T_META: case T_PARAM: case T_SOURCE: case T_TRACK: case T_WBR: return true;
    default: return false;
    }
}

static bool is_head_tag(enum tag t) {
    return t == T_TITLE || t == T_META || t == T_LINK || t == T_STYLE || t == T_SCRIPT || t == T_BASE || t == T_TEMPLATE
        || t == T_NOSCRIPT;
}

static struct node *current(struct parser *ps) { return ps->stack[ps->depth - 1]; }

static void push(struct parser *ps, struct node *n) {
    if (ps->depth < MAX_DEPTH) ps->stack[ps->depth++] = n;
}

static void switch_to_body(struct parser *ps) {
    if (ps->mode == 1) return;
    ps->mode = 1;
    ps->depth = 0;
    push(ps, ps->html);
    push(ps, ps->body);
}

static void flush_text(struct parser *ps) {
    if (ps->text_len == 0) return;
    struct node *parent = current(ps);
    bool all_space = true;
    for (int i = 0; i < ps->text_len; i++) if (!w_is_space(ps->text_buf[i])) { all_space = false; break; }
    if (ps->mode == 0 && parent == ps->head) {
        if (all_space) { ps->text_len = 0; return; }
        switch_to_body(ps);
        parent = current(ps);
    } else if (parent == ps->html) {
        if (all_space) { ps->text_len = 0; return; }
        switch_to_body(ps);
        parent = current(ps);
    }
    // join onto a text node just before
    struct node *last = parent->last;
    if (last && last->type == N_TEXT) {
        char *joined = w_alloc(&ps->p->arena, (size_t)(last->text_len + ps->text_len + 1));
        memcpy(joined, last->text, (size_t)last->text_len);
        memcpy(joined + last->text_len, ps->text_buf, (size_t)ps->text_len);
        last->text = joined;
        last->text_len += ps->text_len;
    } else {
        struct node *t = new_text(ps->p, ps->text_buf, ps->text_len);
        if (t) append_child(parent, t);
    }
    ps->text_len = 0;
}

static void add_text(struct parser *ps, const char *s, int len) {
    if (ps->text_len + len + 1 > ps->text_cap) {
        int cap = (ps->text_len + len + 1) * 2;
        ps->text_buf = realloc(ps->text_buf, (size_t)cap);
        ps->text_cap = cap;
    }
    memcpy(ps->text_buf + ps->text_len, s, (size_t)len);
    ps->text_len += len;
}

// Is a <p> (or other) open, within the "scope" a new block would close it in?
static int find_open(struct parser *ps, enum tag t, bool button_scope) {
    for (int i = ps->depth - 1; i >= 0; i--) {
        enum tag x = (enum tag)ps->stack[i]->tag;
        if (x == t) return i;
        if (x == T_TABLE || x == T_TD || x == T_TH || x == T_CAPTION || x == T_HTML || x == T_OBJECT || x == T_TEMPLATE) return -1;
        if (button_scope && x == T_BUTTON) return -1;
    }
    return -1;
}

static void close_to(struct parser *ps, int index) {   // pop everything from stack[index] up
    if (index < 1) return;
    ps->depth = index;
}

static bool closes_p(enum tag t) {
    switch (t) {
    case T_ADDRESS: case T_ARTICLE: case T_ASIDE: case T_BLOCKQUOTE: case T_CENTER: case T_DETAILS: case T_DIALOG:
    case T_DIV: case T_DL: case T_FIELDSET: case T_FIGCAPTION: case T_FIGURE: case T_FOOTER: case T_FORM:
    case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: case T_HEADER: case T_HGROUP: case T_HR:
    case T_LI: case T_MAIN: case T_MENU: case T_NAV: case T_OL: case T_P: case T_PRE: case T_SECTION: case T_TABLE:
    case T_UL: case T_DD: case T_DT: case T_SUMMARY: case T_LEGEND:
        return true;
    default: return false;
    }
}

static void implied_end_tags(struct parser *ps, enum tag t) {
    if (closes_p(t)) {
        int i = find_open(ps, T_P, true);
        if (i >= 0) close_to(ps, i);
    }
    switch (t) {
    case T_LI: {
        for (int i = ps->depth - 1; i >= 1; i--) {
            enum tag x = (enum tag)ps->stack[i]->tag;
            if (x == T_LI) { close_to(ps, i); break; }
            if (x == T_UL || x == T_OL || x == T_MENU || x == T_TABLE || x == T_TD || x == T_TH || x == T_BODY || x == T_HTML) break;
            if (x == T_DIV || x == T_SECTION || x == T_ARTICLE || x == T_ASIDE || x == T_NAV || x == T_HEADER || x == T_FOOTER || x == T_MAIN) {
                if (x != T_DIV || true) break;
            }
        }
        break;
    }
    case T_DD: case T_DT: {
        for (int i = ps->depth - 1; i >= 1; i--) {
            enum tag x = (enum tag)ps->stack[i]->tag;
            if (x == T_DD || x == T_DT) { close_to(ps, i); break; }
            if (x == T_DL || x == T_TABLE || x == T_BODY || x == T_HTML) break;
        }
        break;
    }
    case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: {
        enum tag x = (enum tag)current(ps)->tag;
        if (x >= T_H1 && x <= T_H6) ps->depth--;
        break;
    }
    case T_TR: case T_TD: case T_TH: case T_THEAD: case T_TBODY: case T_TFOOT: case T_CAPTION: case T_COLGROUP: {
        for (int i = ps->depth - 1; i >= 1; i--) {
            enum tag x = (enum tag)ps->stack[i]->tag;
            if (x == T_TABLE || x == T_HTML || x == T_BODY) break;
            bool close = false;
            if (t == T_TD || t == T_TH) close = (x == T_TD || x == T_TH);
            if (t == T_TR) close = (x == T_TD || x == T_TH || x == T_TR);
            if (t == T_THEAD || t == T_TBODY || t == T_TFOOT || t == T_CAPTION || t == T_COLGROUP)
                close = (x == T_TD || x == T_TH || x == T_TR || x == T_THEAD || x == T_TBODY || x == T_TFOOT || x == T_CAPTION || x == T_COLGROUP);
            if ((t == T_TD || t == T_TH) && x == T_TR) break;
            if (close) { close_to(ps, i); break; }
            if (x == T_DIV || x == T_SPAN || x == T_A || x == T_UL || x == T_OL || x == T_P || x == T_FORM || x == T_SELECT) break;
        }
        break;
    }
    case T_OPTION: {
        if (current(ps)->tag == T_OPTION) ps->depth--;
        break;
    }
    case T_OPTGROUP: {
        if (current(ps)->tag == T_OPTION) ps->depth--;
        if (current(ps)->tag == T_OPTGROUP) ps->depth--;
        break;
    }
    case T_A: {
        int i = find_open(ps, T_A, false);
        if (i >= 0) close_to(ps, i);
        break;
    }
    case T_BUTTON: {
        int i = find_open(ps, T_BUTTON, false);
        if (i >= 0) close_to(ps, i);
        break;
    }
    default: break;
    }
}

static bool is_raw_text(enum tag t, bool scripting) {
    return t == T_SCRIPT || t == T_STYLE || t == T_NOFRAMES || t == T_IFRAME || t == T_TEMPLATE || (t == T_NOSCRIPT && scripting);
}

// Read one start tag. `i` is just after '<'. Returns where it ends.
static int read_start_tag(struct parser *ps, const char *s, int length, int i) {
    struct page *p = ps->p;
    int name_start = i;
    while (i < length && !w_is_space(s[i]) && s[i] != '>' && s[i] != '/') i++;
    int name_len = i - name_start;
    char lname[48];
    int ln = name_len < 47 ? name_len : 47;
    for (int k = 0; k < ln; k++) lname[k] = (char)w_lower((unsigned char)s[name_start + k]);
    lname[ln] = '\0';
    struct node *n = node_new_element(p, lname);
    if (!n) return length;
    bool self_closing = false;
    struct attr **tail = &n->attrs;
    while (i < length && s[i] != '>') {
        if (w_is_space(s[i])) { i++; continue; }
        if (s[i] == '/') {
            if (i + 1 < length && s[i + 1] == '>') self_closing = true;
            i++;
            continue;
        }
        int a_start = i;
        while (i < length && !w_is_space(s[i]) && s[i] != '=' && s[i] != '>' && !(s[i] == '/' && i + 1 < length && s[i + 1] == '>')) i++;
        int a_len = i - a_start;
        while (i < length && w_is_space(s[i])) i++;
        const char *value = "";
        int value_len = 0;
        char *decoded = NULL;
        if (i < length && s[i] == '=') {
            i++;
            while (i < length && w_is_space(s[i])) i++;
            int v_start, v_end;
            if (i < length && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                v_start = i;
                while (i < length && s[i] != q) i++;
                v_end = i;
                if (i < length) i++;
            } else {
                v_start = i;
                while (i < length && !w_is_space(s[i]) && s[i] != '>') i++;
                v_end = i;
            }
            decoded = w_alloc(&p->arena, (size_t)(v_end - v_start) + 1);
            if (decoded) { value_len = decode_text(s + v_start, v_end - v_start, decoded, true); value = decoded; }
        }
        (void)value_len;
        if (a_len <= 0) continue;
        char *aname = w_alloc(&p->arena, (size_t)a_len + 1);
        for (int k = 0; k < a_len; k++) aname[k] = (char)w_lower((unsigned char)s[a_start + k]);
        // first one wins
        bool dup = false;
        for (struct attr *x = n->attrs; x; x = x->next) if (w_eq0(x->name, aname)) { dup = true; break; }
        if (dup) continue;
        struct attr *a = w_alloc(&p->arena, sizeof(struct attr));
        a->name = aname;
        a->value = value;
        a->next = NULL;
        *tail = a;
        tail = &a->next;
    }
    if (i < length) i++;   // '>'

    enum tag t = (enum tag)n->tag;
    // The document's own tags: merge their attributes.
    if (t == T_HTML) {
        for (struct attr *a = n->attrs; a; a = a->next) if (!node_attr(ps->html, a->name)) node_set_attr(p, ps->html, a->name, a->value);
        return i;
    }
    if (t == T_HEAD) {
        for (struct attr *a = n->attrs; a; a = a->next) if (!node_attr(ps->head, a->name)) node_set_attr(p, ps->head, a->name, a->value);
        return i;
    }
    if (t == T_BODY) {
        flush_text(ps);
        for (struct attr *a = n->attrs; a; a = a->next) if (!node_attr(ps->body, a->name)) node_set_attr(p, ps->body, a->name, a->value);
        switch_to_body(ps);
        return i;
    }
    if (t == T_FRAMESET || t == T_FRAME) return i;

    flush_text(ps);
    if (ps->mode == 0 && !is_head_tag(t)) switch_to_body(ps);
    else if (ps->mode == 0 && ps->depth == 1) { /* in html but head popped */ ps->depth = 0; push(ps, ps->html); push(ps, ps->head); }
    if (current(ps) == ps->html) switch_to_body(ps);

    bool parent_svg = current(ps)->in_svg;
    if (!parent_svg) implied_end_tags(ps, t);
    struct node *parent = current(ps);
    n->in_svg = parent->in_svg || t == T_SVG;
    // <noscript> in the head, with scripting off, is read as ordinary content in the body.
    if (t == T_FORM && !parent_svg && find_open(ps, T_FORM, false) >= 0) return i;   // forms don't nest
    const char *aid = node_attr(n, "data-aid");
    if (aid) { int v = 0; for (int k = 0; aid[k] >= '0' && aid[k] <= '9'; k++) v = v * 10 + (aid[k] - '0'); n->aid = v; }
    n->id = node_attr(n, "id");
    n->cls = node_attr(n, "class");
    n->open = node_has_attr(n, "open");
    append_child(parent, n);

    // text inside <svg> style/title etc. is read raw below; void elements end now
    bool void_el = !n->in_svg && is_void(t, n->name, false);
    if (void_el) return i;
    if (n->in_svg && self_closing) return i;
    if (is_raw_text(t, ps->scripting) || (n->in_svg && (t == T_STYLE || t == T_SCRIPT))) {
        // read up to the matching end tag
        int j = i;
        int nl = w_len(n->name);
        while (j < length) {
            if (s[j] == '<' && j + 1 < length && s[j + 1] == '/') {
                int k = 0;
                while (k < nl && j + 2 + k < length && w_lower((unsigned char)s[j + 2 + k]) == (unsigned char)n->name[k]) k++;
                if (k == nl && (j + 2 + k >= length || s[j + 2 + k] == '>' || s[j + 2 + k] == '/' || w_is_space(s[j + 2 + k]))) break;
            }
            j++;
        }
        if (j > i) {
            char *buf = w_alloc(&p->arena, (size_t)(j - i) + 1);
            int out = decode_text(s + i, j - i, buf, false);
            struct node *tx = new_text(p, buf, out);
            if (tx) append_child(n, tx);
        }
        while (j < length && s[j] != '>') j++;
        return j < length ? j + 1 : length;
    }
    if (t == T_TEXTAREA || t == T_TITLE) {
        int j = i;
        int nl = w_len(n->name);
        while (j < length) {
            if (s[j] == '<' && j + 1 < length && s[j + 1] == '/') {
                int k = 0;
                while (k < nl && j + 2 + k < length && w_lower((unsigned char)s[j + 2 + k]) == (unsigned char)n->name[k]) k++;
                if (k == nl) break;
            }
            j++;
        }
        int start = i;
        if (t == T_TEXTAREA && start < j && s[start] == '\n') start++;
        if (j > start) {
            char *buf = w_alloc(&p->arena, (size_t)(j - start) + 1);
            int out = decode_text(s + start, j - start, buf, true);
            struct node *tx = new_text(p, buf, out);
            if (tx) append_child(n, tx);
        }
        while (j < length && s[j] != '>') j++;
        return j < length ? j + 1 : length;
    }
    if (ps->depth < MAX_DEPTH) push(ps, n);
    return i;
}

static int read_end_tag(struct parser *ps, const char *s, int length, int i) {
    int name_start = i;
    while (i < length && !w_is_space(s[i]) && s[i] != '>' && s[i] != '/') i++;
    int name_len = i - name_start;
    while (i < length && s[i] != '>') i++;
    if (i < length) i++;
    if (name_len <= 0 || name_len > 40) return i;
    char lname[48];
    for (int k = 0; k < name_len; k++) lname[k] = (char)w_lower((unsigned char)s[name_start + k]);
    lname[name_len] = '\0';
    enum tag t = tag_from_name(lname, name_len);
    flush_text(ps);
    if (t == T_HTML) return i;
    if (t == T_BODY) return i;
    if (t == T_HEAD) { if (ps->mode == 0) { /* stay: body begins at the next content */ ps->depth = 1; } return i; }
    if (t == T_BR) return i;
    // find the matching open element
    bool svg = current(ps)->in_svg;
    for (int k = ps->depth - 1; k >= 1; k--) {
        struct node *n = ps->stack[k];
        bool match = (n->tag == t && t != T_OTHER) || (t == T_OTHER && n->tag == T_OTHER && w_eq0(n->name, lname));
        if (match) { ps->depth = k; return i; }
        if (!svg && !n->in_svg) {
            // don't reach out of table cells and such
            if (n->tag == T_TD || n->tag == T_TH || n->tag == T_CAPTION || n->tag == T_TABLE) {
                if (t != T_TABLE && t != T_TR && t != T_TBODY && t != T_THEAD && t != T_TFOOT && t != T_TD && t != T_TH && t != T_CAPTION) return i;
            }
            if (n->tag == T_BUTTON && false) return i;
        }
    }
    return i;
}

struct node *web_parse_html(struct page *p, const char *html, int length, bool scripting) {
    struct parser ps;
    memset(&ps, 0, sizeof(ps));
    ps.p = p;
    ps.scripting = scripting;
    ps.html = node_new_element(p, "html");
    ps.head = node_new_element(p, "head");
    ps.body = node_new_element(p, "body");
    append_child(ps.html, ps.head);
    append_child(ps.html, ps.body);
    push(&ps, ps.html);
    push(&ps, ps.head);
    p->root = ps.html;
    p->head = ps.head;
    p->body = ps.body;

    int i = 0;
    // skip a byte order mark
    if (length >= 3 && (unsigned char)html[0] == 0xEF && (unsigned char)html[1] == 0xBB && (unsigned char)html[2] == 0xBF) i = 3;
    while (i < length) {
        if (p->arena.total > 96u * 1024 * 1024) break;
        char c = html[i];
        if (c == '<') {
            if (i + 3 < length && html[i + 1] == '!' && html[i + 2] == '-' && html[i + 3] == '-') {
                int j = i + 4;
                if (j < length && html[j] == '>') { i = j + 1; continue; }
                if (j + 1 < length && html[j] == '-' && html[j + 1] == '>') { i = j + 2; continue; }
                while (j + 2 < length && !(html[j] == '-' && html[j + 1] == '-' && html[j + 2] == '>')) j++;
                i = j + 3 > length ? length : j + 3;
                continue;
            }
            if (i + 1 < length && (html[i + 1] == '!' || html[i + 1] == '?')) {
                // <![CDATA[ ... ]]> inside svg, doctype and the like
                if (i + 8 < length && strncmp(html + i, "<![CDATA[", 9) == 0) {
                    int j = i + 9;
                    while (j + 2 < length && !(html[j] == ']' && html[j + 1] == ']' && html[j + 2] == '>')) j++;
                    add_text(&ps, html + i + 9, j - (i + 9));
                    i = j + 3 > length ? length : j + 3;
                    continue;
                }
                while (i < length && html[i] != '>') i++;
                i++;
                continue;
            }
            if (i + 1 < length && w_is_alpha(html[i + 1])) { i = read_start_tag(&ps, html, length, i + 1); continue; }
            if (i + 2 < length && html[i + 1] == '/' && w_is_alpha(html[i + 2])) { i = read_end_tag(&ps, html, length, i + 2); continue; }
            if (i + 1 < length && html[i + 1] == '/') {   // </ followed by junk: a bogus comment
                while (i < length && html[i] != '>') i++;
                i++;
                continue;
            }
            add_text(&ps, "<", 1);
            i++;
            continue;
        }
        // plain text up to the next '<'
        int j = i;
        while (j < length && html[j] != '<') j++;
        char *buf = malloc((size_t)(j - i) + 1);
        if (!buf) break;
        int out = decode_text(html + i, j - i, buf, true);
        add_text(&ps, buf, out);
        free(buf);
        i = j;
        // Text only joins the page when something else happens (so it can be tidied together).
        if (i >= length || html[i] == '<') {
            // flush when the next thing is a tag (handled in the tag readers), except at the end
        }
    }
    flush_text(&ps);
    free(ps.text_buf);

    // The title.
    p->title[0] = '\0';
    for (struct node *n = p->head->first; n; n = n->next) {
        if (n->tag == T_TITLE) { node_text(n, p->title, sizeof(p->title)); break; }
    }
    return p->root;
}
