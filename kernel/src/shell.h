// shell.h - the AREOS command line.

#ifndef AREOS_SHELL_H
#define AREOS_SHELL_H

// Show the prompt, read commands, run them. Never returns.
// If `autostart` names an app, it's run first (AREOS starts the desktop
// this way). Pass NULL to go straight to the prompt.
void shell_run(const char *autostart);

#endif
