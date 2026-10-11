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
#define SYS_GRAPHICS_START   7   // graphics_start()           - returns (screen width << 32) | height
#define SYS_GRAPHICS_DRAW    8   // graphics_draw(pixels, w, h) - show a 0x00RRGGBB picture
#define SYS_GRAPHICS_STOP    9   // graphics_stop()            - back to text
#define SYS_KEY_EVENT        10  // key_event()                - 0 if none, else key | pressed << 8 | character << 16
#define SYS_MOUSE_EVENT      11  // mouse_event()              - 0 if none, else see areos.h
#define SYS_DATE_TIME        12  // date_time()                - packed date and time, see areos.h
#define SYS_SETTING_GET      13  // setting_get(id)            - a setting's value
#define SYS_SETTING_SET      14  // setting_set(id, value)
#define SYS_APP_NAME         15  // app_name(index, buffer, size) - name of app number `index`
#define SYS_LAUNCH           16  // launch(name, come_back)    - run an app after this one exits
#define SYS_POWER            17  // power(0 = off, 1 = restart)
#define SYS_SPEECH           18  // speech(0 = stop talking, 1 = say text at pointer)
#define SYS_NET_LOOKUP       19  // net_lookup(name)              - name to address (0 = not found)
#define SYS_TCP_CONNECT      20  // tcp_connect(address, port)    - handle, or error
#define SYS_TCP_SEND         21  // tcp_send(handle, data, length) - bytes accepted
#define SYS_TCP_RECEIVE      22  // tcp_receive(handle, buffer, max) - bytes, 0 = none yet, error = finished
#define SYS_TCP_CLOSE        23  // tcp_close(handle)
#define SYS_NET_ADDRESS      24  // net_address()                - our address (0 = no network)
#define SYS_COUNT            25

struct interrupt_frame;
void syscall_dispatch(struct interrupt_frame *frame);

#endif
