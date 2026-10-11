// speech.c - the built-in screen reader.
//
// Text arrives here from the console a character at a time and is cut
// into short pieces (a sentence or a phrase), which wait in a queue. The
// speech thread takes them one at a time, has the speech engine (Flite,
// in the speech/ folder) turn each into sound, and hands the sound to the
// sound card. Pressing a key empties the queue and stops the sound, so
// the newest thing on screen is always what you hear.

#include "speech.h"
#include "sound.h"
#include "thread.h"
#include "settings.h"
#include "timer.h"
#include "pmm.h"
#include "cpu.h"
#include "console.h"
#include "lang_en.h"

// The speech engine (speech/engine.h). It's built separately, with
// floating point turned on, and only ever runs on the speech thread.
typedef bool (*speech_sound_fn)(const short *samples, int count, void *user);
bool speech_engine_init(void *memory, size_t size);
bool speech_engine_say(const char *text, int speed_percent, speech_sound_fn sound, void *user);
void speech_engine_set_voice(int index);

// Flite's own messages (it rarely has any) are dropped.
void speech_libc_debug_write(const char *text, size_t length);
void speech_libc_debug_write(const char *text, size_t length) {
    (void)text;
    (void)length;
}

// Flite works in its own memory, so it can never run the kernel out.
#define ENGINE_MEMORY (96ull * 1024 * 1024)

#define PIECE_MAX   240     // longest piece of text spoken in one go
#define QUEUE_SIZE  48      // pieces waiting
#define LINE_MAX    1024
#define PAUSE_MS    150     // an unfinished line is spoken after this long

// ---- State ----

static bool started = false;                  // sound card + thread running
static volatile bool engine_tried = false;    // the speech thread finished setting up
static volatile bool engine_ok = false;
static bool enabled = true;
static int speed_percent = 110;
static volatile int wanted_voice = 0;

// Pieces waiting to be said. Main adds them, the speech thread takes them.
static char queue[QUEUE_SIZE][PIECE_MAX + 1];
static volatile uint32_t queue_head = 0, queue_tail = 0;

// Bumped by speech_interrupt, so a piece being spoken knows to stop.
static volatile uint32_t generation = 0;
static volatile bool speaking = false;          // the engine is working on a piece
static volatile bool waiting_for_room = false;  // ...and the sound queue is full
static uint32_t speaking_generation;

// The line being written right now.
static char line[LINE_MAX];
static int line_length = 0;
static int line_spoken = 0;          // how much of it has been queued already
static volatile uint64_t last_char_ms = 0;

// ---- The queue ----

static bool queue_empty(void) {
    return queue_head == queue_tail;
}

static void queue_push(const char *text, int length) {
    uint32_t next = (queue_tail + 1) % QUEUE_SIZE;
    if (next == queue_head) {
        return;   // full: AREOS is writing faster than anyone could listen
    }
    if (length > PIECE_MAX) length = PIECE_MAX;
    for (int i = 0; i < length; i++) queue[queue_tail][i] = text[i];
    queue[queue_tail][length] = '\0';
    queue_tail = next;
}

static bool queue_pop(char *out, uint32_t *which_generation) {
    uint64_t flags = cpu_interrupts_save();
    bool got = !queue_empty();
    if (got) {
        for (int i = 0; i <= PIECE_MAX; i++) {
            out[i] = queue[queue_head][i];
            if (out[i] == '\0') break;
        }
        out[PIECE_MAX] = '\0';
        queue_head = (queue_head + 1) % QUEUE_SIZE;
        *which_generation = generation;
    }
    cpu_interrupts_restore(flags);
    return got;
}

// ---- Turning screen text into something good to say ----

static bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// Names for single characters, so typing "." says "dot" rather than nothing.
static const char *character_name(char c) {
    switch (c) {
    case ' ': return "space";
    case '.': return "dot";
    case ',': return "comma";
    case '!': return "bang";
    case '?': return "question";
    case ':': return "colon";
    case ';': return "semicolon";
    case '-': return "dash";
    case '_': return "underline";
    case '+': return "plus";
    case '=': return "equals";
    case '*': return "star";
    case '/': return "slash";
    case '\\': return "backslash";
    case '\'': return "tick";
    case '"': return "quote";
    case '(': return "left paren";
    case ')': return "right paren";
    case '[': return "left bracket";
    case ']': return "right bracket";
    case '{': return "left brace";
    case '}': return "right brace";
    case '<': return "less";
    case '>': return "greater";
    case '@': return "at";
    case '#': return "number";
    case '$': return "dollar";
    case '%': return "percent";
    case '^': return "caret";
    case '&': return "and";
    case '|': return "bar";
    case '~': return "tilde";
    case '`': return "grave";
    default: return NULL;
    }
}

