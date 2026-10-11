// setjmp.S - save and jump back to a place in the program.
// Flite uses these to give up on a sentence it can't say, without crashing.
//
// jmp_buf layout: rbx, rbp, r12, r13, r14, r15, rsp, rip

.text
.global setjmp
.type setjmp, @function
setjmp:
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)
    leaq 8(%rsp), %rdx      // the stack as it will be after we return
    movq %rdx, 48(%rdi)
    movq (%rsp), %rdx       // where we return to
    movq %rdx, 56(%rdi)
    xorl %eax, %eax
    ret

.global longjmp
.type longjmp, @function
longjmp:
    movl %esi, %eax
    testl %eax, %eax
    jnz 1f
    movl $1, %eax           // setjmp must never appear to return 0 here
1:
    movq 0(%rdi), %rbx
    movq 8(%rdi), %rbp
    movq 16(%rdi), %r12
    movq 24(%rdi), %r13
    movq 32(%rdi), %r14
    movq 40(%rdi), %r15
    movq 48(%rdi), %rsp
    jmp *56(%rdi)

.section .note.GNU-stack,"",@progbits
