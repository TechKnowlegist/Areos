// net.c - Ethernet, ARP, IP, ICMP, UDP, DHCP, DNS and TCP.
//
// Everything that arrives is handled from the timer tick (net_tick), so it
// never runs at the same time as a program's network request (those run
// with interrupts off, apart from while they wait).
//
// Numbers in packets are "big-endian" (most significant byte first), the
// opposite of how x86 keeps them, so they're always read and written a
// byte at a time with the helpers below.

#include "net.h"
#include "e1000.h"
#include "timer.h"
#include "thread.h"
#include "cpu.h"
#include "console.h"
#include "memory.h"
#include "lang_en.h"

// ---- Byte helpers ----

static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

// The internet checksum: add up 16-bit words, fold the carries, flip the bits.
static uint32_t checksum_add(uint32_t sum, const uint8_t *data, int length) {
    for (int i = 0; i + 1 < length; i += 2) sum += get16(data + i);
    if (length & 1) sum += (uint32_t)data[length - 1] << 8;
    return sum;
}
static uint16_t checksum_finish(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint32_t random_state = 0x12345678;
static uint32_t random_number(void) {
    random_state ^= (uint32_t)timer_ms();
    random_state = random_state * 1103515245u + 12345u;
    return random_state;
}

// ---- Our settings (from DHCP) ----

static bool card_ok = false;
static uint32_t my_ip = 0, router_ip = 0, dns_ip = 0, netmask = 0xFFFFFF00;
static uint8_t my_mac[6];
static const uint8_t broadcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

uint32_t net_address(void) {
    return my_ip;
}

void net_address_text(uint32_t a, char *out) {
    int n = 0;
    for (int part = 3; part >= 0; part--) {
        uint32_t v = (a >> (part * 8)) & 0xFF;
        if (v >= 100) out[n++] = (char)('0' + v / 100);
        if (v >= 10) out[n++] = (char)('0' + (v / 10) % 10);
        out[n++] = (char)('0' + v % 10);
        if (part > 0) out[n++] = '.';
    }
    out[n] = '\0';
}

// Wait a moment for packets, even if called with interrupts off (from a
// program's request): they're switched on just while resting.
static void wait_a_moment(void) {
    uint64_t flags = cpu_interrupts_save();
    asm volatile ("sti");
    thread_idle(timer_ms() + 2);
    if (!(flags & (1 << 9))) asm volatile ("cli");
}

// Packets are built in shared buffers, and the timer tick may send too
// (acknowledgements, answers to ARP): so senders that aren't the tick
// turn interrupts off while they build and send.
#define LOCK() uint64_t lock_flags = cpu_interrupts_save()
#define UNLOCK() cpu_interrupts_restore(lock_flags)

// ---- Ethernet ----

#define TYPE_IP  0x0800
#define TYPE_ARP 0x0806

static uint8_t frame_out[1514];

static void ethernet_send(const uint8_t *to_mac, uint16_t type, const uint8_t *payload, int length) {
    if (length > 1500) return;
    memcpy(frame_out, to_mac, 6);
    memcpy(frame_out + 6, my_mac, 6);
    put16(frame_out + 12, type);
    memcpy(frame_out + 14, payload, (size_t)length);
    e1000_send(frame_out, (uint16_t)(14 + length));
}

// ---- ARP: whose card has this address? ----

#define ARP_CACHE 8
static struct { uint32_t ip; uint8_t mac[6]; } arp_cache[ARP_CACHE];
static int arp_next = 0;

static void arp_remember(uint32_t ip, const uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE; i++) {
        if (arp_cache[i].ip == ip) { memcpy(arp_cache[i].mac, mac, 6); return; }
    }
    arp_cache[arp_next].ip = ip;
    memcpy(arp_cache[arp_next].mac, mac, 6);
    arp_next = (arp_next + 1) % ARP_CACHE;
}

static const uint8_t *arp_find(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE; i++) {
        if (arp_cache[i].ip == ip && ip != 0) return arp_cache[i].mac;
    }
    return NULL;
}

