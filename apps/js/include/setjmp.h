#ifndef AREOS_JS_SETJMP_H
#define AREOS_JS_SETJMP_H
typedef unsigned long jmp_buf[8];   // rbx rbp r12 r13 r14 r15 rsp rip
int setjmp(jmp_buf env) __attribute__((returns_twice));
_Noreturn void longjmp(jmp_buf env, int value);
#endif
