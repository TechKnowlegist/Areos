// web_gfx.h - putting pixels on the canvas: rectangles, rounded corners, gradients, shadows, letters.

#ifndef WEB_GFX_H
#define WEB_GFX_H

#include "web.h"
#ifndef WEB_HOST
#include "gui.h"
#endif

#define MAX_CLIPS 24

struct clip {
    int x0, y0, x1, y1;         // the rectangle (page coordinates, x1/y1 exclusive)
    bool round;
    int rx, ry, rw, rh;         // the rounded rectangle inside it
    int rad[8];                 // x radii then y radii of: top-left, top-right, bottom-right, bottom-left
    int depth;                  // tree depth of the box that clips (for out-of-flow boxes)
    const struct box *owner;
};

struct pctx {
    struct page *p;
    struct gui_canvas *c;
    int scroll_y;
    struct clip clips[MAX_CLIPS];
    int nclip;
    uint8_t group_alpha;        // 255: normal
};

// Colors are 0xAARRGGBB (not premultiplied); the canvas holds 0x00RRGGBB.
void gfx_fill(struct pctx *P, int x, int y, int w, int h, uint32_t color);
void gfx_fill_rrect(struct pctx *P, int x, int y, int w, int h, const int rad[8], uint32_t color);
// Draws only a ring: between the outer rounded rectangle and the one inset by the border widths. `sides` gives each side a color.
void gfx_border(struct pctx *P, int x, int y, int w, int h, const int rad[8], const int bw[4], const uint8_t style[4], const uint32_t color[4]);
void gfx_gradient(struct pctx *P, const struct gradient *g, int x, int y, int w, int h, int cl_x, int cl_y, int cl_w, int cl_h, const int rad[8], int tile_w, int tile_h);
void gfx_shadow(struct pctx *P, int x, int y, int w, int h, const int rad[8], const struct shadow *sh);
void gfx_text(struct pctx *P, int font, int px, int x64, int baseline, const char *text, int len, uint32_t color, int letter_spacing, int word_spacing);
// Draw a picture scaled to w x h at (x, y), clipped to (cx, cy, cw, ch).
void gfx_image(struct pctx *P, const uint32_t *pix, int iw, int ih, int x, int y, int w, int h, int cx, int cy, int cw, int ch, uint32_t tint_alpha);
void gfx_hline(struct pctx *P, int x, int y, int w, int thickness, uint32_t color);

void clip_push_rect(struct pctx *P, int x, int y, int w, int h);
void clip_push_rrect(struct pctx *P, int x, int y, int w, int h, const int rad[8]);
void clip_pop(struct pctx *P);
bool clip_empty(struct pctx *P);
void clip_bounds(struct pctx *P, int *x0, int *y0, int *x1, int *y1);

void gfx_group_begin(struct pctx *P, int x, int y, int w, int h, uint32_t **saved, int *sx, int *sy, int *sw, int *sh);
void gfx_group_end(struct pctx *P, uint32_t *saved, int sx, int sy, int sw, int sh, int alpha);

#endif
