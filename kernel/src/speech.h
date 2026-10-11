// speech.h - the built-in screen reader: AREOS reads its text out loud.
//
// Everything AREOS writes (the command line, and the desktop's
// descriptions of what has focus or what the mouse is over) goes through
// the console, which hands it here a character at a time. Finished lines
// are spoken; a line that's been sitting unfinished for a moment (like a
// prompt, or a letter you just typed) is spoken too.
//
// Keys:  Ctrl          stop talking
//        Ctrl+Alt+S    turn speech off or on
//        any other key also interrupts, so you never wait for it to finish

#ifndef AREOS_SPEECH_H
#define AREOS_SPEECH_H

#include <stdbool.h>
#include <stdint.h>

// Start the voice and the sound card. Prints a line saying how it went.
void speech_init(void);

// Text that's being written out (called by the console).
void speech_feed_char(char c);

// Say something without printing it.
void speech_say(const char *text);

// Stop talking now and forget anything waiting.
void speech_interrupt(void);

// Turn speech on or off (and say so).
void speech_toggle(void);
bool speech_enabled(void);

// The settings changed (speech on/off, speed): pick them up.
void speech_settings_changed(void);

// Called by the timer every millisecond.
void speech_tick(void);

#endif
