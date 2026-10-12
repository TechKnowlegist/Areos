// jsplatform.h - what the JavaScript host (jshost.c) needs from the program
// it is part of: the clock, the network and a way to say things. The AREOS
// browser provides these (browser_net.c); a test program on a normal
// computer provides its own.

#ifndef JSPLATFORM_H
#define JSPLATFORM_H

#include <stdint.h>

struct jsp_response {
    int status;                 // 200, 404...
    char status_text[64];
    char final_url[2048];       // after redirects
    char *headers;              // "Name: value\r\n" lines (malloc'd), or NULL
    int headers_len;
    char *body;                 // malloc'd, or NULL
    int body_len;
    char error[160];            // when jsp_http returns non-zero
};

// Fetch a web address and wait for the answer (following redirects).
// `headers` is "Name: value\r\n..." (may be NULL). Returns 0 on success.
int jsp_http(const char *method, const char *url, const char *headers,
             const char *body, int body_len, int timeout_ms, struct jsp_response *out);
uint64_t jsp_time_ms(void);
void jsp_log(int level, const char *text);      // 0 log, 1 info, 2 warn, 3 error
void jsp_announce(const char *text);            // say something to the user
void jsp_random(void *buffer, int length);
int jsp_width(void);
int jsp_height(void);
int jsp_theme(void);                            // 0 dark, 1 light

#endif
