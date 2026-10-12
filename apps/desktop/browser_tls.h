// browser_tls.h - secure (https) connections for the browser. See browser_tls.c.

#ifndef BROWSER_TLS_H
#define BROWSER_TLS_H

#include "desktop.h"

enum tls_status { TLS_WORKING, TLS_DONE, TLS_FAILED };

// Begin a secure conversation over an already-open TCP connection to `host`.
void tls_start(int tcp_connection, const char *host);

// Do whatever can be done right now. The first time the connection is ready,
// `request` (an ordinary HTTP request) is sent; the answer is appended to
// `out`. TLS_DONE means the server finished; TLS_FAILED means tls_error()
// says why in words.
enum tls_status tls_step(const char *request, int request_length, char *out, int *out_length, int out_max);
const char *tls_error(void);
// Unpredictable bytes (also used for page scripts).
void tls_random(void *buffer, int length);

#endif
