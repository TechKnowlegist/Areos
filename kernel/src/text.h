// text.h - small helpers for working with text.
//
// A real OS normally gets these from a C library, but AREOS has none
// yet, so we write the few we need ourselves.

#ifndef AREOS_TEXT_H
#define AREOS_TEXT_H

#include <stdbool.h>
#include <stddef.h>

// How many characters are in a string (not counting the ending '\0').
size_t text_length(const char *text);

// True if two strings match, ignoring upper/lower case ("HELP" == "help").
bool text_equal_ignore_case(const char *a, const char *b);

// True if `needle` appears anywhere in `haystack`.
bool text_contains(const char *haystack, const char *needle);

// Skip over any spaces at the start of a string.
const char *text_skip_spaces(const char *text);

#endif
