/* app.ld - how to lay out an AREOS program in memory.
 *
 * Every program starts at address 0x400000 in its own private memory
 * (each program has its own, so they can all use the same address).
 */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.*) *(COMMON) }

    /* Things a hosted program would need but AREOS programs don't. */
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
