// wallpaper.c - painting the space wallpaper.
//
// There are no image files: the wallpaper is calculated.
//   - Soft "clouds" (the nebula) come from noise: random values on a
//     coarse grid, smoothly blended between grid points, with a few
//     sizes of grid layered together so it looks natural.
//   - Stars are random dots, a few with a little sparkle.
//   - The red planet is a circle lit from one side, with a glowing edge.
// It's worked out once (when the look changes), then copied each frame.

#include "desktop.h"

static struct gui_canvas wall;   // the finished wallpaper
static uint32_t *small_pixels;   // the nebula is worked out at half size, then doubled
static int small_w, small_h;

void wallpaper_init(int width, int height) {
    wall.width = width;
    wall.height = height;
    wall.pixels = areos_more_memory((size_t)width * height * 4);
    small_w = width / 2 + 1;
    small_h = height / 2 + 1;
    small_pixels = areos_more_memory((size_t)small_w * small_h * 4);
}

void wallpaper_draw(struct gui_canvas *to) {
    if (wall.pixels != NULL) {
        for (int y = 0; y < to->height && y < wall.height; y++) {
            uint32_t *dest = to->pixels + (size_t)y * to->width;
            const uint32_t *source = wall.pixels + (size_t)y * wall.width;
            for (int x = 0; x < to->width && x < wall.width; x++) {
                dest[x] = source[x];
            }
        }
    }
}

// ---- Noise ----

// A repeatable "random" number 0-255 for a grid point.
static int grid_value(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)((h ^ (h >> 16)) & 0xFF);
}

// Smooth value noise at (x, y) with grid cells `cell` pixels wide. 0-255.
static int smooth_noise(int x, int y, int cell, int seed) {
    int gx = x / cell, gy = y / cell;
    int fx = x % cell, fy = y % cell;
    // "Smoothstep" so the blend between grid points has no hard edges.
    int sx = fx * fx * (3 * cell - 2 * fx) / (cell * cell);
    int sy = fy * fy * (3 * cell - 2 * fy) / (cell * cell);
    int a = grid_value(gx, gy, seed), b = grid_value(gx + 1, gy, seed);
    int c = grid_value(gx, gy + 1, seed), d = grid_value(gx + 1, gy + 1, seed);
    int top = a + (b - a) * sx / cell;
    int bottom = c + (d - c) * sx / cell;
    return top + (bottom - top) * sy / cell;
}

// Layer big, medium and small noise for natural-looking clouds. 0-255.
static int cloud(int x, int y, int seed) {
    return (smooth_noise(x, y, 96, seed) * 4 + smooth_noise(x, y, 40, seed + 1) * 2
            + smooth_noise(x, y, 16, seed + 2)) / 7;
}