// Copy text into a piece, tidied up for the voice.
static int tidy(const char *text, int length, char *out) {
    int n = 0;
    for (int i = 0; i < length && n < PIECE_MAX - 6; i++) {
        char c = text[i];
        // "AREOS" (or "areos") is said "Arios".
        if (i + 5 <= length && lower(c) == 'a' && lower(text[i + 1]) == 'r'
            && lower(text[i + 2]) == 'e' && lower(text[i + 3]) == 'o' && lower(text[i + 4]) == 's'
            && (i == 0 || !is_letter(text[i - 1])) && (i + 5 == length || !is_letter(text[i + 5]))) {
            const char *say = "Arios";
            for (int k = 0; say[k]; k++) out[n++] = say[k];
            i += 4;
            continue;
        }
        // "Ctrl" is said "Control".
        if (c == 'C' && i + 4 <= length && text[i + 1] == 't' && text[i + 2] == 'r' && text[i + 3] == 'l'
            && (i + 4 == length || !is_letter(text[i + 4]))) {
            const char *say = "Control";
            for (int k = 0; say[k]; k++) out[n++] = say[k];
            i += 3;
            continue;
        }
        // Symbols that are just decoration on screen.
        if (c == '>' || c == '<' || c == '_' || c == '|' || c == '*' || c == '#'
            || c == '~' || c == '`' || c == '^' || c == '=' ) {
            c = ' ';
        }
        if (c < 32 || c > 126) c = ' ';
        if (c == ' ' && (n == 0 || out[n - 1] == ' ')) continue;
        out[n++] = c;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    return n;
}

// Is there anything here worth saying out loud?
static bool has_words(const char *text, int length) {
    for (int i = 0; i < length; i++) {
        char c = text[i];
        if (is_letter(c) || (c >= '0' && c <= '9')) return true;
    }
    return false;
}

static bool starts_with(const char *text, int length, const char *prefix) {
    int i = 0;
    for (; prefix[i]; i++) {
        if (i >= length || text[i] != prefix[i]) return false;
    }
    return true;
}

// Where to end the piece starting at `start`. Pieces end at the end of a
// sentence, or at a comma once they're getting long, or (if there's no
// better place) at a space. Short pieces start sounding sooner.
static int cut_point(const char *text, int start, int length) {
    int last_comma = -1, last_space = -1;
    for (int i = start; i < length; i++) {
        int so_far = i - start;
        char c = text[i];
        bool spaced = i + 1 < length && text[i + 1] == ' ';
        if ((c == '.' || c == '!' || c == '?' || c == ';' || c == ':') && spaced && so_far >= 8) {
            return i + 1;
        }
        if (c == ',' && spaced) {
            if (so_far >= 50) return i + 1;
            last_comma = i + 1;
        }
        if (c == ' ') last_space = i;
        if (so_far >= 110) {
            if (last_comma > start + 20) return last_comma;
            if (last_space > start + 20) return last_space;
            return i;
        }
    }
    return length;
}

// Queue some text, cut into pieces at the ends of sentences and phrases.
// Short pieces start sounding sooner and need less memory.
static void queue_text(const char *text, int length) {
    if (!enabled || !started) return;
    while (length > 0 && text[0] == ' ') { text++; length--; }
    while (length > 0 && text[length - 1] == ' ') length--;
    if (length == 0) return;

    // Notes for terminal users about the screen aren't worth hearing.
    if (starts_with(text, length, "(Graphics mode")) return;

    if (length == 1 && !is_letter(text[0]) && !(text[0] >= '0' && text[0] <= '9')) {
        const char *name = character_name(text[0]);
        if (name != NULL) {
            int n = 0;
            while (name[n]) n++;
            queue_push(name, n);
        }
        return;
    }
    if (!has_words(text, length)) return;

    char piece[PIECE_MAX + 1];
    int start = 0;
    while (start < length) {
        int cut = cut_point(text, start, length);
        int n = tidy(text + start, cut - start, piece);
        if (n > 0 && has_words(piece, n)) queue_push(piece, n);
        start = cut;
    }
}

// ---- What the console sends us ----

void speech_feed_char(char c) {
    if (!started) return;
    uint64_t flags = cpu_interrupts_save();
    if (c == '\n') {
        queue_text(line + line_spoken, line_length - line_spoken);
        line_length = line_spoken = 0;
    } else if (c == '\b') {
        if (line_length > line_spoken) line_length--;
        else if (line_length > 0) { line_length--; line_spoken = line_length; }
    } else if (c == '\t') {
        if (line_length < LINE_MAX) line[line_length++] = ' ';
    } else if (c >= 32 && c <= 126) {
        if (line_length >= LINE_MAX) {
            queue_text(line + line_spoken, line_length - line_spoken);
            line_length = line_spoken = 0;
        }
        line[line_length++] = c;
    }
    last_char_ms = timer_ms();
    cpu_interrupts_restore(flags);
}

void speech_say(const char *text) {
    if (!started) return;
    int length = 0;
    while (text[length]) length++;
    uint64_t flags = cpu_interrupts_save();
    queue_text(text, length);
    cpu_interrupts_restore(flags);
}

void speech_interrupt(void) {
    if (!started) return;
    uint64_t flags = cpu_interrupts_save();
    generation++;
    queue_head = queue_tail;
    line_spoken = line_length;     // don't come back to half a line later
    sound_flush();
    cpu_interrupts_restore(flags);
}

bool speech_enabled(void) {
    return enabled;
}

void speech_settings_changed(void) {
    uint64_t value = 0;
    settings_get(SETTING_SPEECH_OFF, &value);
    bool now_enabled = value == 0;
    if (!now_enabled && enabled) speech_interrupt();
    enabled = now_enabled;
    value = 0;
    settings_get(SETTING_SPEECH_SPEED, &value);
    // 0 means "never set": the normal speed. Otherwise it's a percentage.
    speed_percent = value == 0 ? 110 : (int)value;
    if (speed_percent < 50) speed_percent = 50;
    if (speed_percent > 300) speed_percent = 300;
    value = 0;
    settings_get(SETTING_SPEECH_VOICE, &value);
    wanted_voice = (int)value;
    value = 0;
    settings_get(SETTING_VOLUME, &value);
    sound_set_volume(value == 0 ? 100 : (int)value - 1);
}

void speech_toggle(void) {
    if (!started) return;
    if (enabled) {
        speech_interrupt();
        speech_say(STR_SPEECH_OFF);   // said just before going quiet
        enabled = false;
        settings_set(SETTING_SPEECH_OFF, 1);
    } else {
        enabled = true;
        settings_set(SETTING_SPEECH_OFF, 0);
        speech_interrupt();
        speech_say(STR_SPEECH_ON);
    }
}

void speech_tick(void) {
    if (!started) return;
    sound_tick();
    // A line that's stopped growing (a prompt, a letter just typed): say it.
    if (line_length > line_spoken && timer_ms() - last_char_ms >= PAUSE_MS) {
        queue_text(line + line_spoken, line_length - line_spoken);
        line_spoken = line_length;
    }
}

// ---- The speech thread ----

static bool got_sound(const short *samples, int count, void *user) {
    (void)user;
    while (count > 0) {
        if (generation != speaking_generation) {
            return false;   // interrupted: stop this piece
        }
        uint32_t room = sound_room();
        if (room < 256) {
            waiting_for_room = true;
            asm volatile ("hlt");   // the sound card needs time to catch up
            continue;
        }
        waiting_for_room = false;
        uint32_t n = (uint32_t)count < room ? (uint32_t)count : room;
        sound_queue((const int16_t *)samples, n);
        samples += n;
        count -= (int)n;
    }
    return generation == speaking_generation;
}

// Does the speech thread have something to do right now? Asked by the
// timer every tick, to decide who gets the CPU.
static bool speech_runnable(void) {
    if (!engine_tried) return true;            // still setting up
    if (speaking) {
        if (generation != speaking_generation) return true;   // let it stop quickly
        return !waiting_for_room || sound_room() >= 4096;
    }
    return !queue_empty();
}

static void speech_thread(void) {
    void *memory = pmm_alloc_pages(ENGINE_MEMORY / PAGE_SIZE);
    engine_ok = memory != NULL && speech_engine_init(memory, ENGINE_MEMORY);
    engine_tried = true;

    char piece[PIECE_MAX + 1];
    for (;;) {
        uint32_t which;
        if (!engine_ok || !queue_pop(piece, &which)) {
            asm volatile ("hlt");   // nothing to say: rest
            continue;
        }
        speaking_generation = which;
        waiting_for_room = false;
        speaking = true;
        speech_engine_set_voice(wanted_voice);
        speech_engine_say(piece, speed_percent, got_sound, NULL);
        speaking = false;
    }
}

void speech_init(void) {
    if (!sound_init()) {
        console_print(STR_SPEECH_NO_SOUND "\n");
        return;
    }
    speech_settings_changed();
    started = true;
    thread_start_speech(speech_thread, speech_runnable);
    // Wait (up to 3 seconds) for the voice to be ready, so we can say if it isn't.
    uint64_t give_up = timer_ms() + 3000;
    while (!engine_tried && timer_ms() < give_up) {
        thread_idle(timer_ms() + 10);
    }
    if (!engine_ok) {
        started = false;
        console_print(STR_SPEECH_FAILED "\n");
        return;
    }
    console_print(enabled ? STR_SPEECH_READY "\n" : STR_SPEECH_READY_OFF "\n");
}
