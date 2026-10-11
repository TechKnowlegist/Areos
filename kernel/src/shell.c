// shell.c - the AREOS command line.
//
// Every command has one AREOS name (plain English) plus "aliases":
// the names Windows and Linux use for the same thing. So "clear",
// "cls" all do the same job, and people (and AIs) coming from either
// world can just type what they already know.

#include <stddef.h>
#include <stdint.h>
#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "text.h"
#include "io.h"
#include "cpu.h"
#include "heap.h"
#include "pmm.h"
#include "panic.h"
#include "lang_en.h"

#define LINE_MAX_LENGTH   128  // longest command line you can type
#define MAX_ALIASES       4
#define HELP_NAME_COLUMN  10   // width of the name column in the help list

typedef void (*command_handler)(const char *args);

struct command {
    const char *name;                    // the AREOS name
    const char *aliases[MAX_ALIASES];    // Windows / Linux / other names
    const char *summary;                 // one line, for the help list
    const char *details;                 // longer text, for help <command>
    command_handler run;                 // the code that does the work
};

// ---- The commands themselves ----

static void command_help(const char *args);

static void command_clear(const char *args) {
    (void)args;
    console_clear();
}

static void command_say(const char *args) {
    console_print(args);
    console_print("\n");
}

static void command_about(const char *args) {
    (void)args;
    console_print(STR_ABOUT_TEXT);
}

static void command_restart(const char *args) {
    (void)args;
    console_print(STR_RESTARTING "\n");
    // Ask the keyboard controller to pulse the CPU's reset line.
    // (Odd but true: this is the classic way PCs reboot.)
    outb(0x64, 0xFE);
    cpu_halt_forever();
}

static void command_off(const char *args) {
    (void)args;
    console_print(STR_TURNING_OFF "\n");
    // QEMU's virtual power button. Real hardware needs ACPI (much later).
    outw(0x604, 0x2000);
    // If we're still here, the machine didn't power off.
    console_set_color(COLOR_WARNING);
    console_print(STR_OFF_FAILED "\n");
    cpu_halt_forever();
}

static void command_doom(const char *args) {
    (void)args;
    console_print(STR_DOOM_NOT_YET "\n");
}

// Print a byte count in a friendly unit (KB or MB).
static void print_size(uint64_t bytes) {
    if (bytes >= 1024 * 1024) {
        console_print_number(bytes / (1024 * 1024));
        console_print(" MB");
    } else {
        console_print_number(bytes / 1024);
        console_print(" KB");
    }
}

static void command_memory(const char *args) {
    (void)args;
    console_print(STR_MEM_RAM);
    print_size(pmm_total_bytes());
    console_print(STR_MEM_TOTAL);
    print_size(pmm_free_bytes());
    console_print(STR_MEM_FREE "\n");

    console_print(STR_MEM_HEAP);
    print_size(heap_used_bytes());
    console_print(STR_MEM_USED_OF);
    print_size(heap_total_bytes());
    console_print("\n");
}

#define MEMTEST_COUNT 2000

static void command_memtest(const char *args) {
    (void)args;
    static uint8_t *blocks[MEMTEST_COUNT];
    static size_t sizes[MEMTEST_COUNT];
    uint64_t used_before = heap_used_bytes();
    uint32_t random = 12345;  // simple pseudo-random sizes, same every run

    console_print(STR_MEMTEST_RUNNING "\n");

    // Allocate lots of differently-sized blocks, each filled with its own pattern.
    for (size_t i = 0; i < MEMTEST_COUNT; i++) {
        random = random * 1103515245 + 12345;
        sizes[i] = 1 + (random >> 8) % 8192;
        if (i % 500 == 0) {
            sizes[i] = 1024 * 1024;  // a few big ones too
        }
        blocks[i] = kmalloc(sizes[i]);
        if (blocks[i] == NULL) {
            console_set_color(COLOR_WARNING);
            console_print(STR_MEMTEST_NO_MEM "\n");
            for (size_t j = 0; j < i; j++) {
                kfree(blocks[j]);
            }
            return;
        }
        for (size_t b = 0; b < sizes[i]; b++) {
            blocks[i][b] = (uint8_t)(i + b);
        }
    }

    // Check that no block got overwritten by another one.
    for (size_t i = 0; i < MEMTEST_COUNT; i++) {
        for (size_t b = 0; b < sizes[i]; b++) {
            ASSERT(blocks[i][b] == (uint8_t)(i + b));
        }
    }

    // Free in a scrambled order (evens, then odds) to exercise merging.
    for (size_t i = 0; i < MEMTEST_COUNT; i += 2) {
        kfree(blocks[i]);
    }
    for (size_t i = 1; i < MEMTEST_COUNT; i += 2) {
        kfree(blocks[i]);
    }

    // Everything should be handed back.
    ASSERT(heap_used_bytes() == used_before);

    console_set_color(COLOR_GREEN);
    console_print(STR_MEMTEST_PASSED "\n");
}

static void command_crashtest(const char *args) {
    (void)args;
    PANIC(STR_CRASH_REASON);
}

// ---- The command list ----
// Add new commands here. The help list is built from this automatically.

