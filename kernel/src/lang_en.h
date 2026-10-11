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
#define STR_BOOT_MEMORY_1   "Memory: "
#define STR_BOOT_MEMORY_2   " MB ready."
#define STR_BOOT_TIMER      "Timer: ticking 1000 times a second."
#define STR_BOOT_APPS_1     "Apps: "
#define STR_BOOT_APPS_2     " found. Type apps to see them."
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

#define STR_CMD_MEMORY_SHORT    "Show memory use"
#define STR_CMD_MEMORY_LONG     "Shows how much RAM the computer has, how much is free, and how much the heap (AREOS's own memory pool) is using."

#define STR_CMD_MEMTEST_SHORT   "Test the memory system"
#define STR_CMD_MEMTEST_LONG    "Makes a few thousand allocations of different sizes, fills them, checks nothing got mixed up, then frees them all."

#define STR_CMD_CRASH_SHORT     "Show the crash screen"
#define STR_CMD_CRASH_LONG      "Crashes AREOS on purpose so you can see the crash screen. Type 'crashtest cpu' to make the CPU itself trip over a bad memory address instead. You'll need to restart QEMU afterwards."

#define STR_CMD_UPTIME_SHORT    "How long AREOS has been running"
#define STR_CMD_UPTIME_LONG     "Shows how long it's been since AREOS started, using the timer that ticks 1000 times a second."

#define STR_CMD_WAIT_SHORT      "Pause for some seconds"
#define STR_CMD_WAIT_LONG       "Waits for the number of seconds you give it, then carries on. Example: wait 3"

#define STR_CMD_APPS_SHORT      "List the apps"
#define STR_CMD_APPS_LONG       "Lists the .arx programs AREOS was started with, how big each is, and which format it uses (ELF inside, or native AREOS)."

#define STR_CMD_RUN_SHORT       "Run an app"
#define STR_CMD_RUN_LONG        "Runs an app, like 'run hello'. You can also just type the app's name. Apps run walled off from AREOS, so if one crashes, AREOS keeps going."

#define STR_CMD_DESKTOP_SHORT   "Open the desktop"
#define STR_CMD_DESKTOP_LONG    "Opens the AREOS desktop (the graphical side). Everything on it is also described here in text, for screen readers."

#define STR_CMD_SETUP_SHORT     "Run the setup screens again"
#define STR_CMD_SETUP_LONG      "Goes through the welcome and setup screens again, then opens the desktop."

// ---- Command output ----
#define STR_ABOUT_TEXT      "AREOS v0.1 - a hobby operating system built from scratch.\n" \
                            "Not Linux. Not Windows. Not macOS. Its own thing.\n" \
                            "No ads. No bloat. No telemetry.\n"
#define STR_RESTARTING      "Restarting... see you on the other side."
#define STR_TURNING_OFF     "Turning off. Goodbye!"
#define STR_OFF_FAILED      "This machine didn't turn off by itself. It's now safe to close the window."
#define STR_DOOM_NOT_YET    "Not yet! Doom is roadmap step 8. For now AREOS can't run Doom. That's... not it."

#define STR_MEM_RAM         "RAM:   "
#define STR_MEM_HEAP        "Heap:  "
#define STR_MEM_TOTAL       " total, "
#define STR_MEM_FREE        " free"
#define STR_MEM_USED_OF     " used of "
#define STR_MEMTEST_RUNNING "Testing memory..."
#define STR_MEMTEST_PASSED  "Memory test passed. Every byte was where it should be."
#define STR_MEMTEST_NO_MEM  "Memory test couldn't get enough memory to run."
#define STR_UPTIME_1        "Running for "
#define STR_UPTIME_HOURS    " h "
#define STR_UPTIME_MINUTES  " min "
#define STR_UPTIME_SECONDS  " s"
#define STR_WAIT_USAGE      "Tell me how many seconds, like: wait 3"
#define STR_WAIT_DONE       "Done waiting."
#define STR_APPS_NONE       "No apps were loaded."
#define STR_APPS_BYTES      " bytes"
#define STR_RUN_USAGE       "Which app? Like: run hello. Type apps to see them all."
#define STR_RUN_NOT_FOUND   "There's no app called \""
#define STR_RUN_NOT_FOUND_2 "\". Type apps to see them all."
#define STR_RUN_BAD_FORMAT  "That file isn't a program AREOS knows how to run."
#define STR_RUN_NO_MEMORY   "There isn't enough memory to run that."
#define STR_RUN_EXIT_CODE_1 " finished with exit code "
#define STR_RUN_CRASHED_1   " crashed: "
#define STR_RUN_CRASHED_2   "AREOS is fine, only the app was stopped."
#define STR_RUN_WHERE       "  At instruction: "
#define STR_RUN_ADDRESS     "  Tried to use address: "
#define STR_GRAPHICS_ON     "(Graphics mode on: the screen is showing a picture. Text keeps coming here.)"
#define STR_GRAPHICS_OFF    "(Graphics mode off: back to text.)"
#define STR_NO_DESKTOP      "The desktop app isn't on this disc."
#define STR_AUTOSTART       "Starting the desktop. (Type desktop at the prompt to come back to it later.)"
#define STR_CRASH_REASON    "You asked for a crash with the crashtest command. Everything is fine!"

// ---- Crash screen ----
#define STR_PANIC_TITLE     "Something went wrong, we had to stop the process."
#define STR_PANIC_CALM      "We're just catching our breath."
#define STR_PANIC_DETAILS   "Technical details (stays on this screen, nothing is sent anywhere):"
#define STR_PANIC_ERROR     "Error: "
#define STR_PANIC_WHERE     "Where: "
#define STR_PANIC_BAD_ADDRESS "Tried to use address: "
#define STR_PANIC_REGISTERS "CPU registers at the moment it happened:"
#define STR_CPU_ERROR_NAMES { \
    "Divided by zero", "Debug", "Hardware emergency (NMI)", "Breakpoint", \
    "Number too big (overflow)", "Out of bounds", "Unknown instruction", "Math chip not available", \
    "Double fault (an error while handling an error)", "Coprocessor segment overrun", "Bad task state", "Segment missing", \
    "Stack problem", "General protection fault (not allowed)", "Page fault (bad memory address)", "Reserved (15)", \
    "Math error (x87)", "Misaligned memory access", "Hardware failure (machine check)", "Math error (SIMD)", \
    "Virtualization error", "Control protection", "Reserved (22)", "Reserved (23)", \
    "Reserved (24)", "Reserved (25)", "Reserved (26)", "Reserved (27)", \
    "Hypervisor injection", "VMM communication", "Security error", "Reserved (31)" }
#define STR_PANIC_HINT      "Restart to try again. If it keeps happening, these details will help fix it."

#endif
