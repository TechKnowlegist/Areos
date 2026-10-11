// pci.h - finding devices on the PCI bus.
//
// Most devices inside a PC (sound cards, network cards, disk controllers)
// sit on the PCI bus. Each one has a vendor and device number saying what
// it is, and "BARs" saying where its registers are.

#ifndef AREOS_PCI_H
#define AREOS_PCI_H

#include <stdbool.h>
#include <stdint.h>

struct pci_device {
    uint8_t bus, slot, function;
};

// Look for a device by its vendor and device numbers.
bool pci_find(uint16_t vendor, uint16_t device, struct pci_device *out);

uint32_t pci_read32(struct pci_device d, uint8_t offset);
void pci_write32(struct pci_device d, uint8_t offset, uint32_t value);

// Where register block `bar` (0-5) lives, for devices reached through I/O ports.
uint16_t pci_io_base(struct pci_device d, int bar);

// Let the device read memory by itself ("bus mastering"), and answer on its ports.
void pci_enable_io_and_dma(struct pci_device d);

#endif
