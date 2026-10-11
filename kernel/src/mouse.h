// mouse.h - AREOS mouse driver (PS/2).
//
// The mouse doesn't say where it is, only how far it moved since last
// time ("3 right, 1 up") and which buttons are held. Each report comes
// in three bytes, delivered by hardware interrupt 12.

#ifndef AREOS_MOUSE_H
#define AREOS_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

#define MOUSE_BUTTON_LEFT    0x1
#define MOUSE_BUTTON_RIGHT   0x2
#define MOUSE_BUTTON_MIDDLE  0x4

struct mouse_event {
    int16_t dx;        // movement right (negative = left)
    int16_t dy;        // movement DOWN (negative = up), screen-style
    uint8_t buttons;   // MOUSE_BUTTON_ flags held right now
};

void mouse_init(void);

// If the mouse moved or a button changed, describe it in *out and return true.
bool mouse_try_get_event(struct mouse_event *out);

#endif
