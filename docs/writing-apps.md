# Writing an AREOS app

1. Create `apps/yourapp.c`:

   ```c
   #include "areos.h"

   int main(void) {
       areos_print("Hello from my app!\n");
       return 0;
   }
   ```

2. Open `apps/GNUmakefile` and add `yourapp` to one of the two lists:
   - `ELF_APPS` builds a `.arx` with ELF inside
   - `NATIVE_APPS` builds a `.arx` in the native AREOS format

3. Run `make run-bios`. In AREOS, type `apps` to see it, then type `yourapp` (or `run yourapp`).

## What your app can do

Everything goes through the functions in `apps/include/areos.h`:

| Function                          | What it does                                  |
|-----------------------------------|-----------------------------------------------|
| `areos_print(text)`               | Print text                                    |
| `areos_print_number(n)`           | Print a whole number                          |
| `areos_write(text, length)`       | Print an exact number of characters           |
| `areos_read_key()`                | Wait for a key and return it                  |
| `areos_read_line(buffer, size)`   | Read a typed line, with Backspace support     |
| `areos_sleep_ms(ms)`              | Pause (1000 = one second)                     |
| `areos_time_ms()`                 | Milliseconds since AREOS started              |
| `areos_more_memory(bytes)`        | Get fresh, zeroed memory                      |
| `areos_set_color(0xRRGGBB)`       | Change the text color (`AREOS_GREEN`, `AREOS_BLUE`, ...) |
| `areos_exit(code)`                | End the program (returning from `main` does this too) |

## Graphics and games

| Function                                   | What it does                                 |
|--------------------------------------------|----------------------------------------------|
| `areos_graphics_start(&w, &h)`             | Switch to graphics mode (gives you the real screen size) |
| `areos_graphics_draw(pixels, width, height)` | Show your picture, scaled up to fill the screen |
| `areos_graphics_stop()`                    | Back to text (also happens when your app ends) |
| `areos_key_event(&event)`                  | Did a key go down or up? `event.key` and `event.pressed` |

Pixels are `uint32_t` values in `0x00RRGGBB` form, row by row. A small picture like 320 x 200 is scaled up with crisp square pixels. Keys are `AREOS_KEY_UP`, `AREOS_KEY_ESCAPE` and friends, or the lowercase letter for letter keys. See `apps/starfield.c` for a full example.

While your app shows graphics, anything it prints still goes to the terminal, so tell the user what's happening in words. That's what lets screen-reader users follow along.

## The GUI toolkit

`apps/include/gui.h` has what the desktop is built with: drawing rectangles and text onto a canvas, the dark and light themes, and accessibility helpers. Give every clickable thing a `struct gui_item` with a name and a role, and call `gui_announce_item` when it gets focus, so screen-reader users hear it. See `apps/desktop/` for a full example (start with `desktop.h`, which explains how the parts fit together). Apps made of several files go in a folder and are listed in `FOLDER_APPS` in `apps/GNUmakefile`. Also useful: `areos_mouse_event`, `areos_date_time`, `areos_setting_get`/`areos_setting_set` (the theme the user picked is `AREOS_SETTING_THEME`), and `areos_launch` to open another app.

AREOS reads out everything an app prints, so text you print is already accessible. To cut speech off (say, when the mouse moves onto something new) call `areos_speech_stop()`; to say something without printing it, `areos_speech_say("text")`.

Apps have no C library yet, so things like `printf` and `malloc` aren't available. Use the functions above instead.

Everything an app prints also goes to the terminal, so apps are screen-reader friendly automatically.
