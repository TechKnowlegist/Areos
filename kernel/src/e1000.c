// e1000.c - the network card driver.
//
// Like the sound card, the network card reads and writes memory by itself.
// We give it two rings of "descriptors", each pointing at a buffer:
//   receive ring:  empty buffers it fills with frames as they arrive
//   transmit ring: frames we've written, for it to send
// Each ring has a head (the card's place) and a tail (ours). The card's
// registers are reached through memory ("MMIO") rather than I/O ports.

#include "e1000.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "memory.h"

#define VENDOR_INTEL   0x8086
#define DEVICE_82540EM 0x100E   // the model QEMU emulates with model=e1000

// Registers (offsets from the start of the card's memory).
#define REG_CTRL   0x0000
#define REG_EERD   0x0014   // reading the card's settings chip (EEPROM)
#define REG_IMC    0x00D8   // switch interrupts off
#define REG_RCTL   0x0100   // receive control
#define REG_TCTL   0x0400   // transmit control
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800   // receive ring: where, how big, head, tail
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800   // transmit ring
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_MTA    0x5200   // multicast table (cleared)
#define REG_RAL    0x5400   // our MAC address
#define REG_RAH    0x5404

#define CTRL_SET_LINK_UP (1u << 6)
#define CTRL_RESET       (1u << 26)
#define RCTL_ENABLE      (1u << 1)
#define RCTL_BROADCAST   (1u << 15)
#define RCTL_STRIP_CRC   (1u << 26)   // buffers of 2048 bytes is the default size
#define TCTL_ENABLE      (1u << 1)
#define TCTL_PAD_SHORT   (1u << 3)

#define RX_COUNT 32
#define TX_COUNT 16
#define BUFFER_SIZE 2048

struct rx_descriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;     // bit 0: "done" (a frame is here)
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct tx_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t cso;
    uint8_t command;    // end of packet, insert checksum, report status
    uint8_t status;     // bit 0: "done" (sent)
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

#define TX_CMD_EOP  0x01
#define TX_CMD_IFCS 0x02
#define TX_CMD_RS   0x08

static volatile uint8_t *registers;
static struct rx_descriptor *rx_ring;
static struct tx_descriptor *tx_ring;
static uint8_t *rx_buffers[RX_COUNT];
static uint8_t *tx_buffers[TX_COUNT];
static uint32_t rx_next = 0;   // next receive slot we'll look at
static uint32_t tx_next = 0;   // next transmit slot we'll use
static uint8_t mac[6];
static bool ready = false;

static void write_reg(uint32_t reg, uint32_t value) {
    *(volatile uint32_t *)(registers + reg) = value;
}

static uint32_t read_reg(uint32_t reg) {
    return *(volatile uint32_t *)(registers + reg);
}

// Read one 16-bit word from the card's settings chip.
static uint16_t read_eeprom(uint8_t address) {
    write_reg(REG_EERD, 1u | ((uint32_t)address << 8));
    for (int tries = 0; tries < 100000; tries++) {
        uint32_t value = read_reg(REG_EERD);
        if (value & (1u << 4)) {
            return (uint16_t)(value >> 16);
        }
    }
    return 0;
}

