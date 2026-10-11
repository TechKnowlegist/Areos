// net.h - networking: talking to other computers (and the internet).
//
// AREOS speaks the internet's basic languages ("protocols"), stacked up
// like this, each one carried inside the one below:
//
//   TCP    a reliable two-way stream of bytes (what web pages use)
//   UDP    single messages, no promises (DNS and DHCP use it)
//   IP     getting a packet to the right computer, anywhere
//   ARP    finding a computer's network card on our own network
//   Ethernet   the frames the network card actually sends
//
// At start-up, DHCP asks the network for an address to use. DNS turns
// names like "example.com" into numbered addresses.
//
// Addresses are kept as one number: 10.0.2.15 is 0x0A00020F.

#ifndef AREOS_NET_H
#define AREOS_NET_H

#include <stdbool.h>
#include <stdint.h>

// Start the network card and ask for an address. Prints how it went.
void net_init(void);

// Called by the timer: picks up arrived packets, resends lost ones.
void net_tick(void);

// Our address (0 if we don't have one).
uint32_t net_address(void);

// Write an address as text, like "10.0.2.15".
void net_address_text(uint32_t address, char *out);

// Look up a name. Returns its address, or 0 if it couldn't be found.
// Waits (up to a few seconds) for the answer.
uint32_t net_lookup(const char *name);

// ---- TCP connections ----
// Each connection is a small number ("handle"); -1 means it failed.

int tcp_connect(uint32_t address, uint16_t port);       // waits up to 6 seconds
int tcp_send(int handle, const void *data, int length); // how much was accepted
// Copy arrived bytes into `buffer`. 0 = nothing yet, -1 = the other side
// has finished and everything has been read (or the connection broke).
int tcp_receive(int handle, void *buffer, int max);
void tcp_close(int handle);

#endif
