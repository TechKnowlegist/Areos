// host_test.c - try the speech engine on Linux, without AREOS.
// Builds as a bare program (no C library) that writes a .wav file, so the
// engine and its small C library are tested exactly as the kernel uses them.
//   usage: host_test out.wav "text to say" [speed%]
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../engine.h"
#include "../libc/libc.h"

static long sys(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}
static long len(const char *s) { long n = 0; while (s[n]) n++; return n; }
void *memcpy(void *d, const void *s, size_t n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int v, size_t n) { char *a = d; while (n--) *a++ = (char)v; return d; }
void *memmove(void *d, const void *s, size_t n) { char *a = d; const char *b = s; if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; } return d; }
int memcmp(const void *x, const void *y, size_t n) { const unsigned char *a = x, *b = y; for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return a[i] - b[i]; return 0; }
void speech_libc_debug_write(const char *text, size_t length) { sys(1, 2, (long)text, (long)length); }

static unsigned char arena[96u << 20] __attribute__((aligned(16)));
static short samples[16000 * 120];
static int count;

static bool got(const short *s, int n, void *user) {
    (void)user;
    for (int i = 0; i < n && count < (int)(sizeof(samples) / 2); i++) samples[count++] = s[i];
    return true;
}
static void put32(unsigned char *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

int main_c(long argc, char **argv) {
    if (argc < 3) return 2;
    int speed = 100;
    if (argc > 3) { speed = 0; for (char *p = argv[3]; *p; p++) speed = speed * 10 + (*p - '0'); }
    if (!speech_engine_init(arena, sizeof(arena))) { speech_libc_debug_write("init failed\n", 12); return 1; }
    if (!speech_engine_say(argv[2], speed, got, NULL)) { speech_libc_debug_write("say failed\n", 11); return 1; }
    unsigned char h[44] = "RIFF....WAVEfmt ";
    put32(h + 4, 36 + count * 2); put32(h + 16, 16); h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
    put32(h + 24, 16000); put32(h + 28, 32000); h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a'; put32(h + 40, count * 2);
    long fd = sys(2, (long)argv[1], 01 | 0100 | 01000, 0644);
    sys(1, fd, (long)h, 44); sys(1, fd, (long)samples, count * 2); sys(3, fd, 0, 0);
    return 0;
}
__asm__(".global _start\n_start:\n mov (%rsp), %rdi\n lea 8(%rsp), %rsi\n and $-16, %rsp\n call main_c\n mov %eax, %edi\n mov $60, %eax\n syscall\n");