bool e1000_init(void) {
    struct pci_device card;
    if (!pci_find(VENDOR_INTEL, DEVICE_82540EM, &card)) {
        return false;
    }
    pci_enable_io_and_dma(card);
    uint64_t physical = pci_read32(card, 0x10) & ~0xFu;   // BAR0: its memory
    // Map its 128 KB of registers, a page at a time.
    for (uint64_t offset = 0; offset < 0x20000; offset += PAGE_SIZE) {
        volatile void *page = vmm_map_hardware(physical + offset);
        if (offset == 0) registers = page;
    }

    write_reg(REG_IMC, 0xFFFFFFFF);            // no interrupts: we check for frames ourselves
    write_reg(REG_CTRL, read_reg(REG_CTRL) | CTRL_RESET);
    for (volatile int i = 0; i < 100000; i++) { }
    write_reg(REG_IMC, 0xFFFFFFFF);
    write_reg(REG_CTRL, read_reg(REG_CTRL) | CTRL_SET_LINK_UP);

    // The MAC address: from the receive address register (set up by the
    // card), or else from the settings chip.
    uint32_t low = read_reg(REG_RAL), high = read_reg(REG_RAH);
    if (low != 0) {
        for (int i = 0; i < 4; i++) mac[i] = (uint8_t)(low >> (i * 8));
        mac[4] = (uint8_t)high;
        mac[5] = (uint8_t)(high >> 8);
    } else {
        for (int i = 0; i < 3; i++) {
            uint16_t word = read_eeprom((uint8_t)i);
            mac[i * 2] = (uint8_t)word;
            mac[i * 2 + 1] = (uint8_t)(word >> 8);
        }
        write_reg(REG_RAL, mac[0] | (mac[1] << 8) | (mac[2] << 16) | ((uint32_t)mac[3] << 24));
        write_reg(REG_RAH, mac[4] | (mac[5] << 8) | (1u << 31));
    }
    for (int i = 0; i < 128; i++) write_reg(REG_MTA + i * 4, 0);

    // The receive ring.
    rx_ring = pmm_alloc_pages(1);
    tx_ring = pmm_alloc_pages(1);
    if (rx_ring == NULL || tx_ring == NULL) return false;
    memset(rx_ring, 0, PAGE_SIZE);
    memset(tx_ring, 0, PAGE_SIZE);
    for (int i = 0; i < RX_COUNT; i++) {
        rx_buffers[i] = pmm_alloc_pages(1);
        if (rx_buffers[i] == NULL) return false;
        rx_ring[i].address = pmm_physical_address(rx_buffers[i]);
    }
    for (int i = 0; i < TX_COUNT; i++) {
        tx_buffers[i] = pmm_alloc_pages(1);
        if (tx_buffers[i] == NULL) return false;
        tx_ring[i].address = pmm_physical_address(tx_buffers[i]);
        tx_ring[i].status = 1;   // "done": free to use
    }
    uint64_t rx_physical = pmm_physical_address(rx_ring);
    write_reg(REG_RDBAL, (uint32_t)rx_physical);
    write_reg(REG_RDBAH, (uint32_t)(rx_physical >> 32));
    write_reg(REG_RDLEN, RX_COUNT * sizeof(struct rx_descriptor));
    write_reg(REG_RDH, 0);
    write_reg(REG_RDT, RX_COUNT - 1);
    write_reg(REG_RCTL, RCTL_ENABLE | RCTL_BROADCAST | RCTL_STRIP_CRC);

    uint64_t tx_physical = pmm_physical_address(tx_ring);
    write_reg(REG_TDBAL, (uint32_t)tx_physical);
    write_reg(REG_TDBAH, (uint32_t)(tx_physical >> 32));
    write_reg(REG_TDLEN, TX_COUNT * sizeof(struct tx_descriptor));
    write_reg(REG_TDH, 0);
    write_reg(REG_TDT, 0);
    write_reg(REG_TCTL, TCTL_ENABLE | TCTL_PAD_SHORT | (15u << 4) | (64u << 12));
    write_reg(REG_TIPG, 10 | (10 << 10) | (10 << 20));

    ready = true;
    return true;
}

const uint8_t *e1000_mac(void) {
    return mac;
}

bool e1000_send(const void *frame, uint16_t length) {
    if (!ready || length > BUFFER_SIZE) return false;
    struct tx_descriptor *d = &tx_ring[tx_next];
    if (!(d->status & 1)) {
        return false;   // the card hasn't sent what was here before: ring full
    }
    memcpy(tx_buffers[tx_next], frame, length);
    d->length = length;
    d->command = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    d->status = 0;
    tx_next = (tx_next + 1) % TX_COUNT;
    write_reg(REG_TDT, tx_next);
    return true;
}

void e1000_poll(void (*receive)(const uint8_t *frame, uint16_t length)) {
    if (!ready) return;
    for (int handled = 0; handled < RX_COUNT; handled++) {
        struct rx_descriptor *d = &rx_ring[rx_next];
        if (!(d->status & 1)) {
            return;   // nothing more has arrived
        }
        if (d->errors == 0 && d->length > 0) {
            receive(rx_buffers[rx_next], d->length);
        }
        d->status = 0;
        // Give the buffer back to the card.
        write_reg(REG_RDT, rx_next);
        rx_next = (rx_next + 1) % RX_COUNT;
    }
}
