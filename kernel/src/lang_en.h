// lang_en.h - every piece of text AREOS shows, in English.
//
// Code never writes words directly. It uses these labels instead, so a
// translation (like lang_es.h for Spanish) can be dropped in later
// without touching the code.

#ifndef AREOS_LANG_EN_H
#define AREOS_LANG_EN_H

// ---- Boot screen ----
#define STR_BOOT_TITLE      "AREOS v0.1"
#define STR_BOOT_SLOGAN     "It can run Doom. That's it."
#define STR_DEV_WARNING_1   "DEVELOPMENT BUILD: things are broken."
#define STR_DEV_WARNING_2   "Do NOT install on a real machine. Use an emulator."
#define STR_REAL_MACHINE    "If you're seeing this on a real machine, that's not good."
#define STR_SHELL_BANNER    "Delete everything or fix everything."
#define STR_SHELL_HINT      "Type help to get started."

// ---- Shell ----
#define STR_PROMPT              "areos> "
#define STR_UNKNOWN_COMMAND_1   "I don't know the command \""
#define STR_UNKNOWN_COMMAND_2   "\". Type help to see what I can do."

// ---- help ----
#define STR_HELP_HEADER     "Here's what I can do. Type help <command> for more."
#define STR_HELP_ALIASES    "Also works as: "
#define STR_HELP_NOT_FOUND  "There's no command called \""

// ---- Command descriptions: short (for the list) and long (for help <command>) ----
#define STR_CMD_HELP_SHORT      "Show this list"
#define STR_CMD_HELP_LONG       "Lists every command. Type help and a command name, like 'help clear', for details about that one."

#define STR_CMD_CLEAR_SHORT     "Wipe the screen"
#define STR_CMD_CLEAR_LONG      "Clears everything off the screen and moves back to the top. Nothing is deleted, it just tidies up."

#define STR_CMD_SAY_SHORT       "Repeat what you type"
#define STR_CMD_SAY_LONG        "Prints back whatever comes after it. Example: say hello there"

#define STR_CMD_ABOUT_SHORT     "About this system"
#define STR_CMD_ABOUT_LONG      "Shows the AREOS version and a little about it."

#define STR_CMD_RESTART_SHORT   "Restart the computer"
#define STR_CMD_RESTART_LONG    "Restarts the (virtual) computer, like pressing the reset button."

#define STR_CMD_OFF_SHORT       "Turn the computer off"
#define STR_CMD_OFF_LONG        "Turns off the virtual computer. In QEMU this closes the window."

#define STR_CMD_DOOM_SHORT      "Run Doom"
#define STR_CMD_DOOM_LONG       "The whole reason AREOS exists. Coming in roadmap step 8."

// ---- Command output ----
#define STR_ABOUT_TEXT      "AREOS v0.1 - a hobby operating system built from scratch.\n" \
                            "Not Linux. Not Windows. Not macOS. Its own thing.\n" \
                            "No ads. No bloat. No telemetry.\n"
#define STR_RESTARTING      "Restarting... see you on the other side."
#define STR_TURNING_OFF     "Turning off. Goodbye!"
#define STR_OFF_FAILED      "This machine didn't turn off by itself. It's now safe to close the window."
#define STR_DOOM_NOT_YET    "Not yet! Doom is roadmap step 8. For now AREOS can't run Doom. That's... not it."

#endif
