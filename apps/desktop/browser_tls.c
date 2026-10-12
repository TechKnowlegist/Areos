// browser_tls.c - secure (https) connections for the browser.
//
// The hard cryptography is BearSSL (bearssl.org, by Thomas Pornin, MIT
// licence). This file connects it to AREOS: where the clock and the random
// numbers come from, and how the scrambled bytes travel over a TCP
// connection that the kernel's network code provides.
//
// BearSSL never waits: it just says what it wants next ("send these bytes",
// "give me bytes from the server", "here's what was decrypted"). tls_step()
// does whatever it asks for until nothing more can be done right now, so
// the browser can keep drawing while a page loads.
//
// The browser checks the website's certificate against the list of roots in
// browser_trust.inc, and that the certificate is for the website's name and
// hasn't expired.

#include "bearssl.h"
#include "browser.h"
#include "browser_tls.h"

#include "browser_trust.inc"

static br_ssl_client_context client;
static br_x509_minimal_context x509;
static unsigned char io_buffer[BR_SSL_BUFSIZE_BIDI];

static int connection = -1;
static int sent_so_far = 0;
static bool request_done = false;
static char error_text[160];

// ---- Random numbers ----
//
// TLS needs unpredictable numbers. AREOS has no hardware random source yet,
// so the seed is made from things that are hard to predict: the processor's
// cycle counter sampled many times (with the little delays between samples
// varying), the clock, and the processor's random-number instruction when it
// has one. They are all mixed through SHA-256.

static uint64_t cycle_counter(void) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static bool processor_random(uint64_t *out) {
    uint32_t eax = 1, ebx, ecx = 0, edx;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (!(ecx & (1u << 30))) return false;
    for (int attempt = 0; attempt < 10; attempt++) {
        unsigned char ok;
        uint64_t value;
        __asm__ volatile ("rdrand %0; setc %1" : "=r"(value), "=qm"(ok) : : "cc");
        if (ok) { *out = value; return true; }
    }
    return false;
}

static void make_seed(unsigned char seed[64]) {
    br_sha256_context hash;
    for (int half = 0; half < 2; half++) {
        br_sha256_init(&hash);
        for (int i = 0; i < 128; i++) {
            uint64_t sample = cycle_counter();
            br_sha256_update(&hash, &sample, sizeof(sample));
            // A short, uneven wait, so the next sample differs.
            for (volatile uint64_t spin = 0; spin < (uint64_t)(sample & 0x3F) + 8; spin++) { }
        }
        uint64_t extra[4] = { areos_time_ms(), (uint64_t)mouse_x * 4099 + (uint64_t)mouse_y, 0, (uint64_t)half };
        struct areos_date_time now = areos_date_time();
        extra[2] = ((uint64_t)now.second << 40) | ((uint64_t)now.minute << 32) | ((uint64_t)now.hour << 24)
                 | ((uint64_t)now.day << 16) | ((uint64_t)now.month << 8) | (uint64_t)now.year;
        br_sha256_update(&hash, extra, sizeof(extra));
        uint64_t random;
        if (processor_random(&random)) br_sha256_update(&hash, &random, sizeof(random));
        br_sha256_out(&hash, seed + half * 32);
    }
}

// ---- The date, for checking certificates ----

static void set_certificate_time(void) {
    struct areos_date_time now = areos_date_time();
    // BearSSL counts days from 1 January in the year 0. Work out the day
    // number from the calendar date (this is the usual "days from civil" sum).
    int y = now.year, m = now.month;
    if (m <= 2) y -= 1;
    int era = y / 400;
    int year_of_era = y - era * 400;
    int day_of_year = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + now.day - 1;
    int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    uint32_t days_since_1970 = (uint32_t)(era * 146097 + day_of_era - 719468);
    uint32_t days = days_since_1970 + 719528;
    uint32_t seconds = (uint32_t)(now.hour * 3600 + now.minute * 60 + now.second);
    // AREOS doesn't know your time zone yet: its clock shows local time (in the
    // emulator, your computer's local time), while certificates use universal
    // time, which can be up to 14 hours ahead. Without this allowance a
    // certificate issued a few hours ago could look as if it isn't valid yet.
    // The cost: a certificate that expired in the last 14 hours is still accepted.
    seconds += 14 * 3600;
    days += seconds / 86400;
    seconds %= 86400;
    br_x509_minimal_set_time(&x509, days, seconds);
}

// ---- Starting and driving a connection ----

void tls_start(int tcp_connection, const char *host) {
    connection = tcp_connection;
    sent_so_far = 0;
    request_done = false;
    error_text[0] = '\0';

    br_ssl_client_init_full(&client, &x509, TAs, TAs_NUM);
    set_certificate_time();
    br_ssl_engine_set_buffer(&client.eng, io_buffer, sizeof(io_buffer), 1);
    unsigned char seed[64];
    make_seed(seed);
    br_ssl_engine_inject_entropy(&client.eng, seed, sizeof(seed));
    br_ssl_client_reset(&client, host, 0);   // the host name is checked against the certificate
}

