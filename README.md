# AREOS

**It can run Doom. That's it.**

*Pronounced "Arios."*

> ⚠️ **WARNING: AREOS is in heavy development.**
>
> Things are broken. Apps may not work. Honestly, there are probably no apps that work perfectly yet.
>
> **Do NOT install AREOS on a real machine you care about.** Run it in an emulator like QEMU. You could lose data.
>
> If you ignore this and it eats your computer... you can't be mad at me. I told you.

## What is AREOS?

AREOS is a hobby operating system built from scratch. It is not a Linux distro, not Windows, and not macOS. It's its own thing. ("Areo-" is the Greek prefix for Mars.)

The goals:
- Minimal: no ads, no bloat
- Private: no telemetry, nothing phones home
- Readable: open source, clear code
- It can run Doom

## Progress

- [x] Boots in QEMU
- [x] Print text
- [x] Keyboard input
- [x] Shell
- [x] Memory management
- [x] Timer + interrupts
- [x] Program loader (`.arx` apps, in ELF or native AREOS format)
- [x] Graphics mode (apps draw pictures and read raw key presses)
- [ ] Doom

Later:
- [x] Setup screens (welcome, name, language, dark/light, accessibility)
- [x] Desktop and GUI, first version (app tiles, windows, mouse, clock)
- [x] Real desktop: space wallpapers, taskbar, Start menu, movable windows, animations, Calculator and Notes
- [ ] Saving settings to a disk (right now they last until AREOS turns off)
- [x] Built-in screen reader: AREOS reads everything out loud (sound card driver + the Flite voice)

## Apps

AREOS runs `.arx` programs, each walled off in its own memory so a crashing app can't take AREOS down. Type `apps` to list them and type an app's name to run it. Try `hello`, `countdown`, `starfield` (fly through space, arrow keys change speed), and `oops` (which crashes on purpose).

- [Writing an AREOS app](docs/writing-apps.md)
- [The .arx format](docs/arx-format.md)

## Desktop

AREOS starts with a few setup screens, then opens the desktop: a space wallpaper with your apps as icons, a taskbar along the bottom with the Start button and a clock, and windows you can move, minimize and close. Pressing Enter through setup keeps the defaults (dark mode, English, detailed descriptions).

Built in: **Calculator**, **Notes**, **Settings** (dark or light mode, three wallpapers: Nebula, Red planet, Plain) and **About AREOS**. Apps like Starfield open full screen, and the desktop comes back when they finish.

The desktop has areas: the desktop icons, the taskbar, the Start menu and the front window.

