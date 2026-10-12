// web_doc.c - what the screen reader sees of a laid-out page.
//
// The browser draws a page with the web engine, but moving around it by
// keyboard and by ear works on a simpler thing: the "document" (browser_doc.h),
// which is the page's words in reading order, grouped into lines, plus the
// links, text boxes and buttons Tab moves between. This fills that in from
// the engine's boxes, so what is read matches what is on screen, and every
// line knows where it is on the page (to scroll to it).

#include "browser_doc.h"
#include "web.h"
#include "web_lay.h"
#ifndef WEB_HOST
#include "gui.h"
#endif


// ---- Text going into the document (plain ASCII, for the speech engine) ----

static const char latin1_fold[] = "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

// ASCII for a character, written to out. Returns how many characters (0: skip it).
static int fold(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp == 0xA0 || cp == 0x2002 || cp == 0x2003 || cp == 0x2009 || cp == 0x200A || cp == 0x202F || cp == 0x3000) { out[0] = ' '; return 1; }
    if (cp >= 0xC0 && cp <= 0xFF) { out[0] = latin1_fold[cp - 0xC0]; return 1; }
    const char *s = NULL;
    switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: s = "'"; break;
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: s = "\""; break;
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: s = "-"; break;
    case 0x2026: s = "..."; break;
    case 0x2022: case 0x25CF: case 0x25AA: case 0x2023: case 0xB7: s = "*"; break;
    case 0xA9: s = "(c)"; break;
    case 0xAE: s = "(r)"; break;
    case 0x2122: s = "(tm)"; break;
    case 0xAB: s = "<<"; break;
    case 0xBB: s = ">>"; break;
    case 0x2192: s = "->"; break;
    case 0x2190: s = "<-"; break;
    case 0x20AC: s = "EUR "; break;
    case 0xA3: s = "GBP "; break;
    case 0xA5: s = "JPY "; break;
    case 0xB0: s = " degrees"; break;
    case 0xBD: s = "1/2"; break;
    case 0xBC: s = "1/4"; break;
    case 0xBE: s = "3/4"; break;
    case 0x200B: case 0x200C: case 0x200D: case 0xFEFF: case 0xAD: return 0;
    default: return 0;
    }
    int n = 0;
    while (s[n]) { out[n] = s[n]; n++; }
    return n;
}

struct doc {
    struct page_document *d;
    struct page *p;
    bool full;
};

static int store_ascii(struct page_document *d, const char *s, int len, int max_out) {
    if (d->text_used + len * 4 + 8 >= TEXT_STORE) return -1;
    int at = d->text_used;
    int n = 0;
    const char *end = s + len;
    bool space = true;       // collapse and trim
    while (s < end && n < max_out) {
        uint32_t cp = w_utf8_next(&s, end);
        char tmp[8];
        int k = fold(cp, tmp);
        for (int i = 0; i < k && n < max_out; i++) {
            char c = tmp[i];
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                if (!space) { d->text[at + n++] = ' '; space = true; }
            } else if ((unsigned char)c >= 32) { d->text[at + n++] = c; space = false; }
        }
    }
    while (n > 0 && d->text[at + n - 1] == ' ') n--;
    d->text[at + n] = '\0';
    d->text_used = at + n + 1;
    return at;
}

// A string for a field value or the like, copied into a fixed buffer.
static void copy_ascii(char *out, int max, const char *s, int len) {
    int n = 0;
    const char *end = s + len;
    while (s < end && n < max - 1) {
        uint32_t cp = w_utf8_next(&s, end);
        char tmp[8];
        int k = fold(cp, tmp);
        for (int i = 0; i < k && n < max - 1; i++) out[n++] = tmp[i];
    }
    out[n] = '\0';
}

// ---- Looking things up in the page ----

static const char *attr(const struct node *n, const char *name) { return n ? node_attr(n, name) : NULL; }

static struct node *find_id(struct node *n, const char *id, int depth) {
    if (depth > 300) return NULL;
    for (struct node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEMENT) continue;
        if (c->id && strcmp(c->id, id) == 0) return c;
        struct node *r = find_id(c, id, depth + 1);
        if (r) return r;
    }
    return NULL;
}

