// pci.c - finding devices on the PCI bus.
//
// PCI "configuration space" is reached through two I/O ports: write which
// device and register you want to 0xCF8, then read or write 0xCFC.

#include "pci.h"
#include "io.h"

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA    0xCFC

static uint32_t address_of(struct pci_device d, uint8_t offset) {
    return (1u << 31) | ((uint32_t)d.bus << 16) | ((uint32_t)d.slot << 11)
         | ((uint32_t)d.function << 8) | (offset & 0xFC);
}

uint32_t pci_read32(struct pci_device d, uint8_t offset) {
    outl(CONFIG_ADDRESS, address_of(d, offset));
    return inl(CONFIG_DATA);
}

void pci_write32(struct pci_device d, uint8_t offset, uint32_t value) {
    outl(CONFIG_ADDRESS, address_of(d, offset));
    outl(CONFIG_DATA, value);
}

bool pci_find(uint16_t vendor, uint16_t device, struct pci_device *out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int function = 0; function < 8; function++) {
                struct pci_device d = { (uint8_t)bus, (uint8_t)slot, (uint8_t)function };
                uint32_t id = pci_read32(d, 0x00);
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (function == 0) break;   // no device in this slot at all
                    continue;
                }
                if ((id & 0xFFFF) == vendor && (id >> 16) == device) {
                    *out = d;
                    return true;
                }
            }
        }
    }
    return false;
}

uint16_t pci_io_base(struct pci_device d, int bar) {
    uint32_t value = pci_read32(d, (uint8_t)(0x10 + bar * 4));
    return (uint16_t)(value & ~3u);   // bit 0 says "I/O ports"; the rest is the address
}

void pci_enable_io_and_dma(struct pci_device d) {
    uint32_t command = pci_read32(d, 0x04);
    command |= (1u << 0) | (1u << 2);   // I/O space on, bus mastering on
    pci_write32(d, 0x04, command);
}
