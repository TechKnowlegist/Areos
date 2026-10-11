// text.c - small helpers for working with text.

#include "text.h"

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

size_t text_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

bool text_equal_ignore_case(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (to_lower(a[i]) != to_lower(b[i])) {
            return false;
        }
        i++;
    }
    return a[i] == b[i];  // both must end at the same place
}

const char *text_skip_spaces(const char *text) {
    while (*text == ' ') {
        text++;
    }
    return text;
}