static struct node *find_label_for(struct node *n, const char *id, int depth) {
    if (depth > 300) return NULL;
    for (struct node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEMENT) continue;
        if (c->tag == T_LABEL) {
            const char *f = attr(c, "for");
            if (f && strcmp(f, id) == 0) return c;
        }
        struct node *r = find_label_for(c, id, depth + 1);
        if (r) return r;
    }
    return NULL;
}

static bool aria_hidden(const struct node *n) {
    for (int i = 0; n && i < 60; n = n->parent, i++) {
        if (n->type != N_ELEMENT) continue;
        const char *h = node_attr(n, "aria-hidden");
        if (h && w_eq0(h, "true")) return true;
    }
    return false;
}

static int heading_level(const struct node *n) {
    for (int i = 0; n && i < 40; n = n->parent, i++) {
        if (n->type != N_ELEMENT) continue;
        if (n->tag >= T_H1 && n->tag <= T_H6) return n->tag - T_H1 + 1;
        const char *r = node_attr(n, "role");
        if (r && w_eq0(r, "heading")) {
            const char *l = node_attr(n, "aria-level");
            int v = l ? l[0] - '0' : 2;
            return v >= 1 && v <= 6 ? v : 2;
        }
    }
    return 0;
}

static int focus_of(const struct node *n) {
    for (int i = 0; n && i < 30; n = n->parent, i++) if (n->type == N_ELEMENT && n->focusable >= 0) return n->focusable;
    return -1;
}

static struct node *form_of(struct node *n) {
    for (int i = 0; n && i < 60; n = n->parent, i++) if (n->type == N_ELEMENT && n->tag == T_FORM) return n;
    return NULL;
}

static bool is_text_type(const char *t) {
    static const char *names[] = { "text", "search", "email", "url", "tel", "number", "password", "date", "time", "datetime-local",
                                   "month", "week", "", NULL };
    for (int i = 0; names[i]; i++) if (w_eq0(t, names[i])) return true;
    return false;
}

// The words a control or link is called.
static void label_of(struct page *p, struct node *n, char *out, int max) {
    char buf[600];
    buf[0] = '\0';
    const char *v;
    if ((v = attr(n, "aria-label")) && *v) { copy_ascii(out, max, v, (int)strlen(v)); return; }
    if ((v = attr(n, "aria-labelledby")) && *v) {
        char id[80]; int k = 0;
        while (*v && *v != ' ' && k < 79) id[k++] = *v++;
        id[k] = '\0';
        struct node *t = p->root ? find_id(p->root, id, 0) : NULL;
        if (t) { int tl = node_text(t, buf, sizeof(buf)); if (tl > 0) { copy_ascii(out, max, buf, tl); if (out[0]) return; } }
    }
    bool field = n->tag == T_INPUT || n->tag == T_TEXTAREA || n->tag == T_SELECT;
    const char *type = n->tag == T_INPUT ? node_input_type(n) : "";
    bool button_input = n->tag == T_INPUT && (w_eq0(type, "submit") || w_eq0(type, "button") || w_eq0(type, "reset") || w_eq0(type, "image"));
    if (button_input) {
        if ((v = attr(n, "value")) && *v) { copy_ascii(out, max, v, (int)strlen(v)); return; }
        if ((v = attr(n, "alt")) && *v) { copy_ascii(out, max, v, (int)strlen(v)); return; }
        copy_ascii(out, max, w_eq0(type, "reset") ? "Reset" : "Submit", 8);
        return;
    }
    if (field) {
        // <label for=id>, or a label around it
        struct node *lab = NULL;
        if (n->id && p->root) lab = find_label_for(p->root, n->id, 0);
        if (!lab) for (struct node *q = n->parent; q && !lab; q = q->parent) if (q->type == N_ELEMENT && q->tag == T_LABEL) lab = q;
        if (lab) {
            int tl = node_text(lab, buf, sizeof(buf));
            if (tl > 0) { copy_ascii(out, max, buf, tl); if (out[0]) return; }
        }
        if ((v = attr(n, "placeholder")) && *v) { copy_ascii(out, max, v, (int)strlen(v)); return; }
        if ((v = attr(n, "title")) && *v) { copy_ascii(out, max, v, (int)strlen(v)); return; }
        if ((v = attr(n, "name")) && *v) { copy_ascii(out, max, v, (int)strlen(v)); return; }
        const char *fallback = n->tag == T_TEXTAREA ? "text area" : (n->tag == T_SELECT ? "menu" : "text");
        if (n->tag == T_INPUT && w_eq0(type, "checkbox")) fallback = "check box";
        if (n->tag == T_INPUT && w_eq0(type, "radio")) fallback = "radio button";
        copy_ascii(out, max, fallback, (int)strlen(fallback));
        return;
    }
    int tl = node_text(n, buf, sizeof(buf));
    if (tl > 0) { copy_ascii(out, max, buf, tl); if (out[0]) return; }
    if ((v = attr(n, "title")) && *v) { copy_ascii(out, max, v, (int)strlen(v)); return; }
    if ((v = attr(n, "href")) && *v) {
        // the last part of the address
        const char *q = v + strlen(v);
        while (q > v && (q[-1] == '/')) q--;
        const char *s = q;
        while (s > v && s[-1] != '/') s--;
        if (q > s) { copy_ascii(out, max, s, (int)(q - s)); return; }
    }
    copy_ascii(out, max, n->tag == T_A ? "link" : "button", 8);
}

