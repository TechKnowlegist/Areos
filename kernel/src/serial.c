// serial.c - the serial port (COM1).

#include <stdint.h>
#include "serial.h"
#include "io.h"

#define COM1                 0x3F8
#define REG_DATA             (COM1 + 0)
#define REG_INTERRUPTS       (COM1 + 1)
#define REG_FIFO             (COM1 + 2)
#define REG_LINE_CONTROL     (COM1 + 3)
#define REG_MODEM_CONTROL    (COM1 + 4)
#define REG_LINE_STATUS      (COM1 + 5)

#define STATUS_DATA_READY    0x01  // a character has arrived
#define STATUS_SEND_READY    0x20  // ready to send another character

static bool present = false;

void serial_init(void) {
    outb(REG_INTERRUPTS, 0x00);     // no serial interrupts; we check it ourselves
    outb(REG_LINE_CONTROL, 0x80);   // next two writes set the speed...
    outb(REG_DATA, 0x01);           // ...115200 bits per second (divisor 1)
    outb(REG_INTERRUPTS, 0x00);
    outb(REG_LINE_CONTROL, 0x03);   // 8 bits, no parity, 1 stop bit
    outb(REG_FIFO, 0xC7);           // turn on the send/receive buffers
    outb(REG_MODEM_CONTROL, 0x03);  // "ready to talk"

    // If nothing is attached, the status reads as all ones. Skip the port then.
    present = (inb(REG_LINE_STATUS) != 0xFF);
}

void serial_putc(char c) {
    if (!present) {
        return;
    }
    while ((inb(REG_LINE_STATUS) & STATUS_SEND_READY) == 0) {
        // wait for room
    }
    outb(REG_DATA, (uint8_t)c);
}

bool serial_try_getc(char *out) {
    if (!present || (inb(REG_LINE_STATUS) & STATUS_DATA_READY) == 0) {
        return false;
    }
    *out = (char)inb(REG_DATA);
    return true;
}