static int clamp255(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

// ---- Colors for each look ----

struct palette {
    uint32_t top, bottom;        // sky gradient
    uint32_t cloud1, cloud2;     // nebula tints (green-ish, blue-ish)
    int cloud_strength;          // 0-255
    uint32_t star;
    uint32_t planet_light, planet_dark, planet_glow;
};

static const struct palette dark_palette = {
    .top = 0x05080E, .bottom = 0x0B1622,
    .cloud1 = 0x1C8C5E, .cloud2 = 0x2A78B0, .cloud_strength = 150,
    .star = 0xE8F4FF,
    .planet_light = 0xD0643A, .planet_dark = 0x2A0E08, .planet_glow = 0xFF8A5C,
};

static const struct palette light_palette = {
    .top = 0xD8E6EF, .bottom = 0xF3F8FB,
    .cloud1 = 0x8FE0BA, .cloud2 = 0x8EC8EE, .cloud_strength = 130,
    .star = 0x7A9AB0,
    .planet_light = 0xE88A62, .planet_dark = 0xA0472E, .planet_glow = 0xFFC2A6,
};

static uint32_t mix(uint32_t a, uint32_t b, int amount) {  // amount 0-255 of b
    return gui_blend(a, b, amount);
}

// ---- Painting ----

static void paint_sky_and_clouds(const struct palette *p, int strength) {
    for (int y = 0; y < small_h; y++) {
        uint32_t sky = mix(p->top, p->bottom, y * 255 / small_h);
        for (int x = 0; x < small_w; x++) {
            uint32_t color = sky;
            if (strength > 0) {
                // Two cloud layers; only the brighter parts of each show.
                int c1 = clamp255((cloud(x, y, 11) - 118) * 3);
                int c2 = clamp255((cloud(x + 300, y, 29) - 122) * 3);
                // A gentle diagonal band so the nebula has a shape.
                int band = 255 - clamp255((x - y * 2 + 40) > 0 ? (x - y * 2 + 40) / 2 : -(x - y * 2 + 40) / 2);
                c1 = c1 * (band + 80) / 335;
                color = mix(color, p->cloud1, c1 * strength / 255);
                color = mix(color, p->cloud2, c2 * strength / 340);
            }
            small_pixels[y * small_w + x] = color;
        }
    }
    // Double it up to full size.
    for (int y = 0; y < wall.height; y++) {
        const uint32_t *source = small_pixels + (y / 2) * small_w;
        uint32_t *dest = wall.pixels + (size_t)y * wall.width;
        for (int x = 0; x < wall.width; x++) {
            dest[x] = source[x / 2];
        }
    }
}

static void paint_stars(const struct palette *p, int count) {
    uint32_t seed = 777;
    for (int i = 0; i < count; i++) {
        seed = seed * 1103515245u + 12345u;
        int x = (int)((seed >> 8) % (uint32_t)wall.width);
        seed = seed * 1103515245u + 12345u;
        int y = (int)((seed >> 8) % (uint32_t)wall.height);
        seed = seed * 1103515245u + 12345u;
        int brightness = 60 + (int)((seed >> 8) % 196);
        uint32_t *pixel = &wall.pixels[(size_t)y * wall.width + x];
        *pixel = mix(*pixel, p->star, brightness);
        if (brightness > 235) {
            // A bright star gets a little cross-shaped sparkle.
            for (int d = 1; d <= 3; d++) {
                int fade = brightness * (4 - d) / 5;
                if (x - d >= 0) wall.pixels[(size_t)y * wall.width + x - d] =
                    mix(wall.pixels[(size_t)y * wall.width + x - d], p->star, fade);
                if (x + d < wall.width) wall.pixels[(size_t)y * wall.width + x + d] =
                    mix(wall.pixels[(size_t)y * wall.width + x + d], p->star, fade);
                if (y - d >= 0) wall.pixels[(size_t)(y - d) * wall.width + x] =
                    mix(wall.pixels[(size_t)(y - d) * wall.width + x], p->star, fade);
                if (y + d < wall.height) wall.pixels[(size_t)(y + d) * wall.width + x] =
                    mix(wall.pixels[(size_t)(y + d) * wall.width + x], p->star, fade);
            }
        }
    }
}

// A planet lit from the top left, with surface markings and a glowing rim.
static void paint_planet(const struct palette *p, int cx, int cy, int radius) {
    int glow = radius / 8 + 6;
    for (int y = cy - radius - glow; y <= cy + radius + glow; y++) {
        if (y < 0 || y >= wall.height) continue;
        for (int x = cx - radius - glow; x <= cx + radius + glow; x++) {
            if (x < 0 || x >= wall.width) continue;
            int dx = x - cx, dy = y - cy;
            int distance2 = dx * dx + dy * dy;
            uint32_t *pixel = &wall.pixels[(size_t)y * wall.width + x];
            if (distance2 > radius * radius) {
                // The atmosphere's glow just outside the edge.
                int outer = radius + glow;
                if (distance2 < outer * outer) {
                    int edge = (outer * outer - distance2) * 110 / (outer * outer - radius * radius);
                    *pixel = mix(*pixel, p->planet_glow, edge);
                }
                continue;
            }
            // Light comes from the top left: brightness depends on direction.
            int light = 128 + (-dx - dy) * 128 / (radius * 2);
            // Darker towards the edge, like a ball.
            int edge_dark = distance2 * 90 / (radius * radius);
            int markings = (cloud(x * 2, y * 4, 51) - 128) / 3;  // stripy surface
            int shade = clamp255(light - edge_dark + markings + 40);
            *pixel = mix(p->planet_dark, p->planet_light, shade);
        }
    }
}

void wallpaper_build(enum wallpaper_kind kind, bool light) {
    if (wall.pixels == NULL || small_pixels == NULL) {
        return;
    }
    const struct palette *p = light ? &light_palette : &dark_palette;
    switch (kind) {
    case WALLPAPER_NEBULA:
        paint_sky_and_clouds(p, p->cloud_strength);
        paint_stars(p, 900);
        paint_planet(p, wall.width - 210, wall.height - TASKBAR_H - 150, 90);
        break;
    case WALLPAPER_PLANET:
        paint_sky_and_clouds(p, p->cloud_strength / 3);
        paint_stars(p, 600);
        paint_planet(p, wall.width * 2 / 3, wall.height * 3 / 4, wall.height / 2);
        break;
    case WALLPAPER_PLAIN:
        paint_sky_and_clouds(p, 0);
        break;
    }
}
