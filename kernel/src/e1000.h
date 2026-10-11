// e1000.h - the network card driver (Intel 8254x "e1000", which QEMU,
// VirtualBox and VMware can all pretend to be).

#ifndef AREOS_E1000_H
#define AREOS_E1000_H

#include <stdbool.h>
#include <stdint.h>

// Find and start the card. False if there isn't one.
bool e1000_init(void);

// The card's own address (its "MAC address"), 6 bytes.
const uint8_t *e1000_mac(void);

// Send one Ethernet frame (up to 1514 bytes).
bool e1000_send(const void *frame, uint16_t length);

// Hand every frame that has arrived to `receive`.
void e1000_poll(void (*receive)(const uint8_t *frame, uint16_t length));

#endif
