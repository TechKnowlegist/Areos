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
- [ ] Program loader
- [ ] Graphics mode
- [ ] Doom

## Building

You need a Linux environment (WSL works) with: `build-essential nasm xorriso curl git make qemu-system-x86 qemu-system-gui`

Then run:

    make run-bios

## Credits

Started from the [Limine C Template](https://github.com/limine-bootloader/limine-c-template). Boots with the [Limine](https://github.com/limine-bootloader/limine) bootloader.

The built-in console font is a bitmap rendering of [DejaVu Sans Mono](https://dejavu-fonts.github.io/) Bold.

## License

See [LICENSE](LICENSE).
