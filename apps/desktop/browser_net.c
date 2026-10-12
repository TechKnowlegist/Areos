// browser_net.c - what a web page's scripts need from the network.
//
// A page's JavaScript can ask for more files (scripts, data) and expects the
// answer in one go. This file does that: it fetches an address and waits for
// the whole answer, following redirects, over http or https. (While it waits,
// the desktop stands still, so it gives up after a while.) The page itself is
// still fetched in the background by app_browser.c.
//
// It also provides the clock, random numbers and window size that the
// JavaScript side asks for (see js/jsplatform.h).

#include "browser.h"
#include "browser_tls.h"
#include "jsplatform.h"

void *malloc(size_t size);
void free(void *p);

#define NET_MAX (6 * 1024 * 1024)
static char *net_buf;

uint64_t jsp_time_ms(void) { return areos_time_ms(); }
void jsp_log(int level, const char *text) { (void)level; (void)text; }
void jsp_announce(const char *text) { gui_announce(text); }

static void fail(struct jsp_response *out, const char *why) {
    text_copy(out->error, why, sizeof(out->error));
}

static bool has_header(const char *headers, const char *name) {
    if (!headers) return false;
    int n = text_length(name);
    for (int i = 0; headers[i]; i++) {
        if (i != 0 && headers[i - 1] != '\n') continue;
        int k = 0;
        while (k < n && to_lower(headers[i + k]) == to_lower(name[k])) k++;
        if (k == n && headers[i + k] == ':') return true;
    }
    return false;
}

