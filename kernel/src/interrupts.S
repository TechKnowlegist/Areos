# interrupts.S - the first code that runs when an interrupt happens.
#
# An "interrupt" is the CPU dropping what it's doing to handle something:
# a key press, a timer tick, or an error like reading a bad address.
# There are 256 possible interrupts, numbered 0-255 ("vectors").
#
# Each one gets a tiny "stub" here. The stub saves every register (so the
# interrupted code can carry on afterwards as if nothing happened), calls
# interrupt_dispatch() in idt.c, then restores everything and returns.
#
# Some CPU errors push an extra "error code" and some don't. To keep one
# layout for all of them, stubs for the ones that don't push a fake 0.

.section .text

# A stub for an interrupt WITHOUT a CPU error code.
.macro STUB_NO_ERROR vector
interrupt_stub_\vector:
    pushq $0            # fake error code
    pushq $\vector
    jmp interrupt_common
.endm

# A stub for an interrupt WITH a CPU error code (already pushed).
.macro STUB_ERROR vector
interrupt_stub_\vector:
    pushq $\vector
    jmp interrupt_common
.endm

interrupt_common:
    # Save all general-purpose registers. Order must match struct interrupt_frame.
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    cld                     # C code expects string operations to go forwards
    movq %rsp, %rdi         # argument 1: pointer to the saved registers
    call interrupt_dispatch
    movq %rax, %rsp         # carry on with the frame it hands back: usually the
                            # same one, or another thread's (see thread.c)

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp          # throw away the vector number and error code
    iretq                   # return to whatever was interrupted

# CPU exceptions 0-31. These push an error code: 8, 10-14, 17, 21, 29, 30.
STUB_NO_ERROR 0
STUB_NO_ERROR 1
STUB_NO_ERROR 2
STUB_NO_ERROR 3
STUB_NO_ERROR 4
STUB_NO_ERROR 5
STUB_NO_ERROR 6
STUB_NO_ERROR 7
STUB_ERROR    8
STUB_NO_ERROR 9
STUB_ERROR    10
STUB_ERROR    11
STUB_ERROR    12
STUB_ERROR    13
STUB_ERROR    14
STUB_NO_ERROR 15
STUB_NO_ERROR 16
STUB_ERROR    17
STUB_NO_ERROR 18
STUB_NO_ERROR 19
STUB_NO_ERROR 20
STUB_ERROR    21
STUB_NO_ERROR 22
STUB_NO_ERROR 23
STUB_NO_ERROR 24
STUB_NO_ERROR 25
STUB_NO_ERROR 26
STUB_NO_ERROR 27
STUB_NO_ERROR 28
STUB_ERROR    29
STUB_ERROR    30
STUB_NO_ERROR 31

# Everything else (32-255): hardware like the timer and keyboard, and spares.
.altmacro
.set i, 32
.rept 224
    STUB_NO_ERROR %i
    .set i, i + 1
.endr

# A table of all 256 stub addresses, so idt.c can fill in the IDT.
.macro STUB_ADDRESS vector
    .quad interrupt_stub_\vector
.endm

.section .data
.global interrupt_stub_table
interrupt_stub_table:
.set i, 0
.rept 256
    STUB_ADDRESS %i
    .set i, i + 1
.endr

.section .note.GNU-stack, "", @progbits