static const struct node *selected_option(const struct node *sel) {
    const struct node *first = NULL;
    for (const struct node *o = sel->first; o; o = o->next) {
        if (o->tag == T_OPTION) {
            if (!first) first = o;
            if (node_has_attr(o, "selected")) return o;
        } else if (o->tag == T_OPTGROUP) {
            for (const struct node *q = o->first; q; q = q->next) if (q->tag == T_OPTION) {
                if (!first) first = q;
                if (node_has_attr(q, "selected")) return q;
            }
        }
    }
    return first;
}

// ---- Building the focusables ----

static void make_focusables(struct doc *D) {
    struct page_document *d = D->d;
    struct page *p = D->p;
    struct node *forms[MAX_FORMS];
    int nforms = 0;
    d->focusable_count = 0;
    d->link_count = d->field_count = 0;
    for (int i = 0; i < p->focus_count && i < MAX_FOCUSABLES; i++) {
        struct node *n = p->focus_nodes[i];
        struct page_focusable *f = &d->focusables[i];
        memset(f, 0, sizeof(*f));
        f->node = n;
        f->aid = n->aid;
        f->name = f->target = f->form = -1;
        f->label = f->target;
        char label[200];
        label[0] = '\0';
        const char *type = n->tag == T_INPUT ? node_input_type(n) : "";
        const char *href = attr(n, "href");
        const char *name = attr(n, "name");
        f->kind = FOCUS_BUTTON;
        if ((n->tag == T_A || n->tag == T_AREA) && href && *href) {
            f->kind = FOCUS_LINK;
        } else if (n->tag == T_TEXTAREA || (n->tag == T_INPUT && is_text_type(type))) {
            f->kind = FOCUS_FIELD;
            f->secret = n->tag == T_INPUT && w_eq0(type, "password");
        } else if (n->tag == T_INPUT && w_eq0(type, "checkbox")) { f->sub = SUB_CHECK; }
        else if (n->tag == T_INPUT && w_eq0(type, "radio")) { f->sub = SUB_RADIO; }
        else if (n->tag == T_INPUT && w_eq0(type, "file")) { f->sub = SUB_FILE; }
        else if (n->tag == T_SELECT) { f->sub = SUB_SELECT; }
        else if (n->tag == T_SUMMARY) { f->sub = SUB_SUMMARY; }
        else if (n->tag == T_BUTTON || (n->tag == T_INPUT && (w_eq0(type, "submit") || w_eq0(type, "image")))) {
            const char *bt = attr(n, "type");
            f->submits = n->aid == 0 && (n->tag == T_INPUT || !bt || w_eq0(bt, "submit"));
        }
        if (f->kind == FOCUS_LINK) d->link_count++;
        if (f->kind == FOCUS_FIELD) d->field_count++;
        // its form
        struct node *fm = form_of(n);
        if (fm) {
            int k = 0;
            while (k < nforms && forms[k] != fm) k++;
            if (k == nforms && nforms < MAX_FORMS) {
                forms[nforms] = fm;
                const char *act = attr(fm, "action");
                d->forms[nforms].action = (act && *act) ? store_ascii(d, act, (int)strlen(act), 1000) : -1;
                d->forms[nforms].hidden_first = d->hidden_count;
                d->forms[nforms].hidden_count = 0;
                nforms++;
            }
            f->form = k < nforms ? k : -1;
        }
        if (name && *name && (f->kind == FOCUS_FIELD || f->sub == SUB_CHECK || f->sub == SUB_RADIO || f->sub == SUB_SELECT || f->submits))
            f->name = store_ascii(d, name, (int)strlen(name), 100);
        label_of(p, n, label, sizeof(label));
        f->label = store_ascii(d, label, (int)strlen(label), 190);
        if (f->kind == FOCUS_LINK && href) {
            f->target = store_ascii(d, href, (int)strlen(href), 1500);
        }
        // state and value
        if (f->kind == FOCUS_FIELD) {
            if (n->tag == T_TEXTAREA) {
                char tb[FIELD_MAX * 2];
                int tl = node_text(n, tb, sizeof(tb));
                const char *v = attr(n, "value");
                if (v) copy_ascii(f->value, FIELD_MAX, v, (int)strlen(v)); else copy_ascii(f->value, FIELD_MAX, tb, tl);
            } else {
                const char *v = attr(n, "value");
                if (v) copy_ascii(f->value, FIELD_MAX, v, (int)strlen(v));
            }
        } else if (f->sub == SUB_CHECK || f->sub == SUB_RADIO) {
            f->checked = node_has_attr(n, "checked");
            const char *v = attr(n, "value");
            copy_ascii(f->value, FIELD_MAX, v && *v ? v : "on", v && *v ? (int)strlen(v) : 2);
        } else if (f->sub == SUB_SELECT) {
            const struct node *o = selected_option(n);
            if (o) { char tb[200]; int tl = node_text(o, tb, sizeof(tb)); copy_ascii(f->value, FIELD_MAX, tb, tl); }
        }
        d->focusable_count = i + 1;
    }
    d->form_count = nforms;
    // hidden fields of the forms (kept in the order of the forms)
    d->hidden_count = 0;
    for (int k = 0; k < nforms; k++) {
        d->forms[k].hidden_first = d->hidden_count;
        d->forms[k].hidden_count = 0;
        struct node *stack[256]; int sp = 0;
        stack[sp++] = forms[k];
        while (sp) {
            struct node *q = stack[--sp];
            if (q->type == N_ELEMENT && q->tag == T_INPUT && w_eq0(node_input_type(q), "hidden")) {
                const char *nm = attr(q, "name");
                const char *vl = attr(q, "value");
                if (nm && *nm && d->hidden_count < MAX_HIDDENS) {
                    struct page_hidden *h = &d->hiddens[d->hidden_count++];
                    h->name = store_ascii(d, nm, (int)strlen(nm), 100);
                    h->value = store_ascii(d, vl ? vl : "", vl ? (int)strlen(vl) : 0, 300);
                    h->form = k;
                    d->forms[k].hidden_count++;
                }
            }
            for (struct node *c = q->first; c && sp < 255; c = c->next) if (c->type == N_ELEMENT) stack[sp++] = c;
        }
    }
}

