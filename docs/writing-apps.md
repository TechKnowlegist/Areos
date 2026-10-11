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

Apps have no C library yet, so things like `printf` and `malloc` aren't available. Use the functions above instead.

Everything an app prints also goes to the terminal, so apps are screen-reader friendly automatically.
