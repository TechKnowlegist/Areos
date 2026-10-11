// settings.h - choices made in setup (theme, language, your name...).
//
// AREOS can't save to a disk yet, so settings live in memory: they last
// until the computer is turned off. Programs read and change them by
// number (see apps/include/areos.h for what each number means).

#ifndef AREOS_SETTINGS_H
#define AREOS_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define SETTINGS_COUNT 32

// What each setting number means. KEEP IN SYNC with apps/include/areos.h.
#define SETTING_SETUP_DONE   0   // 1 once the setup screens have been finished
#define SETTING_THEME        1   // 0 = dark, 1 = light
#define SETTING_LANGUAGE     2   // 0 = English
#define SETTING_VERBOSITY    3   // 0 = detailed announcements, 1 = brief
#define SETTING_WALLPAPER    4   // 0 = nebula, 1 = red planet, 2 = plain
#define SETTING_RETURN_TO    5   // desktop: which app it opened (+1), to come back to
#define SETTING_SPEECH_OFF   6   // 0 = AREOS speaks (the default), 1 = quiet
#define SETTING_SPEECH_SPEED 7   // 0 = normal (110%), otherwise a percentage (50-300)
#define SETTING_SPEECH_VOICE 12  // 0 Sally, 1 Rob, 2 Alan, 3 Kal
#define SETTING_MOUSE_SIZE   13  // 0 normal, 1 large, 2 extra large
#define SETTING_VOLUME       14  // 0 = full (never set), otherwise volume + 1 (1-101)
#define SETTING_ACCENT       15  // 0 green, 1 blue, 2 purple, 3 orange, 4 pink
#define SETTING_CLOCK_24H    16  // 1 = 24-hour clock
#define SETTING_NAME_FIRST   8   // 8-11: your name, 8 letters per setting

bool settings_get(uint64_t id, uint64_t *value);
bool settings_set(uint64_t id, uint64_t value);

#endif
