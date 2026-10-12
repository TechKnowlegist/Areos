#include "../desktop/web_font.h"
#include <stdio.h>
int main(void) {
    int f = wfont_pick(WF_SANS, false, false);
    const char *t = "Hello, World! Quick brown fox \xe2\x9c\x93 \xce\xb1\xce\xb2";
    int px = 32, W = 700, H = 60;
    unsigned char *img = calloc(W * H, 1);
    int pen = 10 * 64, base = 40;
    const char *p = t, *end = t + strlen(t);
    while (p < end) {
        uint32_t cp = w_utf8_next(&p, end);
        const struct wglyph *g = wfont_glyph(f, px, cp);
        if (g && g->alpha) for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++) {
            int X = pen / 64 + g->x + x, Y = base + g->y + y;
            if (X >= 0 && X < W && Y >= 0 && Y < H) img[Y * W + X] = g->alpha[y * g->w + x];
        }
        pen += wfont_advance(f, px, cp);
    }
    printf("width %d\n", wfont_text_width(f, px, t, (int)strlen(t)) / 64);
    FILE *o = fopen("/tmp/font.pgm", "wb"); fprintf(o, "P5 %d %d 255\n", W, H); fwrite(img, 1, W * H, o); fclose(o);
    return 0;
}