- **F3**, **Alt+Space** or the **Windows key**: open or close the Start menu.
- **F6**: move to the next area. **Shift+F6**: the one before.
- **Tab** or **arrow keys**: move. **Shift+Tab**: move back. **Home**/**End**: first/last item.
- **Enter** or **Space**: open or choose.
- **Escape**: close the Start menu, or the front window.
- **F1**: hear the keys. **F2**: hear where you are.
- **Letters**: jump to the next item starting with that letter.
- The **mouse** works too, and in QEMU AREOS's pointer sits exactly under your real one: double-click desktop icons, click everything else, drag windows by their title bar, click a taskbar button to bring a window back.

To skip the desktop, pick **AREOS (text only)** in the boot menu. At the command line, `desktop` opens it again and `setup` reruns the setup screens.

## Accessibility

### The built-in screen reader

AREOS talks. Everything it writes is read out loud: the command line, what has focus on the desktop, and what the mouse is pointing at. Letters you type are read back too.

- **Ctrl**: stop talking. Pressing any other key also cuts it off, so the newest thing is always what you hear.
- **Ctrl+Alt+S**: turn the voice off or on.
- **Settings** on the desktop: speak out loud or quiet, and normal, fast or very fast.

The voice is [Flite](https://github.com/festvox/flite) from Carnegie Mellon University, running inside AREOS (see [speech/flite](speech/flite/README.md)). Sound comes out of an emulated AC'97 sound card; `make run-bios` adds one to QEMU and picks your sound system (PulseAudio on WSL). If you hear nothing, try `make run-bios AUDIO=sdl`.

If you already use a screen reader on your computer, you might want AREOS's own voice off (Ctrl+Alt+S) so you don't hear things twice.

### Text in the terminal

Everything AREOS shows is also sent as plain text to the terminal you ran `make run-bios` from, and you can type there too, so screen readers like NVDA can use all of AREOS:

- The command line and every app's text appear in the terminal.
- On the desktop, every focus change is described, like *"Light mode, radio button, not selected. 2 of 2."*
- Whatever the mouse points at is described, like *"Mouse: Settings"*.
- The boot menu also appears in the terminal. Press Down then Enter within 3 seconds to pick text only.

## Building

You need a Linux environment (WSL works) with: `build-essential nasm xorriso curl git make qemu-system-x86 qemu-system-gui`

Then run:

    make run-bios

AREOS opens in a QEMU window and also talks through the terminal. To quit, type `off` (or press Ctrl+C in the terminal).

## Credits

Started from the [Limine C Template](https://github.com/limine-bootloader/limine-c-template). Boots with the [Limine](https://github.com/limine-bootloader/limine) bootloader.

The voice is [Flite](https://github.com/festvox/flite) by the Language Technologies Institute at Carnegie Mellon University, with the CMU "slt" voice. Its licence is in [speech/flite/COPYING](speech/flite/COPYING).

The built-in console font is a bitmap rendering of [DejaVu Sans Mono](https://dejavu-fonts.github.io/) Bold.

## Settings, launcher and quick settings

- The start menu is the **AREOS launcher** (F3, Alt+Space or the Windows key).
- **Alt+Q** opens quick settings: volume, speech speed, mouse size, theme, accent colour and more.
- The Settings app has categories. Accessibility has the screen reader switch, a voice slider with ten voices (Sally, Rob, Alan and Kal, plus higher and deeper versions of each), a speech speed slider and mouse pointer size.
- Settings live in memory for now; saving to disk comes later.

## Third-party code

- Flite (speech): see `speech/flite/README.md`.
- BearSSL (https): MIT licence, `apps/bearssl/LICENSE.txt`.
- stb_truetype (page fonts): public domain, `apps/desktop/stb_truetype.h`.
- Liberation fonts (page text): SIL Open Font License 1.1. DejaVu Sans (symbols): Bitstream Vera / DejaVu licence. Both in `apps/desktop/fonts/`.

## Window management

- Maximize, restore and snap to the left or right half of the screen (Settings, Notes and Browser can resize; Calculator and About stay their size).
- Keyboard: **Alt+Shift+Left / Right** snap, **Alt+Shift+Up** maximizes, **Alt+Shift+Down** restores (or minimizes). From the terminal mirror: **Alt+L**, **Alt+R**, **Alt+M**.
- Mouse: the Maximize button in the title bar, double-click the title bar, or drag a window to the left edge, right edge or top of the screen. Dragging a snapped window pulls it back to its normal size.

## Browser (version 4: real page layout)

Open **Browser** from the launcher. It uses AREOS's own network stack (DHCP, DNS, TCP) and reads **http** and **https** pages (secure connections use BearSSL; see docs/https.md): words, headings, links, buttons and text boxes. **It runs JavaScript** (see docs/javascript.md), so single-page sites built with React, Vue and similar work. **Pages are drawn the way their authors designed them**: CSS layout (flexbox, grid, tables, floats), colours, fonts, background pictures, gradients, rounded corners, shadows, JPEG/PNG/GIF/BMP/WebP images and SVG graphics. **Alt+T** switches between the styled page and a plain text view. The screen reader still reads line by line, and Tab, Enter and H work on the styled page. Not yet: web fonts, video, canvas, mouse wheel scrolling, cookies and POST forms.

- Alt+D: address bar (type an address or words to search with FrogFind). Backspace or Alt+Left: back. Alt+Right: forward. F5: reload. The Reload button becomes Stop while loading.
- On a page: Down/Up read line by line, Tab/Shift+Tab move between links, text boxes and buttons, Enter follows, H jumps to headings, K to links, Space/Page Down scroll.
- **R** reads the page aloud from here (any key stops). **/** finds words, **N** finds the next match. **B** bookmarks the page, **Alt+B** shows bookmarks, **Alt+H** shows history.
- The page re-flows when the window is maximized or snapped.
- **Alt+J** shows what the page's scripts wrote to their console, including errors.
- Not yet: pictures, styles (so menus and pop-ups the page hides with CSS may show), cookies, sending forms with POST, TLS 1.3.
- In QEMU, `make run-bios` adds the network card (`-nic user,model=e1000`) and hides the Windows pointer over the AREOS screen (turn off with `HIDE_HOST_POINTER=no`).

## License

See [LICENSE](LICENSE).
