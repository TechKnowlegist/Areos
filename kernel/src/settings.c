// settings.c - choices made in setup, kept in memory.

#include "settings.h"

static uint64_t values[SETTINGS_COUNT];  // all start at 0

bool settings_get(uint64_t id, uint64_t *value) {
    if (id >= SETTINGS_COUNT) {
        return false;
    }
    *value = values[id];
    return true;
}

bool settings_set(uint64_t id, uint64_t value) {
    if (id >= SETTINGS_COUNT) {
        return false;
    }
    values[id] = value;
    return true;
}