// ---- Building the lines ----

struct builder {
    struct doc *D;
    unsigned char *seen;        // per focusable: has it got a box in the document?
    int line_right;             // where the current line's last item ended
    int line_bottom;
    int line_top;
    int cur_heading;
    bool open;                  // a line is being built
    bool full_stop;
    bool pending_unseen;
};

static void end_line(struct builder *B) {
    B->open = false;
}

static void add_item(struct builder *B, int x, int y, int w, int h, const char *text, int len, uint16_t style, int focus,
                     int heading, const struct node *owner) {
    struct page_document *d = B->D->d;
    if (B->full_stop) return;
    if (d->box_count >= MAX_BOXES - 1 || d->line_count >= MAX_LINES - 1) { B->full_stop = true; return; }
    if (len > 0 || focus >= 0) {
        // text first, so the store has room
    }
    int at = len > 0 ? store_ascii(d, text, len, 2000) : store_ascii(d, "", 0, 1);
    if (at < 0) { B->full_stop = true; return; }
    int tl = (int)strlen(d->text + at);
    if (tl == 0 && focus < 0) return;
    (void)owner;
    // a new line when the item is below the line so far, or starts back to the left
    bool newline = !B->open;
    if (B->open) {
        int mid = y + h / 2;
        if (mid > B->line_bottom - 1 || y + h < B->line_top + 1 || x < B->line_right - 3) newline = true;
    }
    if (newline) {
        struct page_line *l = &d->lines[d->line_count++];
        l->y = y; l->h = h > 0 ? h : 16;
        l->first_box = d->box_count; l->box_count = 0;
        l->heading = heading;
        B->open = true;
        B->line_top = y; B->line_bottom = y + (h > 0 ? h : 16); B->line_right = x;
    }
    struct page_line *l = &d->lines[d->line_count - 1];
    struct page_box *b = &d->boxes[d->box_count++];
    b->x = x; b->y = y; b->w = w; b->h = h;
    b->text = at; b->length = tl;
    b->style = style;
    b->scale = 1;
    b->focusable = (int16_t)focus;
    b->line = d->line_count - 1;
    l->box_count++;
    if (y < l->y) { l->h += l->y - y; l->y = y; }
    if (y + h > l->y + l->h) l->h = y + h - l->y;
    if (heading && !l->heading) l->heading = heading;
    if (y < B->line_top) B->line_top = y;
    if (y + h > B->line_bottom) B->line_bottom = y + h;
    if (x + w > B->line_right) B->line_right = x + w;
    if (focus >= 0 && focus < MAX_FOCUSABLES) {
        if (!B->seen[focus]) d->focusables[focus].line = d->line_count - 1;
        B->seen[focus] = 1;
    }
}

