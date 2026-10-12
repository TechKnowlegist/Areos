#include <time.h>
// render.c - draw a web page to a picture on an ordinary computer (for testing the engine).
//   render page.html width [out.ppm] [scroll_h]
#include "../desktop/browser_doc.h"
#include "../desktop/web.h"
#include <unistd.h>
#include <limits.h>

static unsigned char *slurp(const char *path, int *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz + 1);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    b[sz] = 0; fclose(f); *n = (int)sz;
    return b;
}

static void dump_boxes(struct box *b, int ox, int oy, int depth) {
    if (depth > 12) return;
    int x = ox + b->x, y = oy + b->y;
    const char *nm = b->node ? b->node->name : (b->type == BT_TEXT ? "#text" : "anon");
    if (b->type != BT_TEXT) fprintf(stderr, "%*s%s type=%d [%d,%d %dx%d]\n", depth * 2, "", nm ? nm : "?", b->type, x, y, b->w, b->h);
    for (struct box *c = b->first; c; c = c->next) dump_boxes(c, (b->type == BT_BLOCK || b->type == BT_REPLACED) ? x : ox, (b->type == BT_BLOCK || b->type == BT_REPLACED) ? y : oy, depth + 1);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: render page.html width [out.ppm]\n"); return 1; }
    int n; unsigned char *html = slurp(argv[1], &n);
    if (!html) { fprintf(stderr, "can't read %s\n", argv[1]); return 1; }
    int W = atoi(argv[2]);
    const char *outp = argc > 3 ? argv[3] : "/tmp/out.ppm";
    bool dump = getenv("DUMP") != NULL;
    char abs[PATH_MAX]; if (!realpath(argv[1], abs)) strcpy(abs, argv[1]);
    char url[PATH_MAX + 16]; snprintf(url, sizeof(url), "file://%s", abs);
    struct page *p = web_page_new();
    web_page_load(p, (const char *)html, n, url, false);
    struct resource *r;
    while ((r = web_next_resource(p))) {
        int len = 0; unsigned char *d = NULL;
        if (!strncmp(r->url, "file://", 7)) {
            char path[PATH_MAX]; snprintf(path, sizeof(path), "%s", r->url + 7);
            char *q = strchr(path, '?'); if (q) *q = 0;
            d = slurp(path, &len);
        }
        web_resource_arrived(p, r, d, len, NULL);
        free(d);
    }
    int H = argc > 4 ? atoi(argv[4]) : 800;
    for (int round = 0; round < 6; round++) {
        web_page_layout(p, W, H, getenv("DARK") != NULL);
        // pictures (and anything else the layout found) are fetched after it
        bool any = false;
        while ((r = web_next_resource(p))) {
            int len = 0; unsigned char *d = NULL;
            if (!strncmp(r->url, "file://", 7)) { char path[PATH_MAX]; snprintf(path, sizeof(path), "%s", r->url + 7); char *q = strchr(path, '?'); if (q) *q = 0; d = slurp(path, &len); }
            web_resource_arrived(p, r, d, len, NULL); free(d); any = true;
        }
        if (!any) break;
    }
    if (dump && p->root_box) dump_boxes(p->root_box, 0, 0, 0);
    if (getenv("BENCH")) {
        #include <time.h>
        struct gui_canvas bc; bc.width = W; bc.height = 700; bc.pixels = malloc((size_t)W * 700 * 4);
        clock_t t0 = clock();
        for (int i = 0; i < 20; i++) web_page_paint(p, &bc, 5000 + i * 3);
        double pt = (double)(clock() - t0) / CLOCKS_PER_SEC / 20;
        t0 = clock();
        for (int i = 0; i < 3; i++) web_page_layout(p, W, H, false);
        double lt = (double)(clock() - t0) / CLOCKS_PER_SEC / 3;
        fprintf(stderr, "paint %.1f ms, layout %.1f ms\n", pt * 1000, lt * 1000);
    }
    int ch = p->content_h > 0 ? p->content_h : H;
    if (ch > 20000) ch = 20000;
    struct gui_canvas c; c.width = W; c.height = ch; c.pixels = malloc((size_t)W * ch * 4);
    web_page_paint(p, &c, 0);
    if (getenv("RECTS")) {
        // every box-making element: tag:occurrence x y w h
        struct node *stack[4096]; int sp = 0; struct { const char *t; int n; } counts[200]; int nc = 0;
        stack[sp++] = p->root;
        FILE *rf = fopen(getenv("RECTS"), "w");
        while (sp) {
            struct node *nd = stack[--sp];
            if (nd->type == N_ELEMENT) {
                int k; for (k = 0; k < nc; k++) if (!strcmp(counts[k].t, nd->name)) break;
                if (k == nc && nc < 200) { counts[nc].t = nd->name; counts[nc].n = 0; nc++; }
                int occ = counts[k].n++;
                struct box *b = nd->box;
                if (b && (b->type == BT_BLOCK || b->type == BT_REPLACED) && b->stack_order > 0 && b->node == nd)
                    fprintf(rf, "%s:%d %d %d %d %d\n", nd->name, occ, b->ax, b->ay, b->w, b->h);
            }
            struct node *kids[512]; int kn = 0;
            for (struct node *c = nd->first; c && kn < 512; c = c->next) kids[kn++] = c;
            for (int i = kn - 1; i >= 0 && sp < 4090; i--) stack[sp++] = kids[i];
        }
        fclose(rf);
    }
    if (getenv("DOC")) {
        struct page_document *d = calloc(1, sizeof(*d));
        d->text = malloc(TEXT_STORE); d->boxes = malloc(sizeof(struct page_box) * MAX_BOXES);
        d->lines = malloc(sizeof(struct page_line) * MAX_LINES); d->focusables = malloc(sizeof(struct page_focusable) * MAX_FOCUSABLES);
        extern void web_doc_fill(struct page_document *, struct page *);
        clock_t t0 = clock();
        web_doc_fill(d, p);
        fprintf(stderr, "doc fill %.1f ms\n", (double)(clock() - t0) / CLOCKS_PER_SEC * 1000);
        printf("TITLE: %s  lines=%d boxes=%d focus=%d links=%d headings=%d fields=%d forms=%d height=%d\n", d->title, d->line_count, d->box_count, d->focusable_count, d->link_count, d->heading_count, d->field_count, d->form_count, d->height);
        for (int i = 0; i < d->line_count; i++) {
            struct page_line *l = &d->lines[i];
            printf("%4d y=%-5d h=%-3d %s", i, l->y, l->h, l->heading ? "H" : " ");
            if (l->heading) printf("%d", l->heading); else printf(" ");
            printf(" | ");
            for (int b = l->first_box; b < l->first_box + l->box_count; b++) {
                struct page_box *bx = &d->boxes[b];
                if (bx->focusable >= 0) printf("[%s%d:", d->focusables[bx->focusable].kind == FOCUS_LINK ? "L" : d->focusables[bx->focusable].kind == FOCUS_FIELD ? "F" : "B", bx->focusable);
                printf("%.*s", bx->length, d->text + bx->text);
                if (bx->focusable >= 0) printf("]");
                printf(" ");
            }
            printf("\n");
        }
        for (int i = 0; i < d->focusable_count; i++) {
            struct page_focusable *f = &d->focusables[i];
            printf("focus %d kind=%d sub=%d label='%s' target='%s' value='%s' line=%d form=%d\n", i, f->kind, f->sub, d->text + f->label, f->target >= 0 ? d->text + f->target : "", f->value, f->line, f->form);
        }
    }
    FILE *o = fopen(outp, "wb");
    fprintf(o, "P6 %d %d 255\n", W, ch);
    for (int i = 0; i < W * ch; i++) { unsigned v = c.pixels[i]; fputc((v >> 16) & 255, o); fputc((v >> 8) & 255, o); fputc(v & 255, o); }
    fclose(o);
    if (getenv("CANVAS")) fprintf(stderr, "canvas %08x from_body=%d body=%p bodybg=%08x\n", p->canvas_color, p->canvas_from_body, (void*)p->body, p->body && p->body->style ? p->body->style->bg_color : 0);
    fprintf(stderr, "content %dx%d, styles ready=%d, images pending=%d\n", p->content_w, p->content_h, web_styles_ready(p), web_pending_count(p, true));
    return 0;
}