static int find_headers_end(const char *buf, int length) {
    for (int i = 0; i + 3 < length; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') return i + 4;
    return -1;
}

static bool header_get(const char *buf, int length, const char *name, char *out, int max) {
    int n = text_length(name);
    for (int i = 0; i < length; i++) {
        if (i != 0 && buf[i - 1] != '\n') continue;
        int k = 0;
        while (k < n && i + k < length && to_lower(buf[i + k]) == to_lower(name[k])) k++;
        if (k == n && buf[i + k] == ':') {
            int j = i + k + 1, m = 0;
            while (j < length && buf[j] == ' ') j++;
            while (j < length && buf[j] != '\r' && buf[j] != '\n' && m < max - 1) out[m++] = buf[j++];
            out[m] = '\0';
            return true;
        }
    }
    return false;
}

static int decode_chunks(char *body, int length) {
    int in = 0, out = 0;
    while (in < length) {
        int size = 0, digits = 0;
        while (in < length) {
            char c = to_lower(body[in]);
            if (c >= '0' && c <= '9') size = size * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') size = size * 16 + (c - 'a' + 10);
            else break;
            in++; digits++;
        }
        while (in < length && body[in] != '\n') in++;
        in++;
        if (digits == 0 || size == 0) break;
        if (in + size > length) size = length - in;
        for (int k = 0; k < size; k++) body[out + k] = body[in + k];
        out += size;
        in += size + 2;
    }
    return out;
}

// One request, no redirects. The raw answer ends up in net_buf. Returns its length, or -1.
static int request_once(const char *method, const char *url, const char *headers, const char *body, int body_len,
                        uint64_t deadline, struct jsp_response *out) {
    char host[128], path[URL_MAX * 2];
    int port;
    bool secure;
    if (!url_split(url, host, sizeof(host), &port, path, sizeof(path), &secure)) { fail(out, "unsupported address"); return -1; }
    uint32_t ip = areos_net_lookup(host);
    if (ip == 0) { fail(out, "website not found"); return -1; }
    int connection = areos_tcp_connect(ip, (uint16_t)port);
    if (connection < 0) { fail(out, "could not connect"); return -1; }

    static char request[8192];
    text_copy(request, method, sizeof(request));
    text_append(request, " ", sizeof(request));
    text_append(request, path, sizeof(request));
    text_append(request, " HTTP/1.0\r\nHost: ", sizeof(request));
    text_append(request, host, sizeof(request));
    text_append(request, "\r\nConnection: close\r\n", sizeof(request));
    if (!has_header(headers, "user-agent")) text_append(request, "User-Agent: Mozilla/5.0 (X11; AREOS x86_64) AppleWebKit/537.36 (KHTML, like Gecko) AREOS-Browser/3.0\r\n", sizeof(request));
    if (!has_header(headers, "accept")) text_append(request, "Accept: */*\r\n", sizeof(request));
    if (headers) text_append(request, headers, sizeof(request));
    if (body_len > 0) {
        text_append(request, "Content-Length: ", sizeof(request));
        text_append_number(request, body_len, sizeof(request));
        text_append(request, "\r\n", sizeof(request));
    }
    text_append(request, "\r\n", sizeof(request));
    int head_len = text_length(request);
    if (body_len > 0 && head_len + body_len < (int)sizeof(request)) {
        for (int i = 0; i < body_len; i++) request[head_len + i] = body[i];
        head_len += body_len;
    }
    int length = 0;
    if (secure) {
        tls_start(connection, host);
        for (;;) {
            int before = length;
            enum tls_status s = tls_step(request, head_len, net_buf, &length, NET_MAX);
            if (s == TLS_FAILED) { fail(out, tls_error()); areos_tcp_close(connection); return -1; }
            if (s == TLS_DONE) break;
            if (areos_time_ms() > deadline) { fail(out, "timed out"); areos_tcp_close(connection); return -1; }
            if (length == before) areos_sleep_ms(2);
        }
    } else {
        int sent = 0;
        while (sent < head_len) {
            int n = areos_tcp_send(connection, request + sent, head_len - sent);
            if (n < 0) { fail(out, "connection broke"); areos_tcp_close(connection); return -1; }
            sent += n;
            if (n == 0) { if (areos_time_ms() > deadline) { fail(out, "timed out"); areos_tcp_close(connection); return -1; } areos_sleep_ms(2); }
        }
        for (;;) {
            int room = NET_MAX - 1 - length;
            if (room <= 0) break;
            int n = areos_tcp_receive(connection, net_buf + length, room > 65536 ? 65536 : room);
            if (n < 0) break;
            if (n == 0) {
                if (areos_time_ms() > deadline) { fail(out, "timed out"); areos_tcp_close(connection); return -1; }
                areos_sleep_ms(2);
                continue;
            }
            length += n;
        }
    }
    areos_tcp_close(connection);
    net_buf[length] = '\0';
    return length;
}

int jsp_http(const char *method, const char *url, const char *headers, const char *body, int body_len,
             int timeout_ms, struct jsp_response *out) {
    browser_pause_resources();
    if (!net_buf) net_buf = malloc(NET_MAX + 1);
    if (!net_buf) { fail(out, "out of memory"); return 1; }
    if (timeout_ms <= 0 || timeout_ms > 20000) timeout_ms = 20000;
    uint64_t deadline = areos_time_ms() + (uint64_t)timeout_ms;
    char current[URL_MAX * 2];
    text_copy(current, url, sizeof(current));
    for (int redirects = 0;; redirects++) {
        int length = request_once(method, current, headers, body, body_len, deadline, out);
        if (length < 0) return 1;
        int head = find_headers_end(net_buf, length);
        if (head < 0 || length < 12 || !text_starts_with(net_buf, "HTTP/")) { fail(out, "bad answer"); return 1; }
        int code = 0;
        for (int i = 9; i < 12; i++) code = code * 10 + (net_buf[i] - '0');
        char location[URL_MAX];
        if (code >= 300 && code < 400 && header_get(net_buf, head, "location", location, sizeof(location))) {
            if (redirects >= 5) { fail(out, "too many redirects"); return 1; }
            char next[URL_MAX * 2];
            url_resolve(current, location, next, sizeof(next));
            text_copy(current, next, sizeof(current));
            if (code != 307 && code != 308) { method = "GET"; body = NULL; body_len = 0; }
            continue;
        }
        out->status = code;
        int s = 9 + 3;
        while (net_buf[s] == ' ') s++;
        int n = 0;
        while (net_buf[s] && net_buf[s] != '\r' && n < (int)sizeof(out->status_text) - 1) out->status_text[n++] = net_buf[s++];
        out->status_text[n] = '\0';
        text_copy(out->final_url, current, sizeof(out->final_url));
        int line_end = 0;
        while (line_end < head && net_buf[line_end] != '\n') line_end++;
        line_end++;
        out->headers_len = head - line_end;
        out->headers = malloc((size_t)out->headers_len + 1);
        if (out->headers) { for (int i = 0; i < out->headers_len; i++) out->headers[i] = net_buf[line_end + i]; out->headers[out->headers_len] = '\0'; }
        char *content = net_buf + head;
        int content_len = length - head;
        char encoding[40];
        if (header_get(net_buf, head, "transfer-encoding", encoding, sizeof(encoding)) && text_starts_with(encoding, "chunked"))
            content_len = decode_chunks(content, content_len);
        out->body = malloc((size_t)content_len + 1);
        if (!out->body) { fail(out, "out of memory"); free(out->headers); out->headers = NULL; return 1; }
        for (int i = 0; i < content_len; i++) out->body[i] = content[i];
        out->body[content_len] = '\0';
        out->body_len = content_len;
        return 0;
    }
}

void jsp_random(void *buffer, int length) { tls_random(buffer, length); }
