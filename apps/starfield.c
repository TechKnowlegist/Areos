// starfield.c - fly through space. The first AREOS graphics app.
//
// Stars rush toward you in AREOS green and blue. Up/Down change speed,
// Escape or Q quits. Everything it says also goes to the terminal, so
// you can follow along with a screen reader.
//
// No floating point here (AREOS apps don't have it yet), so the 3D
// maths uses whole numbers: a star at (x, y) and distance z appears on
// screen at (x / z, y / z), scaled up. Closer = bigger and brighter.

#include "areos.h"

#define WIDTH       320
#define HEIGHT      200
#define STAR_COUNT  300
#define FAR_AWAY    1024     // stars start this far away
#define SPACE_COLOR 0x0A0F14 // the AREOS background color
#define FRAME_MS    16       // about 60 pictures a second

struct star {
    int32_t x, y, z;
};

static uint32_t random_state = 20261011;

static int32_t random_between(int32_t low, int32_t high) {
    random_state = random_state * 1103515245 + 12345;
    return low + (int32_t)((random_state >> 8) % (uint32_t)(high - low + 1));
}

static void place_star(struct star *s, bool anywhere) {
    s->x = random_between(-2000, 2000);
    s->y = random_between(-1250, 1250);
    s->z = anywhere ? random_between(1, FAR_AWAY) : FAR_AWAY;
}

// Mix the star's color toward the background the further away it is.
static uint32_t star_color(int32_t z, bool blue) {
    uint32_t r = blue ? 0x4F : 0x3D, g = blue ? 0xC3 : 0xFF, b = blue ? 0xF7 : 0x9A;
    uint32_t near = (uint32_t)(FAR_AWAY - z);  // 0 = far, FAR_AWAY = right here
    r = (r * near + 0x0A * (uint32_t)z) / FAR_AWAY;
    g = (g * near + 0x0F * (uint32_t)z) / FAR_AWAY;
    b = (b * near + 0x14 * (uint32_t)z) / FAR_AWAY;
    return (r << 16) | (g << 8) | b;
}

static void put(uint32_t *pixels, int32_t x, int32_t y, uint32_t color) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
        pixels[y * WIDTH + x] = color;
    }
}

static void say_speed(int speed) {
    areos_print("Speed: ");
    areos_print_number((uint64_t)speed);
    areos_print("\n");
}

int main(void) {
    uint32_t *pixels = areos_more_memory(WIDTH * HEIGHT * sizeof(uint32_t));
    struct star *stars = areos_more_memory(STAR_COUNT * sizeof(struct star));
    if (pixels == NULL || stars == NULL) {
        areos_print("Not enough memory for the starfield.\n");
        return 1;
    }
    for (int i = 0; i < STAR_COUNT; i++) {
        place_star(&stars[i], true);
    }

    if (!areos_graphics_start(NULL, NULL)) {
        areos_print("Couldn't switch to graphics mode.\n");
        return 1;
    }
    areos_print("Starfield: flying through space in green and blue.\n");
    areos_print("Up and Down arrows change speed. Escape or Q to land.\n");

    int speed = 8;
    say_speed(speed);
    bool flying = true;

    while (flying) {
        // Handle every key since the last picture.
        struct areos_key_event event;
        while (areos_key_event(&event)) {
            if (!event.pressed) {
                continue;
            }
            if (event.key == AREOS_KEY_ESCAPE || event.key == 'q') {
                flying = false;
            } else if (event.key == AREOS_KEY_UP && speed < 40) {
                speed += 4;
                say_speed(speed);
            } else if (event.key == AREOS_KEY_DOWN && speed > 0) {
                speed -= 4;
                say_speed(speed);
            }
        }

        // Clear the sky.
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            pixels[i] = SPACE_COLOR;
        }

        // Move every star closer and draw it.
        for (int i = 0; i < STAR_COUNT; i++) {
            struct star *s = &stars[i];
            s->z -= speed;
            if (s->z <= 1) {
                place_star(s, false);
            }
            int32_t screen_x = WIDTH / 2 + s->x * 64 / s->z;
            int32_t screen_y = HEIGHT / 2 + s->y * 64 / s->z;
            if (screen_x < 0 || screen_x >= WIDTH || screen_y < 0 || screen_y >= HEIGHT) {
                place_star(s, false);  // flew past the edge: start a new one far away
                continue;
            }
            uint32_t color = star_color(s->z, i % 3 == 0);
            put(pixels, screen_x, screen_y, color);
            if (s->z < FAR_AWAY / 3) {  // close stars are bigger
                put(pixels, screen_x + 1, screen_y, color);
                put(pixels, screen_x, screen_y + 1, color);
                put(pixels, screen_x + 1, screen_y + 1, color);
            }
        }

        areos_graphics_draw(pixels, WIDTH, HEIGHT);
        areos_sleep_ms(FRAME_MS);
    }

    areos_graphics_stop();
    areos_set_color(AREOS_GREEN);
    areos_print("Landed safely. Thanks for flying AREOS.\n");
    return 0;
}
