// start.c - where every AREOS program begins.
//
// AREOS jumps to _start. It calls your main(), then exits with whatever
// main() returned. Every app is linked with this file.

#include "areos.h"

int main(void);

__attribute__((noreturn)) void _start(void) {
    areos_exit(main());
}
