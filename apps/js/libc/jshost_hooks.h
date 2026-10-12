// jshost_hooks.h - what the JavaScript engine's C library needs from its host.
#ifndef JSHOST_HOOKS_H
#define JSHOST_HOOKS_H
#include <stddef.h>
#include <stdint.h>

// Text the engine itself prints (internal diagnostics only).
void js_host_log(const char *text, size_t length);
// The engine hit something it can't recover from. Must not return.
_Noreturn void js_host_fatal(const char *why);
// Milliseconds since 1970 (see clib.c).
int64_t js_host_epoch_ms(void);

#endif
