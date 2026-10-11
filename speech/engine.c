// engine.c - the AREOS speech engine: turns text into sound with Flite.
//
// Flite (festvox.org/flite, from Carnegie Mellon University) does the hard
// part: working out how words are said and making the voice. Four of its
// voices are built in (see speech_engine_voice_name).
//
// This file is the small layer between Flite and the AREOS kernel:
//   speech_engine_init()  set Flite up with a block of memory
//   speech_engine_say()   speak some text, handing back sound bit by bit
//
// Flite is compiled with floating point turned on (the rest of the kernel
// isn't), so only call these from the speech thread, which saves and
// restores the floating point registers.

#include <setjmp.h>
#include <string.h>
#include "flite.h"
#include "cst_audio.h"
#include "engine.h"
#include "libc/libc.h"

cst_voice *register_cmu_us_slt(const char *voxdir);
cst_voice *register_cmu_us_rms(const char *voxdir);
cst_voice *register_cmu_us_awb(const char *voxdir);
cst_voice *register_cmu_us_kal16(const char *voxdir);

// Ten voices from four recordings: each "higher" or "deeper" voice is the
// same recording spoken at a different pitch.
#define VOICE_COUNT 10
#define BASE_COUNT 4
static const struct { const char *name; int base; int pitch_percent; } voice_table[VOICE_COUNT] = {
    { "Sally (US English)",           0, 100 },   // Flite's "slt"
    { "Rob (US English)",             1, 100 },   // "rms"
    { "Alan (Scottish English)",      2, 100 },   // "awb"
    { "Kal (classic robot)",          3, 100 },   // "kal16"
    { "Ivy (higher Sally)",           0, 125 },
    { "Sam (deeper Sally)",           0,  78 },
    { "Finn (higher Rob)",            1, 140 },
    { "Dan (deeper Rob)",             1,  82 },
    { "Angus (deeper Alan)",          2,  80 },
    { "Kit (higher Kal)",             3, 135 },
};
static cst_voice *bases[BASE_COUNT];
static int voice_index;             // the one speaking
static cst_voice *voice;            // its recording
static cst_audio_streaming_info *streaming;
static speech_sound_fn current_sound;
static void *current_user;
static jmp_buf *escape;   // where to jump if Flite gives up
static const char *current_text;

_Noreturn void speech_libc_fatal(const char *why) {
    (void)why;
    if (escape) longjmp(*escape, 1);
    for (;;) { }
}

// Flite calls this as each piece of the sentence is ready.
static int got_sound(const cst_wave *wave, int start, int size, int last, cst_audio_streaming_info *asi) {
    (void)last; (void)asi;
    if (size > 0 && current_sound != NULL) {
        if (!current_sound(&wave->samples[start], size, current_user)) {
            return CST_AUDIO_STREAM_STOP;
        }
    }
    return CST_AUDIO_STREAM_CONT;
}

bool speech_engine_init(void *memory, size_t size) {
    speech_libc_set_memory(memory, size);
    jmp_buf here;
    if (setjmp(here)) {
        escape = NULL;
        cst_errjmp = NULL;
        voice = NULL;
        return false;
    }
    escape = &here;
    cst_errjmp = &here;

    flite_init();
    bases[0] = register_cmu_us_slt(NULL);
    bases[1] = register_cmu_us_rms(NULL);
    bases[2] = register_cmu_us_awb(NULL);
    bases[3] = register_cmu_us_kal16(NULL);
    streaming = new_audio_streaming_info();
    streaming->asc = got_sound;
    streaming->min_buffsize = 512;   // hand sound over in small pieces
    for (int i = 0; i < BASE_COUNT; i++) {
        if (bases[i] != NULL) {
            feat_set(bases[i]->features, "streaming_info", audio_streaming_info_val(streaming));
        }
    }
    voice = bases[0];

    escape = NULL;
    cst_errjmp = NULL;
    return voice != NULL;
}

// Kept apart from speech_engine_say so nothing it uses is a local variable
// that a longjmp (Flite giving up) could leave in a muddle.
static bool say_current(void) {
    jmp_buf here;
    if (setjmp(here)) {
        escape = NULL;
        cst_errjmp = NULL;
        current_sound = NULL;
        return false;
    }
    escape = &here;
    cst_errjmp = &here;

    cst_utterance *utterance = flite_synth_text(current_text, voice);
    if (utterance != NULL) delete_utterance(utterance);

    current_sound = NULL;
    escape = NULL;
    cst_errjmp = NULL;
    return true;
}

int speech_engine_voice_count(void) {
    return VOICE_COUNT;
}

const char *speech_engine_voice_name(int index) {
    return (index >= 0 && index < VOICE_COUNT) ? voice_table[index].name : "";
}

void speech_engine_set_voice(int index) {
    if (index >= 0 && index < VOICE_COUNT && bases[voice_table[index].base] != NULL) {
        voice_index = index;
        voice = bases[voice_table[index].base];
    }
}

int speech_engine_sample_rate(void) {
    return 16000;
}

bool speech_engine_say(const char *text, int speed_percent, speech_sound_fn sound, void *user) {
    if (voice == NULL) return false;

    // Faster speech = shorter sounds: 200% speed is a stretch of 0.5.
    if (speed_percent < 50) speed_percent = 50;
    if (speed_percent > 400) speed_percent = 400;
    feat_set_float(voice->features, "duration_stretch", 100.0f / (float)speed_percent);
    // Higher and deeper voices: shift the pitch.
    float shift = (float)voice_table[voice_index].pitch_percent / 100.0f;
    feat_set_float(voice->features, "f0_shift", shift);
    current_sound = sound;
    current_user = user;
    current_text = text;
    return say_current();
}