static bool block_like(const struct box *b) {
    return b->type == BT_BLOCK && !b->inline_level;
}

static void gather(struct builder *B, struct box *b, int depth) {
    if (depth > 300 || B->full_stop) return;
    struct page *p = B->D->p;
    (void)p;
    bool shown = b->stack_order > 0 && !b->hidden;
    if (b->type == BT_REPLACED && shown && b->node) {
        const struct node *n = b->node;
        int f = n->focusable >= 0 ? n->focusable : focus_of(n);
        int x = b->ax, y = b->ay, w = b->w, h = b->h;
        if (!aria_hidden(n)) {
            if (b->replaced == RK_IMAGE || b->replaced == RK_SVG) {
                const char *alt = b->alt;
                if (!alt && n->tag == T_SVG) alt = attr(n, "aria-label");
                if (alt && *alt) {
                    char text[400];
                    int k = 0;
                    bool linked = f >= 0;
                    if (!linked) { memcpy(text, "Image: ", 7); k = 7; }
                    int al = (int)strlen(alt); if (al > 350) al = 350;
                    memcpy(text + k, alt, (size_t)al); k += al;
                    add_item(B, x, y, w, h, text, k, STYLE_IMAGE, f, heading_level(n), n);
                } else if (f >= 0 && n->focusable == f) {
                    add_item(B, x, y, w, h, "", 0, STYLE_IMAGE, f, heading_level(n), n);
                }
            } else if (n->focusable >= 0) {
                struct page_focusable *pf = &B->D->d->focusables[n->focusable];
                add_item(B, x, y, w, h, "", 0, pf->kind == FOCUS_FIELD ? STYLE_FIELD : STYLE_BUTTON, n->focusable, 0, n);
            }
        }
        return;
    }
    if (b->ifc && b->frags && shown) {
        int ox = b->ax + b->border.left + b->pad.left, oy = b->ay + b->border.top + b->pad.top;
        // The lines of this block, in order; atomic boxes in them are added when we reach them below.
        for (int i = 0; i < b->frag_count && !B->full_stop; i++) {
            const struct frag *fr = &b->frags[i];
            if (fr->type == FRAG_TEXT) {
                struct box *tb = fr->box;
                if (tb->hidden || !tb->transformed || fr->text_len <= 0 || !tb->node) continue;
                if (aria_hidden(tb->node)) continue;
                int f = fr->focusable >= 0 ? fr->focusable : focus_of(tb->node);
                uint16_t st = 0;
                if (f >= 0 && B->D->d->focusables[f].kind == FOCUS_LINK) st |= STYLE_LINK;
                if (tb->style->font_weight >= 600) st |= STYLE_BOLD;
                add_item(B, ox + fr->x, oy + fr->y, fr->w, fr->h, tb->transformed + fr->text_off, fr->text_len, st, f,
                         heading_level(tb->node), tb->node);
            } else if (fr->type == FRAG_BOX) {
                // an inline-block, image or control: its own content
                gather(B, fr->box, depth + 1);
            }
        }
        // Children that are not part of the line boxes (floats, positioned boxes) still need a visit.
        for (struct box *c = b->first; c; c = c->next) {
            if (c->type == BT_TEXT || c->type == BT_BR) continue;
            if (c->inline_level && !c->out_of_flow && !c->is_float) {
                if (c->type == BT_INLINE) {
                    // may hold atomic boxes that were handled via frags, or floats
                    for (struct box *g = c->first; g; g = g->next) if (g->out_of_flow || g->is_float) gather(B, g, depth + 1);
                }
                continue;
            }
            gather(B, c, depth + 1);
        }
        return;
    }
    for (struct box *c = b->first; c; c = c->next) {
        if (c->type == BT_TEXT || c->type == BT_BR) continue;
        gather(B, c, depth + 1);
    }
}