static void arp_send(uint16_t operation, const uint8_t *to_mac, uint32_t to_ip) {
    uint8_t p[28];
    put16(p, 1);              // Ethernet
    put16(p + 2, TYPE_IP);
    p[4] = 6; p[5] = 4;       // address sizes
    put16(p + 6, operation);  // 1 = who has?, 2 = it's me
    memcpy(p + 8, my_mac, 6);
    put32(p + 14, my_ip);
    memcpy(p + 18, operation == 1 ? (const uint8_t *)"\0\0\0\0\0\0" : to_mac, 6);
    put32(p + 24, to_ip);
    ethernet_send(operation == 1 ? broadcast_mac : to_mac, TYPE_ARP, p, 28);
}

static void arp_receive(const uint8_t *p, int length) {
    if (length < 28) return;
    uint16_t operation = get16(p + 6);
    uint32_t sender_ip = get32(p + 14);
    uint32_t target_ip = get32(p + 24);
    if (sender_ip != 0) arp_remember(sender_ip, p + 8);
    if (operation == 1 && target_ip == my_ip && my_ip != 0) {
        arp_send(2, p + 8, sender_ip);   // someone asked for us: answer
    }
}

// Ask for an address's card, waiting up to a second.
static const uint8_t *arp_resolve(uint32_t ip) {
    for (int attempt = 0; attempt < 4; attempt++) {
        const uint8_t *mac = arp_find(ip);
        if (mac) return mac;
        { LOCK(); arp_send(1, NULL, ip); UNLOCK(); }
        uint64_t give_up = timer_ms() + 250;
        while (timer_ms() < give_up) {
            wait_a_moment();
            if ((mac = arp_find(ip)) != NULL) return mac;
        }
    }
    return NULL;
}

// ---- IP ----

#define PROTO_ICMP 1
#define PROTO_TCP  6
#define PROTO_UDP  17

static uint8_t ip_out[1500];
static uint16_t ip_id = 1;

static bool ip_send(uint32_t to, uint8_t protocol, const uint8_t *payload, int length) {
    if (length > 1480) return false;
    const uint8_t *mac;
    if (to == 0xFFFFFFFF) {
        mac = broadcast_mac;
    } else {
        // Same network: straight there. Anywhere else: via the router.
        uint32_t hop = ((to & netmask) == (my_ip & netmask)) ? to : router_ip;
        mac = arp_find(hop);
        if (mac == NULL) {
            arp_send(1, NULL, hop);   // ask, and let the sender try again later
            return false;
        }
    }
    uint8_t *h = ip_out;
    h[0] = 0x45;               // version 4, 20-byte header
    h[1] = 0;
    put16(h + 2, (uint16_t)(20 + length));
    put16(h + 4, ip_id++);
    put16(h + 6, 0x4000);      // don't fragment
    h[8] = 64;                 // time to live
    h[9] = protocol;
    put16(h + 10, 0);
    put32(h + 12, my_ip);
    put32(h + 16, to);
    put16(h + 10, checksum_finish(checksum_add(0, h, 20)));
    memcpy(h + 20, payload, (size_t)length);
    ethernet_send(mac, TYPE_IP, h, 20 + length);
    return true;
}

static void udp_receive(uint32_t from, const uint8_t *p, int length);
static void tcp_segment(uint32_t from, const uint8_t *p, int length);

static void icmp_receive(uint32_t from, const uint8_t *p, int length) {
    if (length < 8 || p[0] != 8) return;   // only echo requests ("ping")
    static uint8_t reply[1480];
    if (length > 1480) return;
    memcpy(reply, p, (size_t)length);
    reply[0] = 0;   // echo reply
    put16(reply + 2, 0);
    put16(reply + 2, checksum_finish(checksum_add(0, reply, length)));
    ip_send(from, PROTO_ICMP, reply, length);
}

static void ip_receive(const uint8_t *p, int length) {
    if (length < 20 || (p[0] >> 4) != 4) return;
    int header = (p[0] & 0xF) * 4;
    int total = get16(p + 2);
    if (total > length || header > total) return;
    if (get16(p + 6) & 0x3FFF) return;   // pieces of a bigger packet: not handled
    uint32_t from = get32(p + 12), to = get32(p + 16);
    if (to != my_ip && to != 0xFFFFFFFF && my_ip != 0) return;
    const uint8_t *payload = p + header;
    int size = total - header;
    switch (p[9]) {
    case PROTO_ICMP: icmp_receive(from, payload, size); break;
    case PROTO_UDP:  udp_receive(from, payload, size); break;
    case PROTO_TCP:  tcp_segment(from, payload, size); break;
    }
}

