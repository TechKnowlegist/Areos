# usermode.S - jumping into a program (ring 3), and coming back.
#
# usermode_enter() saves the kernel's place, then "returns" from a fake
# interrupt into the program, which is how x86 drops to ring 3.
# usermode_return() is used when the program exits (or crashes): it
# jumps back to the saved place, so usermode_enter() finally returns.

.section .text

# uint64_t usermode_enter(uint64_t entry, uint64_t user_stack, uint64_t *saved_rsp)
#   rdi = where the program starts
#   rsi = top of the program's stack
#   rdx = where to remember the kernel's stack pointer
# Returns the program's exit code (handed over by usermode_return).
.global usermode_enter
usermode_enter:
    # Save the registers C code expects us to keep.
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdx)

    # Build the frame iretq expects: ss, rsp, rflags, cs, rip.
    pushq $(0x18 | 3)       # user data segment
    pushq %rsi              # user stack
    pushq $0x202            # flags: interrupts on
    pushq $(0x20 | 3)       # user code segment
    pushq %rdi              # program start

    # Don't leak kernel values to the program.
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    xorl %r12d, %r12d
    xorl %r13d, %r13d
    xorl %r14d, %r14d
    xorl %r15d, %r15d
    iretq

# void usermode_return(uint64_t saved_rsp, uint64_t exit_code)
.global usermode_return
usermode_return:
    movq %rdi, %rsp
    movq %rsi, %rax
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.section .note.GNU-stack, "", @progbits
