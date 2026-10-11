// sound.h - playing sound through the sound card.
//
// Sound is 16-bit samples, 16,000 a second, one channel (what the speech
// engine makes). Queue it up; the timer keeps the card fed.

#ifndef AREOS_SOUND_H
#define AREOS_SOUND_H

#include <stdbool.h>
#include <stdint.h>

// Find and start the sound card. False if there isn't one.
bool sound_init(void);
bool sound_available(void);

// How many samples fit in the queue right now.
uint32_t sound_room(void);
// Add samples to play (no more than sound_room()).
void sound_queue(const int16_t *samples, uint32_t count);
// Throw away everything still waiting to play.
void sound_flush(void);
// Is anything still waiting to play?
bool sound_busy(void);

// Loudness, 0 (silent) to 100 (full).
void sound_set_volume(int percent);

// Called by the timer: keeps the card's buffers topped up.
void sound_tick(void);

#endif
