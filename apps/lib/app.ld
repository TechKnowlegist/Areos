/* app.ld - how to lay out an AREOS program in memory.
 *
 * Every program starts at address 0x400000 in its own private memory
 * (each program has its own, so they can all use the same address).
 * Code, read-only data and changeable data each get their own block.
 */

ENTRY(_start)

PHDRS
{
    code    PT_LOAD FLAGS(5);  /* read + run */
    rodata  PT_LOAD FLAGS(4);  /* read only */
    data    PT_LOAD FLAGS(6);  /* read + write */
}

SECTIONS
{
    . = 0x400000;

    .text : { KEEP(*(.text._start)) *(.text .text.*) } :code

    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) } :rodata

    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss  : { *(.bss .bss.*) *(COMMON) } :data

    /* Things a hosted program would need but AREOS programs don't. */
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
