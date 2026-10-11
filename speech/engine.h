// engine.h - the AREOS speech engine (Flite underneath).
// Shared by the speech engine library and the kernel.

#ifndef SPEECH_ENGINE_H
#define SPEECH_ENGINE_H

#include <stdbool.h>
#include <stddef.h>

// Called with each new piece of sound: 16-bit mono samples at
// speech_engine_sample_rate(). Return false to stop speaking early.
typedef bool (*speech_sound_fn)(const short *samples, int count, void *user);

// Set up the voice, using `memory` (at least 48 MB) for its working space.
bool speech_engine_init(void *memory, size_t size);

// Say `text`. speed_percent: 100 is normal, 200 twice as fast.
// Returns once it's all been handed to `sound` (or `sound` said stop).
bool speech_engine_say(const char *text, int speed_percent, speech_sound_fn sound, void *user);

int speech_engine_sample_rate(void);

// The voices: how many, what each is called, and which to speak with.
int speech_engine_voice_count(void);
const char *speech_engine_voice_name(int index);
void speech_engine_set_voice(int index);

#endif
