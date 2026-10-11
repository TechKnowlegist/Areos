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
- [ ] Built-in screen reader (sound driver + speech engine). Focus and mouse hover are already described in text.

## Apps

AREOS runs `.arx` programs, each walled off in its own memory so a crashing app can't take AREOS down. Type `apps` to list them and type an app's name to run it. Try `hello`, `countdown`, `starfield` (fly through space, arrow keys change speed), and `oops` (which crashes on purpose).

- [Writing an AREOS app](docs/writing-apps.md)
- [The .arx format](docs/arx-format.md)

## Desktop

AREOS starts with a few setup screens, then opens the desktop: a space wallpaper with your apps as icons, a taskbar along the bottom with the Start button and a clock, and windows you can move, minimize and close. Pressing Enter through setup keeps the defaults (dark mode, English, detailed descriptions).

Built in: **Calculator**, **Notes**, **Settings** (dark or light mode, three wallpapers: Nebula, Red planet, Plain) and **About AREOS**. Apps like Starfield open full screen, and the desktop comes back when they finish.

The desktop has areas: the desktop icons, the taskbar, the Start menu and the front window.

- **F3** or the **Windows key**: open or close the Start menu.
- **F6**: move to the next area. **Shift+F6**: the one before.
- **Tab** or **arrow keys**: move. **Shift+Tab**: move back. **Home**/**End**: first/last item.
- **Enter** or **Space**: open or choose.
- **Escape**: close the Start menu, or the front window.
- **F1**: hear the keys. **F2**: hear where you are.
- **Letters**: jump to the next item starting with that letter.
- The **mouse** works too: double-click desktop icons, click everything else, drag windows by their title bar, click a taskbar button to bring a window back.

To skip the desktop, pick **AREOS (text only)** in the boot menu. At the command line, `desktop` opens it again and `setup` reruns the setup screens.

## Accessibility

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

The built-in console font is a bitmap rendering of [DejaVu Sans Mono](https://dejavu-fonts.github.io/) Bold.

## License

See [LICENSE](LICENSE).