void web_doc_fill(struct page_document *d, struct page *p) {
    struct doc D = { d, p, false };
    d->text_used = 1;
    d->text[0] = '\0';
    d->box_count = d->line_count = d->focusable_count = d->form_count = d->hidden_count = 0;
    d->link_count = d->heading_count = d->field_count = 0;
    d->title[0] = '\0';
    copy_ascii(d->title, (int)sizeof(d->title), p->title, (int)strlen(p->title));
    d->height = p->content_h;
    if (!p->root_box) return;
    // Paint once into a tiny canvas: that works out where every box is (and which are drawn at all).
    static uint32_t dry[16];
    struct gui_canvas c = { dry, 4, 4 };
    paint_page(p, &c, 0);
    make_focusables(&D);
    static unsigned char seen[MAX_FOCUSABLES + 1];
    for (int pass = 0; pass < 2; pass++) {
        memset(seen, 0, sizeof(seen));
        d->box_count = d->line_count = 0;
        struct builder B;
        memset(&B, 0, sizeof(B));
        B.D = &D; B.seen = seen;
        for (int i = 0; i < d->focusable_count; i++) d->focusables[i].line = 0;
        gather(&B, p->root_box, 0);
        // controls with no words of their own (a link around a picture with no description...)
        bool missing = false;
        for (int i = 0; i < d->focusable_count; i++) if (!seen[i]) { missing = true; break; }
        if (!missing || pass == 1) break;
        // second pass: add them at the end of the page, in order
        B.open = false;
        for (int i = 0; i < d->focusable_count; i++) {
            if (seen[i]) continue;
            struct node *n = d->focusables[i].node;
            struct box *bx = n ? n->box : NULL;
            if (!bx || bx->stack_order == 0) {
                // not drawn: still reachable by Tab, say its name
                add_item(&B, 0, p->content_h, 1, 1, "", 0, STYLE_BUTTON, i, 0, n);
                end_line(&B);
                continue;
            }
            uint16_t st = d->focusables[i].kind == FOCUS_FIELD ? STYLE_FIELD : (d->focusables[i].kind == FOCUS_LINK ? STYLE_LINK : STYLE_BUTTON);
            add_item(&B, bx->ax, bx->ay, bx->w, bx->h, "", 0, st, i, heading_level(n), n);
            end_line(&B);
        }
        break;
    }
    for (int i = 0; i < d->line_count; i++) if (d->lines[i].heading > 0) d->heading_count++;
    if (d->line_count == 0) {
        // nothing readable: one empty line keeps navigation simple
        struct page_line *l = &d->lines[d->line_count++];
        l->y = 0; l->h = 16; l->first_box = d->box_count; l->box_count = 0; l->heading = 0;
    }
}

