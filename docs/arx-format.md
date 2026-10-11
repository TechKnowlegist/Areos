# The .arx program format

Every AREOS program is a `.arx` file. AREOS looks at the first bytes of the file to tell which of two kinds it is.

## Kind 1: ELF inside

If the file starts with the four bytes `7F 45 4C 46` (`\x7F` then `ELF`), it is a standard 64-bit x86 ELF executable, the same format Linux uses. Normal compilers produce it directly, so this is the easiest way to build an AREOS program, and the way bigger ports (like Doom) are built.

Requirements:

- 64-bit, little-endian, x86-64, type "executable" (not a shared library)
- Statically linked: no shared libraries, no dynamic loader
- All loadable segments below address `0x0000600000000000`
- Uses AREOS system calls (see `apps/include/areos.h`), not Linux ones

## Kind 2: native AREOS

If the file starts with the eight bytes `AREOSARX`, it is a native AREOS program: a 64-byte header followed by the program image. All numbers are little-endian.

| Offset | Size | Field          | Meaning                                                |
|-------:|-----:|----------------|--------------------------------------------------------|
| 0      | 8    | `magic`        | The text `AREOSARX`                                    |
| 8      | 4    | `version`      | Format version, currently `1`                          |
| 12     | 4    | `header_size`  | Size of this header; the image starts right after it (`64`) |
| 16     | 8    | `load_address` | Where in memory the image is placed                    |
| 24     | 8    | `entry`        | Address where the program starts running               |
| 32     | 8    | `image_size`   | Number of image bytes stored in the file               |
| 40     | 8    | `memory_size`  | Bytes the program occupies in memory; anything past `image_size` starts as zeros |
| 48     | 16   | `reserved`     | Zeros, kept for future use                             |

AREOS maps `memory_size` bytes at `load_address`, copies the image in, and jumps to `entry`.

Rules: `image_size <= memory_size`, the program must fit below `0x0000600000000000`, and `entry` must fall inside the loaded memory.

You don't write this by hand: `tools/arxpack` converts a normal ELF build into this format.

## What a running program gets

- Its own private memory. It can't see the kernel or other programs.
- A 256 KB stack, ready when it starts.
- More memory on request through the `more_memory` system call.
- If it does something not allowed (like touching kernel memory), only the program is stopped. AREOS reports what happened and carries on.