static const struct command commands[] = {
    { "help",    { "?", "commands" },               STR_CMD_HELP_SHORT,    STR_CMD_HELP_LONG,    command_help },
    { "clear",   { "cls" },                         STR_CMD_CLEAR_SHORT,   STR_CMD_CLEAR_LONG,   command_clear },
    { "say",     { "echo" },                        STR_CMD_SAY_SHORT,     STR_CMD_SAY_LONG,     command_say },
    { "about",   { "ver", "uname", "version" },     STR_CMD_ABOUT_SHORT,   STR_CMD_ABOUT_LONG,   command_about },
    { "restart", { "reboot" },                      STR_CMD_RESTART_SHORT, STR_CMD_RESTART_LONG, command_restart },
    { "off",     { "shutdown", "poweroff" },        STR_CMD_OFF_SHORT,     STR_CMD_OFF_LONG,     command_off },
    { "memory",  { "mem", "free" },                 STR_CMD_MEMORY_SHORT,  STR_CMD_MEMORY_LONG,  command_memory },
    { "memtest", { 0 },                             STR_CMD_MEMTEST_SHORT, STR_CMD_MEMTEST_LONG, command_memtest },
    { "crashtest", { 0 },                           STR_CMD_CRASH_SHORT,   STR_CMD_CRASH_LONG,   command_crashtest },
    { "doom",    { 0 },                             STR_CMD_DOOM_SHORT,    STR_CMD_DOOM_LONG,    command_doom },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

// Find a command by its name or any of its aliases. Returns NULL if none.
static const struct command *find_command(const char *word) {
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (text_equal_ignore_case(word, commands[i].name)) {
            return &commands[i];
        }
        for (size_t a = 0; a < MAX_ALIASES && commands[i].aliases[a] != NULL; a++) {
            if (text_equal_ignore_case(word, commands[i].aliases[a])) {
                return &commands[i];
            }
        }
    }
    return NULL;
}

static void print_aliases(const struct command *cmd) {
    if (cmd->aliases[0] == NULL) {
        return;
    }
    console_set_color(COLOR_DIM);
    console_print(STR_HELP_ALIASES);
    for (size_t a = 0; a < MAX_ALIASES && cmd->aliases[a] != NULL; a++) {
        if (a > 0) {
            console_print(", ");
        }
        console_print(cmd->aliases[a]);
    }
    console_print("\n");
}

static void print_unknown(const char *prefix, const char *word) {
    console_set_color(COLOR_WARNING);
    console_print(prefix);
    console_print(word);
    console_print(STR_UNKNOWN_COMMAND_2 "\n");
}

static void command_help(const char *args) {
    // "help <command>": details about one command.
    if (args[0] != '\0') {
        const struct command *cmd = find_command(args);
        if (cmd == NULL) {
            print_unknown(STR_HELP_NOT_FOUND, args);
            return;
        }
        console_set_color(COLOR_GREEN);
        console_print(cmd->name);
        console_print("\n");
        console_set_color(COLOR_BLUE);
        console_print(cmd->details);
        console_print("\n");
        print_aliases(cmd);
        return;
    }

    // Plain "help": the full list.
    console_set_color(COLOR_DIM);
    console_print(STR_HELP_HEADER "\n\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        console_set_color(COLOR_GREEN);
        console_print("  ");
        console_print(commands[i].name);
        for (size_t pad = text_length(commands[i].name); pad < HELP_NAME_COLUMN; pad++) {
            console_putc(' ');
        }
        console_set_color(COLOR_BLUE);
        console_print(commands[i].summary);
        console_print("\n");
    }
}

// ---- Reading and running a line ----

// Read one line of typing into buffer. Handles Backspace. Returns on Enter.
static void read_line(char *buffer) {
    size_t length = 0;
    for (;;) {
        char c = keyboard_get_char();
        if (c == KEY_ENTER) {
            console_putc('\n');
            buffer[length] = '\0';
            return;
        }
        if (c == KEY_BACKSPACE) {
            if (length > 0) {
                length--;
                console_putc('\b');
            }
            continue;
        }
        if (c == '\t') {
            continue;  // no tab-completion yet
        }
        if (length < LINE_MAX_LENGTH - 1) {
            buffer[length++] = c;
            console_putc(c);
        }
    }
}

static void run_line(char *line) {
    // Remove spaces at the end.
    size_t end = text_length(line);
    while (end > 0 && line[end - 1] == ' ') {
        line[--end] = '\0';
    }

    // Split into the command word and everything after it (the args).
    char *word = (char *)text_skip_spaces(line);
    if (*word == '\0') {
        return;  // empty line: do nothing
    }
    char *args = word;
    while (*args != '\0' && *args != ' ') {
        args++;
    }
    if (*args == ' ') {
        *args = '\0';  // end the command word here
        args = (char *)text_skip_spaces(args + 1);
    }

    const struct command *cmd = find_command(word);
    if (cmd == NULL) {
        print_unknown(STR_UNKNOWN_COMMAND_1, word);
        return;
    }
    console_set_color(COLOR_BLUE);
    cmd->run(args);
}

void shell_run(void) {
    char line[LINE_MAX_LENGTH];
    for (;;) {
        console_set_color(COLOR_GREEN);
        console_print(STR_PROMPT);
        console_set_color(COLOR_BLUE);
        read_line(line);
        run_line(line);
    }
}
