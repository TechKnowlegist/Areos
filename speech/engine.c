// engine.c - the AREOS speech engine: turns text into sound with Flite.
//
// Flite (festvox.org/flite, from Carnegie Mellon University) does the hard
// part: working out how words are said and making the voice. We use its
// "slt" voice, a clear US English female voice.
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

static cst_voice *voice;
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
    voice = register_cmu_us_slt(NULL);
    if (voice != NULL) {
        streaming = new_audio_streaming_info();
        streaming->asc = got_sound;
        streaming->min_buffsize = 512;   // hand sound over in small pieces
        feat_set(voice->features, "streaming_info", audio_streaming_info_val(streaming));
    }

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

int speech_engine_sample_rate(void) {
    return 16000;
}

bool speech_engine_say(const char *text, int speed_percent, speech_sound_fn sound, void *user) {
    if (voice == NULL) return false;

    // Faster speech = shorter sounds: 200% speed is a stretch of 0.5.
    if (speed_percent < 50) speed_percent = 50;
    if (speed_percent > 400) speed_percent = 400;
    feat_set_float(voice->features, "duration_stretch", 100.0f / (float)speed_percent);
    current_sound = sound;
    current_user = user;
    current_text = text;
    return say_current();
}