static void failed(const char *why) {
    text_copy(error_text, why, sizeof(error_text));
}

const char *tls_error(void) {
    return error_text;
}

static void describe_error(int code) {
    char text[160] = "";
    if (code >= BR_ERR_X509_OK && code < BR_ERR_X509_OK + 64) {
        switch (code) {
        case BR_ERR_X509_EXPIRED:
            text_copy(text, "The website's security certificate has expired, or this computer's date is wrong "
                            "(AREOS reads it from the clock).", sizeof(text));
            break;
        case BR_ERR_X509_NOT_TRUSTED:
        case BR_ERR_X509_NOT_CA:
            text_copy(text, "The website's security certificate isn't signed by anyone AREOS trusts.", sizeof(text));
            break;
        case BR_ERR_X509_BAD_SERVER_NAME:
            text_copy(text, "The website's security certificate is for a different website name.", sizeof(text));
            break;
        default:
            text_copy(text, "The website's security certificate couldn't be checked", sizeof(text));
            text_append(text, " (certificate error ", sizeof(text));
            text_append_number(text, code - BR_ERR_X509_OK, sizeof(text));
            text_append(text, ").", sizeof(text));
            break;
        }
    } else if (code == BR_ERR_IO) {
        text_copy(text, "The connection broke while setting up security.", sizeof(text));
    } else {
        text_copy(text, "The secure connection couldn't be set up (TLS error ", sizeof(text));
        text_append_number(text, code, sizeof(text));
        text_append(text, "). The website may need something AREOS doesn't support.", sizeof(text));
    }
    failed(text);
}

enum tls_status tls_step(const char *request, int request_length, char *out, int *out_length, int out_max) {
    for (int turn = 0; turn < 400; turn++) {
        unsigned state = br_ssl_engine_current_state(&client.eng);

        if (state & BR_SSL_CLOSED) {
            int error = br_ssl_engine_last_error(&client.eng);
            if (error == BR_ERR_OK || (request_done && error == BR_ERR_IO)) return TLS_DONE;
            describe_error(error);
            return TLS_FAILED;
        }

        if (state & BR_SSL_SENDREC) {            // scrambled bytes for the server
            size_t length;
            unsigned char *data = br_ssl_engine_sendrec_buf(&client.eng, &length);
            int sent = areos_tcp_send(connection, data, (int)length);
            if (sent > 0) { br_ssl_engine_sendrec_ack(&client.eng, (size_t)sent); continue; }
            if (sent < 0) { failed("The connection broke while sending."); return TLS_FAILED; }
            return TLS_WORKING;                   // the network is busy: try again next time
        }

        if (state & BR_SSL_RECVAPP) {            // the page, unscrambled
            size_t length;
            unsigned char *data = br_ssl_engine_recvapp_buf(&client.eng, &length);
            int room = out_max - 1 - *out_length;
            int take = (int)length < room ? (int)length : room;
            for (int i = 0; i < take; i++) out[*out_length + i] = (char)data[i];
            *out_length += take;
            br_ssl_engine_recvapp_ack(&client.eng, length);   // (anything that didn't fit is dropped)
            continue;
        }

        if ((state & BR_SSL_SENDAPP) && !request_done) {      // the handshake is done: ask for the page
            size_t length;
            unsigned char *data = br_ssl_engine_sendapp_buf(&client.eng, &length);
            int left = request_length - sent_so_far;
            int give = (int)length < left ? (int)length : left;
            for (int i = 0; i < give; i++) data[i] = (unsigned char)request[sent_so_far + i];
            br_ssl_engine_sendapp_ack(&client.eng, (size_t)give);
            sent_so_far += give;
            if (sent_so_far >= request_length) {
                br_ssl_engine_flush(&client.eng, 0);
                request_done = true;
            }
            continue;
        }

        if (state & BR_SSL_RECVREC) {            // scrambled bytes from the server
            size_t length;
            unsigned char *data = br_ssl_engine_recvrec_buf(&client.eng, &length);
            int got = areos_tcp_receive(connection, data, (int)length);
            if (got > 0) { br_ssl_engine_recvrec_ack(&client.eng, (size_t)got); continue; }
            if (got < 0) {                        // the server hung up
                if (request_done) return TLS_DONE;
                failed("The website closed the connection while setting up security.");
                return TLS_FAILED;
            }
            return TLS_WORKING;                   // nothing yet
        }

        if (state & BR_SSL_SENDAPP) return TLS_WORKING;   // request already sent: wait for the answer
        return TLS_WORKING;
    }
    return TLS_WORKING;
}

// Random bytes for page scripts (crypto.getRandomValues...): a fresh seed
// for every 64 bytes, mixed through SHA-256.
void tls_random(void *buffer, int length) {
    unsigned char *out = buffer;
    unsigned char seed[64];
    int used = 64;
    for (int i = 0; i < length; i++) {
        if (used >= 64) { make_seed(seed); used = 0; }
        out[i] = seed[used++];
    }
}