static void frame_received(const uint8_t *frame, uint16_t length) {
    if (length < 14) return;
    uint16_t type = get16(frame + 12);
    if (type == TYPE_ARP) arp_receive(frame + 14, length - 14);
    else if (type == TYPE_IP) ip_receive(frame + 14, length - 14);
}

// ---- UDP ----

static uint8_t udp_out[1480];

static void udp_send(uint32_t to, uint16_t from_port, uint16_t to_port, const uint8_t *data, int length) {
    if (length > 1472) return;
    put16(udp_out, from_port);
    put16(udp_out + 2, to_port);
    put16(udp_out + 4, (uint16_t)(8 + length));
    put16(udp_out + 6, 0);     // checksum optional in IPv4: leave it out
    memcpy(udp_out + 8, data, (size_t)length);
    ip_send(to, PROTO_UDP, udp_out, 8 + length);
}

// ---- DHCP: asking the network for an address ----

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
static uint32_t dhcp_xid;
static uint32_t dhcp_offered = 0, dhcp_server = 0;
static volatile int dhcp_stage = 0;   // 0 nothing, 1 offered, 2 acknowledged

static void dhcp_send(uint8_t type) {
    uint8_t p[300];
    memset(p, 0, sizeof(p));
    p[0] = 1; p[1] = 1; p[2] = 6;               // request, Ethernet, 6-byte MAC
    put32(p + 4, dhcp_xid);
    put16(p + 10, 0x8000);                      // please broadcast the answer
    memcpy(p + 28, my_mac, 6);
    put32(p + 236, 0x63825363);                 // the "magic cookie"
    int o = 240;
    p[o++] = 53; p[o++] = 1; p[o++] = type;     // message type
    if (type == 3) {                            // a request names what it accepts
        p[o++] = 50; p[o++] = 4; put32(p + o, dhcp_offered); o += 4;
        p[o++] = 54; p[o++] = 4; put32(p + o, dhcp_server); o += 4;
    }
    p[o++] = 55; p[o++] = 3; p[o++] = 1; p[o++] = 3; p[o++] = 6;   // want: mask, router, DNS
    p[o++] = 255;
    udp_send(0xFFFFFFFF, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, p, o);
}

static void dhcp_receive(const uint8_t *p, int length) {
    if (length < 240 || p[0] != 2 || get32(p + 4) != dhcp_xid || get32(p + 236) != 0x63825363) return;
    uint32_t offered = get32(p + 16);
    int type = 0;
    uint32_t mask = 0, router = 0, dns = 0, server = 0;
    for (int o = 240; o < length && p[o] != 255;) {
        if (p[o] == 0) { o++; continue; }
        int code = p[o], size = p[o + 1];
        const uint8_t *v = p + o + 2;
        if (o + 2 + size > length) break;
        if (code == 53 && size >= 1) type = v[0];
        if (code == 1 && size >= 4) mask = get32(v);
        if (code == 3 && size >= 4) router = get32(v);
        if (code == 6 && size >= 4) dns = get32(v);
        if (code == 54 && size >= 4) server = get32(v);
        o += 2 + size;
    }
    if (type == 2 && dhcp_stage == 0) {          // offer
        dhcp_offered = offered;
        dhcp_server = server;
        dhcp_stage = 1;
    } else if (type == 5 && dhcp_stage == 1) {   // acknowledged: it's ours
        my_ip = offered;
        if (mask) netmask = mask;
        router_ip = router;
        dns_ip = dns ? dns : router;
        dhcp_stage = 2;
    }
}

// ---- DNS: names to addresses ----

#define DNS_PORT 53
static uint16_t dns_id;
static uint16_t dns_local_port;
static volatile uint32_t dns_answer;
static volatile bool dns_done;