// ---- Changing a control ----

// Press a check box, radio button or details summary. Returns true if the picture needs drawing again (or laying out).
bool web_doc_toggle(struct page_document *d, struct page *p, int f) {
    if (f < 0 || f >= d->focusable_count) return false;
    struct page_focusable *pf = &d->focusables[f];
    struct node *n = pf->node;
    if (!n) return false;
    if (pf->sub == SUB_CHECK) {
        pf->checked = !pf->checked;
        if (pf->checked) node_set_attr(p, n, "checked", ""); else node_remove_attr(n, "checked");
        return true;
    }
    if (pf->sub == SUB_RADIO) {
        // switch off the others with the same name in the same form
        for (int i = 0; i < d->focusable_count; i++) {
            struct page_focusable *q = &d->focusables[i];
            if (q->sub != SUB_RADIO || q->form != pf->form || q->name != pf->name || !q->node) continue;
            bool on = i == f;
            q->checked = on;
            if (on) node_set_attr(p, (struct node *)q->node, "checked", ""); else node_remove_attr((struct node *)q->node, "checked");
        }
        return true;
    }
    if (pf->sub == SUB_SUMMARY) {
        struct node *det = n->parent;
        if (det && det->tag == T_DETAILS) {
            if (node_has_attr(det, "open")) { node_remove_attr(det, "open"); det->open = false; }
            else { node_set_attr(p, det, "open", ""); det->open = true; }
            p->needs_layout = true;
        }
        return true;
    }
    return false;
}

// Choose the next choice in a menu.
void web_doc_cycle_select(struct page_document *d, struct page *p, int f) {
    if (f < 0 || f >= d->focusable_count) return;
    struct page_focusable *pf = &d->focusables[f];
    struct node *sel = pf->node;
    if (!sel || sel->tag != T_SELECT) return;
    struct node *opts[200]; int no = 0, cur = 0;
    for (struct node *o = sel->first; o && no < 199; o = o->next) {
        if (o->tag == T_OPTION) opts[no++] = o;
        else if (o->tag == T_OPTGROUP) for (struct node *q = o->first; q && no < 199; q = q->next) if (q->tag == T_OPTION) opts[no++] = q;
    }
    if (no == 0) return;
    for (int i = 0; i < no; i++) if (node_has_attr(opts[i], "selected")) cur = i;
    int next = (cur + 1) % no;
    for (int i = 0; i < no; i++) node_remove_attr(opts[i], "selected");
    node_set_attr(p, opts[next], "selected", "");
    char tb[200]; int tl = node_text(opts[next], tb, sizeof(tb));
    copy_ascii(pf->value, FIELD_MAX, tb, tl);
    const char *v = attr(opts[next], "value");
    (void)v;
}

// A text box was typed in: let the picture show it.
void web_doc_set_value(struct page_document *d, struct page *p, int f) {
    if (f < 0 || f >= d->focusable_count) return;
    struct page_focusable *pf = &d->focusables[f];
    if (pf->node && pf->kind == FOCUS_FIELD) node_set_attr(p, (struct node *)pf->node, "value", pf->value);
}

// The value to send for a menu: the chosen choice's value (or its words).
void web_doc_select_value(struct page_document *d, int f, char *out, int max) {
    out[0] = '\0';
    if (f < 0 || f >= d->focusable_count || !d->focusables[f].node) return;
    const struct node *o = selected_option(d->focusables[f].node);
    if (!o) return;
    const char *v = attr(o, "value");
    if (v) { copy_ascii(out, max, v, (int)strlen(v)); return; }
    char tb[200]; int tl = node_text(o, tb, sizeof(tb));
    copy_ascii(out, max, tb, tl);
}
