// syscall.h - system calls: the things a program can ask AREOS to do.
//
// A program can't touch hardware or kernel memory itself. Instead it
// puts a request number in register rax (and details in rdi, rsi, rdx)
// and runs the instruction `int 0x80`. AREOS does the job and puts the
// answer back in rax.
//
// This list is the AREOS "API". The same numbers are in apps/include/areos.h,
// which is what programs use. KEEP THE TWO IN SYNC.

#ifndef AREOS_SYSCALL_H
#define AREOS_SYSCALL_H

#define SYSCALL_VECTOR       0x80

#define SYS_EXIT             0   // exit(code)                 - end the program
#define SYS_WRITE            1   // write(text, length)        - print text
#define SYS_READ_KEY         2   // read_key()                 - wait for a key, return it
#define SYS_SLEEP_MS         3   // sleep_ms(ms)               - pause
#define SYS_TIME_MS          4   // time_ms()                  - ms since AREOS started
#define SYS_MORE_MEMORY      5   // more_memory(bytes)         - get zeroed memory, returns address or 0
#define SYS_SET_COLOR        6   // set_color(0xRRGGBB)        - change the text color
#define SYS_COUNT            7

struct interrupt_frame;
void syscall_dispatch(struct interrupt_frame *frame);

#endif