// Skip over a (possibly compressed) name in a DNS message.
static int dns_skip_name(const uint8_t *p, int length, int o) {
    while (o < length) {
        uint8_t n = p[o];
        if (n == 0) return o + 1;
        if ((n & 0xC0) == 0xC0) return o + 2;   // a pointer to a name earlier on
        o += n + 1;
    }
    return length;
}

static void dns_receive(const uint8_t *p, int length) {
    if (length < 12 || get16(p) != dns_id) return;
    int questions = get16(p + 4), answers = get16(p + 6);
    int o = 12;
    for (int q = 0; q < questions; q++) o = dns_skip_name(p, length, o) + 4;
    for (int a = 0; a < answers && o < length; a++) {
        o = dns_skip_name(p, length, o);
        if (o + 10 > length) break;
        uint16_t type = get16(p + o), size = get16(p + o + 8);
        if (type == 1 && size == 4 && o + 14 <= length) {   // an "A" record: the address
            dns_answer = get32(p + o + 10);
            dns_done = true;
            return;
        }
        o += 10 + size;
    }
    dns_done = true;   // answered, but no address
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

uint32_t net_lookup(const char *name) {
    if (my_ip == 0 || name == NULL || name[0] == '\0') return 0;
    // Already a number, like "10.0.2.2"?
    uint32_t value = 0;
    int parts = 0, digits = 0;
    uint32_t part = 0;
    bool numeric = true;
    for (int i = 0;; i++) {
        char c = name[i];
        if (is_digit(c)) { part = part * 10 + (uint32_t)(c - '0'); digits++; }
        else if ((c == '.' || c == '\0') && digits > 0 && part < 256) {
            value = (value << 8) | part; parts++; part = 0; digits = 0;
            if (c == '\0') break;
        } else { numeric = false; break; }
    }
    if (numeric && parts == 4) return value;

    // Build the question: the name as length-prefixed pieces, type A, class IN.
    uint8_t q[300];
    int o = 12;
    memset(q, 0, 12);
    dns_id = (uint16_t)random_number();
    put16(q, dns_id);
    put16(q + 2, 0x0100);   // please look it up fully ("recursion desired")
    put16(q + 4, 1);
    int start = 0;
    for (int i = 0;; i++) {
        if (name[i] == '.' || name[i] == '\0') {
            int n = i - start;
            if (n <= 0 || n > 63 || o + n + 6 > (int)sizeof(q)) return 0;
            q[o++] = (uint8_t)n;
            for (int k = 0; k < n; k++) q[o++] = (uint8_t)name[start + k];
            start = i + 1;
            if (name[i] == '\0') break;
        }
    }
    q[o++] = 0;
    put16(q + o, 1); put16(q + o + 2, 1); o += 4;

    for (int attempt = 0; attempt < 3; attempt++) {
        dns_done = false;
        dns_answer = 0;
        dns_local_port = (uint16_t)(40000 + random_number() % 20000);
        { LOCK(); udp_send(dns_ip, dns_local_port, DNS_PORT, q, o); UNLOCK(); }
        uint64_t give_up = timer_ms() + 1500;
        while (!dns_done && timer_ms() < give_up) wait_a_moment();
        if (dns_done) return dns_answer;
    }
    return 0;
}

static void udp_receive(uint32_t from, const uint8_t *p, int length) {
    (void)from;
    if (length < 8) return;
    uint16_t to_port = get16(p + 2);
    int size = get16(p + 4) - 8;
    if (size < 0 || size > length - 8) return;
    if (to_port == DHCP_CLIENT_PORT) dhcp_receive(p + 8, size);
    else if (to_port == dns_local_port && dns_local_port != 0) dns_receive(p + 8, size);
}

// ---- TCP: reliable streams ----

#define TCP_MAX 4
#define TCP_MSS 1400
#define SEND_SIZE 16384
#define RECEIVE_SIZE 65536

#define FLAG_FIN 0x01
#define FLAG_SYN 0x02
#define FLAG_RST 0x04
#define FLAG_PSH 0x08
#define FLAG_ACK 0x10

enum tcp_state { TCP_FREE, TCP_SYN_SENT, TCP_OPEN, TCP_CLOSED };

struct tcp {
    enum tcp_state state;
    uint32_t remote_ip;
    uint16_t local_port, remote_port;
    uint32_t send_unacked;      // oldest byte not yet acknowledged
    uint32_t send_next;         // next byte to send
    uint32_t receive_next;      // next byte we expect from them
    uint32_t their_window;
    uint8_t send_buffer[SEND_SIZE];   // bytes from send_unacked onwards
    int send_length;
    uint8_t receive_buffer[RECEIVE_SIZE];
    int receive_start, receive_length;
    bool fin_received;          // they've finished sending
    bool fin_wanted;            // we've finished sending (once the buffer empties)
    bool fin_sent;
    uint64_t last_sent_ms;
    int retries;
};

static struct tcp sockets[TCP_MAX];
static uint16_t next_port = 49152;

static uint8_t tcp_out[1480];

static void tcp_send_segment(struct tcp *s, uint32_t seq, uint8_t flags, const uint8_t *data, int length) {
    int header = (flags & FLAG_SYN) ? 24 : 20;
    uint8_t *t = tcp_out;
    put16(t, s->local_port);
    put16(t + 2, s->remote_port);
    put32(t + 4, seq);
    put32(t + 8, (flags & FLAG_ACK) ? s->receive_next : 0);
    t[12] = (uint8_t)((header / 4) << 4);
    t[13] = flags;
    int room = RECEIVE_SIZE - s->receive_length;
    put16(t + 14, (uint16_t)(room > 65535 ? 65535 : room));
    put16(t + 16, 0);
    put16(t + 18, 0);
    if (flags & FLAG_SYN) {     // tell them the biggest piece we take
        t[20] = 2; t[21] = 4; put16(t + 22, TCP_MSS);
    }
    if (length > 0) memcpy(t + header, data, (size_t)length);
    // The checksum covers a "pseudo header" with both addresses too.
    uint8_t pseudo[12];
    put32(pseudo, my_ip);
    put32(pseudo + 4, s->remote_ip);
    pseudo[8] = 0; pseudo[9] = PROTO_TCP;
    put16(pseudo + 10, (uint16_t)(header + length));
    uint32_t sum = checksum_add(0, pseudo, 12);
    sum = checksum_add(sum, t, header + length);
    put16(t + 16, checksum_finish(sum));
    ip_send(s->remote_ip, PROTO_TCP, t, header + length);
}

// Send whatever's waiting (or resend, after a timeout).
static void tcp_output(struct tcp *s) {
    if (s->state != TCP_OPEN) return;
    int sent_already = (int)(s->send_next - s->send_unacked);
    int window = (int)s->their_window;
    if (window < TCP_MSS) window = TCP_MSS;   // keep a trickle going
    while (sent_already < s->send_length && sent_already < window) {
        int size = s->send_length - sent_already;
        if (size > TCP_MSS) size = TCP_MSS;
        tcp_send_segment(s, s->send_next, FLAG_ACK | FLAG_PSH, s->send_buffer + sent_already, size);
        s->send_next += (uint32_t)size;
        sent_already += size;
        s->last_sent_ms = timer_ms();
    }
    if (s->fin_wanted && !s->fin_sent && sent_already >= s->send_length) {
        tcp_send_segment(s, s->send_next, FLAG_ACK | FLAG_FIN, NULL, 0);
        s->send_next++;
        s->fin_sent = true;
        s->last_sent_ms = timer_ms();
    }
}

static void tcp_segment(uint32_t from, const uint8_t *p, int length) {
    if (length < 20) return;
    uint16_t from_port = get16(p), to_port = get16(p + 2);
    struct tcp *s = NULL;
    for (int i = 0; i < TCP_MAX; i++) {
        struct tcp *c = &sockets[i];
        if (c->state != TCP_FREE && c->remote_ip == from && c->remote_port == from_port
            && c->local_port == to_port) {
            s = c;
            break;
        }
    }
    if (s == NULL || s->state == TCP_CLOSED) return;
    uint32_t seq = get32(p + 4), ack = get32(p + 8);
    int header = (p[12] >> 4) * 4;
    uint8_t flags = p[13];
    if (header < 20 || header > length) return;
    const uint8_t *data = p + header;
    int size = length - header;

    if (flags & FLAG_RST) {
        s->state = TCP_CLOSED;
        return;
    }
    if (s->state == TCP_SYN_SENT) {
        if ((flags & FLAG_SYN) && (flags & FLAG_ACK) && ack == s->send_next) {
            s->receive_next = seq + 1;
            s->send_unacked = ack;
            s->their_window = get16(p + 14);
            s->state = TCP_OPEN;
            s->retries = 0;
            tcp_send_segment(s, s->send_next, FLAG_ACK, NULL, 0);
        }
        return;
    }

    // What they've received of ours.
    if (flags & FLAG_ACK) {
        uint32_t newly = ack - s->send_unacked;
        uint32_t in_flight = s->send_next - s->send_unacked;
        if (newly > 0 && newly <= in_flight) {
            int data_acked = (int)newly;
            if (data_acked > s->send_length) data_acked = s->send_length;   // the rest was our FIN
            memmove(s->send_buffer, s->send_buffer + data_acked, (size_t)(s->send_length - data_acked));
            s->send_length -= data_acked;
            s->send_unacked = ack;
            s->retries = 0;
            s->last_sent_ms = timer_ms();
        }
        s->their_window = get16(p + 14);
    }

    // Their data, if it's the next piece we expect.
    bool need_ack = false;
    if (size > 0) {
        need_ack = true;
        if (seq != s->receive_next) {
            // Partly old? Keep just the new part.
            uint32_t behind = s->receive_next - seq;
            if (behind < (uint32_t)size) {
                data += behind;
                size -= (int)behind;
                seq = s->receive_next;
            }
        }
        if (seq == s->receive_next && !s->fin_received) {
            int room = RECEIVE_SIZE - s->receive_length;
            if (size > room) size = room;
            for (int i = 0; i < size; i++) {
                s->receive_buffer[(s->receive_start + s->receive_length + i) % RECEIVE_SIZE] = data[i];
            }
            s->receive_length += size;
            s->receive_next += (uint32_t)size;
        }
    }
    if ((flags & FLAG_FIN) && seq + (uint32_t)size == s->receive_next && !s->fin_received) {
        s->fin_received = true;
        s->receive_next++;
        need_ack = true;
    }
    if (need_ack) tcp_send_segment(s, s->send_next, FLAG_ACK, NULL, 0);
    tcp_output(s);
}

static struct tcp *socket_for(int handle) {
    if (handle < 0 || handle >= TCP_MAX || sockets[handle].state == TCP_FREE) return NULL;
    return &sockets[handle];
}

int tcp_connect(uint32_t address, uint16_t port) {
    if (my_ip == 0 || address == 0) return -1;
    int handle = -1;
    for (int i = 0; i < TCP_MAX; i++) {
        if (sockets[i].state == TCP_FREE) { handle = i; break; }
    }
    if (handle < 0) return -1;
    // Make sure we can reach the next hop first.
    uint32_t hop = ((address & netmask) == (my_ip & netmask)) ? address : router_ip;
    if (arp_resolve(hop) == NULL) return -1;

    struct tcp *s = &sockets[handle];
    memset(s, 0, sizeof(*s));
    s->remote_ip = address;
    s->remote_port = port;
    s->local_port = next_port++;
    if (next_port == 0 || next_port < 49152) next_port = 49152;
    uint32_t start = random_number();
    s->send_unacked = start;
    s->send_next = start + 1;     // the SYN counts as one byte
    s->state = TCP_SYN_SENT;
    s->their_window = TCP_MSS;
    { LOCK(); tcp_send_segment(s, start, FLAG_SYN, NULL, 0); UNLOCK(); }
    s->last_sent_ms = timer_ms();

    uint64_t give_up = timer_ms() + 6000;
    while (s->state == TCP_SYN_SENT && timer_ms() < give_up) wait_a_moment();
    if (s->state != TCP_OPEN) {
        s->state = TCP_FREE;
        return -1;
    }
    return handle;
}

int tcp_send(int handle, const void *data, int length) {
    struct tcp *s = socket_for(handle);
    if (s == NULL || s->state != TCP_OPEN || s->fin_wanted || length <= 0) return s ? 0 : -1;
    int room = SEND_SIZE - s->send_length;
    if (length > room) length = room;
    LOCK();
    memcpy(s->send_buffer + s->send_length, data, (size_t)length);
    s->send_length += length;
    tcp_output(s);
    UNLOCK();
    return length;
}

int tcp_receive(int handle, void *buffer, int max) {
    struct tcp *s = socket_for(handle);
    if (s == NULL) return -1;
    if (s->receive_length == 0) {
        return (s->fin_received || s->state == TCP_CLOSED) ? -1 : 0;
    }
    int bytes_before = s->receive_length;
    int n = s->receive_length < max ? s->receive_length : max;
    uint8_t *out = buffer;
    for (int i = 0; i < n; i++) out[i] = s->receive_buffer[(s->receive_start + i) % RECEIVE_SIZE];
    s->receive_start = (s->receive_start + n) % RECEIVE_SIZE;
    s->receive_length -= n;
    // If our window had nearly closed, tell them there's room again.
    if (bytes_before > RECEIVE_SIZE - TCP_MSS * 2 && s->state == TCP_OPEN) {
        LOCK();
        tcp_send_segment(s, s->send_next, FLAG_ACK, NULL, 0);
        UNLOCK();
    }
    return n;
}

void tcp_close(int handle) {
    struct tcp *s = socket_for(handle);
    if (s == NULL) return;
    LOCK();
    if (s->state == TCP_OPEN) {
        s->fin_wanted = true;
        tcp_output(s);
        // Say goodbye, and don't wait for an answer.
        tcp_send_segment(s, s->send_next, FLAG_ACK | FLAG_RST, NULL, 0);
    }
    s->state = TCP_FREE;
    UNLOCK();
}

// Resend anything that hasn't been acknowledged for a while.
static void tcp_timers(void) {
    uint64_t now = timer_ms();
    for (int i = 0; i < TCP_MAX; i++) {
        struct tcp *s = &sockets[i];
        if (s->state != TCP_OPEN && s->state != TCP_SYN_SENT) continue;
        bool waiting = s->send_next != s->send_unacked;
        if (!waiting) continue;
        uint64_t timeout = 400u << (s->retries < 4 ? s->retries : 4);
        if (now - s->last_sent_ms < timeout) continue;
        if (++s->retries > 8) {
            s->state = TCP_CLOSED;   // they've gone quiet: give up
            continue;
        }
        s->last_sent_ms = now;
        if (s->state == TCP_SYN_SENT) {
            tcp_send_segment(s, s->send_unacked, FLAG_SYN, NULL, 0);
        } else {
            // Go back to the oldest unacknowledged byte and send from there.
            s->send_next = s->send_unacked;
            s->fin_sent = false;
            tcp_output(s);
        }
    }
}

// ---- Start-up and the tick ----

void net_tick(void) {
    if (!card_ok) return;
    e1000_poll(frame_received);
    tcp_timers();
}

void net_init(void) {
    if (!e1000_init()) {
        console_print(STR_NET_NO_CARD "\n");
        return;
    }
    memcpy(my_mac, e1000_mac(), 6);
    card_ok = true;

    // DHCP: discover, wait for an offer, request it, wait for the go-ahead.
    dhcp_xid = random_number();
    for (int attempt = 0; attempt < 3 && dhcp_stage < 2; attempt++) {
        dhcp_stage = 0;
        { LOCK(); dhcp_send(1); UNLOCK(); }
        uint64_t give_up = timer_ms() + 1500;
        while (dhcp_stage == 0 && timer_ms() < give_up) wait_a_moment();
        if (dhcp_stage != 1) continue;
        { LOCK(); dhcp_send(3); UNLOCK(); }
        give_up = timer_ms() + 1500;
        while (dhcp_stage == 1 && timer_ms() < give_up) wait_a_moment();
    }
    if (dhcp_stage != 2) {
        my_ip = 0;
        console_print(STR_NET_NO_ADDRESS "\n");
        return;
    }
    char text[20];
    net_address_text(my_ip, text);
    console_print(STR_NET_READY_1);
    console_print(text);
    console_print(STR_NET_READY_2 "\n");
    arp_resolve(router_ip);   // so the first web page doesn't have to wait
}
