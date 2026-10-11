// mouse.c - AREOS mouse driver (PS/2).
//
// The mouse plugs into the same controller chip as the keyboard, on its
// second ("auxiliary") port. Setting it up means a short conversation
// with that chip: switch the port on, allow its interrupt, then tell the
// mouse itself to start sending movement reports.

#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "graphics.h"

#define DATA_PORT        0x60
#define STATUS_PORT      0x64   // reading
#define COMMAND_PORT     0x64   // writing

#define STATUS_OUTPUT_FULL  0x01  // a byte is waiting for us
#define STATUS_INPUT_FULL   0x02  // the chip is still busy with our last byte

#define CMD_ENABLE_AUX      0xA8  // switch the mouse port on
#define CMD_READ_CONFIG     0x20
#define CMD_WRITE_CONFIG    0x60
#define CMD_SEND_TO_MOUSE   0xD4  // "the next byte is for the mouse"
#define CONFIG_AUX_IRQ      0x02  // config bit: mouse interrupts on
#define CONFIG_AUX_DISABLED 0x20  // config bit: mouse clock off

#define MOUSE_SET_DEFAULTS  0xF6
#define MOUSE_ENABLE        0xF4  // start sending movement reports
#define MOUSE_GET_ID        0xF2  // "what kind of mouse are you?"
#define MOUSE_ID_WHEEL      3     // a scroll-wheel mouse: reports are 4 bytes
#define MOUSE_ID_5_BUTTONS  4     // also 4 bytes

#define IRQ_MOUSE 12

// First byte of each report: buttons in the low 3 bits, bit 3 always set,
// bits 4/5 say whether the x/y movement is negative.
#define PACKET_ALWAYS_ONE  0x08
#define PACKET_X_NEGATIVE  0x10
#define PACKET_Y_NEGATIVE  0x20
#define PACKET_OVERFLOW    0xC0

#define WAIT_LIMIT 100000

static void wait_to_write(void) {
    for (int i = 0; i < WAIT_LIMIT && (inb(STATUS_PORT) & STATUS_INPUT_FULL); i++) { }
}

static void wait_to_read(void) {
    for (int i = 0; i < WAIT_LIMIT && !(inb(STATUS_PORT) & STATUS_OUTPUT_FULL); i++) { }
}

static void send_command(uint8_t command) {
    wait_to_write();
    outb(COMMAND_PORT, command);
}

static uint8_t read_from_mouse(void) {
    wait_to_read();
    return inb(DATA_PORT);
}

static void send_to_mouse(uint8_t byte) {
    send_command(CMD_SEND_TO_MOUSE);
    wait_to_write();
    outb(DATA_PORT, byte);
    read_from_mouse();  // the mouse answers 0xFA ("got it")
}

// Finished movement reports, waiting to be collected.
#define QUEUE_SIZE 64
static struct mouse_event queue[QUEUE_SIZE];
static volatile uint32_t queue_head = 0;
static volatile uint32_t queue_tail = 0;

// The report being assembled, one byte per interrupt. Basic mice send 3
// bytes per report; scroll-wheel mice send 4 (the 4th is the wheel).
static uint8_t packet[4];
static int packet_bytes = 0;
static int packet_size = 3;

static void queue_event(struct mouse_event event) {
    uint32_t next = (queue_tail + 1) % QUEUE_SIZE;
    if (next == queue_head) {
        return;  // queue full: drop it
    }
    queue[queue_tail] = event;
    queue_tail = next;
}

// ---- The "VMware mouse": exactly where the pointer is ----
//
// A normal PS/2 mouse only says "moved 3 left, 2 up". In an emulator that
// makes AREOS's pointer drift away from your real one, because the two
// move at different speeds. QEMU (and VMware) also offer a back door that
// says exactly where your pointer is, so AREOS's pointer can sit right
// under it. It's reached through a special I/O port with a magic number.
// On a real PC nobody answers, and we stay with the PS/2 mouse.

#define VM_PORT              0x5658
#define VM_MAGIC             0x564D5868u
#define VM_GET_VERSION       10
#define VM_POINTER_DATA      39
#define VM_POINTER_STATUS    40
#define VM_POINTER_COMMAND   41
#define VM_CMD_ENABLE        0x45414552u
#define VM_CMD_ABSOLUTE      0x53424152u
#define VM_VERSION_ID        0x3442554Au
#define VM_STATUS_ERROR      0xFFFF0000u
#define VM_LEFT              0x20
#define VM_RIGHT             0x10
#define VM_MIDDLE            0x08
#define VM_RANGE             0xFFFF   // positions come as 0..65535 across the screen

static bool absolute_mode = false;

static void vm_call(uint32_t command, uint32_t argument, uint32_t out[4]) {
    uint32_t a, b, c, d;
    asm volatile ("inl %%dx, %%eax"
                  : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                  : "a"(VM_MAGIC), "b"(argument), "c"(command), "d"(VM_PORT)
                  : "memory");
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
}

static bool absolute_start(void) {
    uint32_t r[4];
    vm_call(VM_GET_VERSION, ~VM_MAGIC, r);
    if (r[1] != VM_MAGIC) {
        return false;   // no back door here (a real PC)
    }
    vm_call(VM_POINTER_COMMAND, VM_CMD_ENABLE, r);
    vm_call(VM_POINTER_STATUS, 0, r);
    if ((r[0] & 0xFFFF) == 0 || (r[0] & VM_STATUS_ERROR) == VM_STATUS_ERROR) {
        return false;
    }
    vm_call(VM_POINTER_DATA, 1, r);
    if (r[0] != VM_VERSION_ID) {
        return false;
    }
    vm_call(VM_POINTER_COMMAND, VM_CMD_ABSOLUTE, r);
    return true;
}

// Collect the positions waiting in the back door.
static void absolute_read(void) {
    for (int guard = 0; guard < 64; guard++) {
        uint32_t r[4];
        vm_call(VM_POINTER_STATUS, 0, r);
        uint32_t status = r[0];
        if ((status & VM_STATUS_ERROR) == VM_STATUS_ERROR) {
            // It got confused (or overflowed): switch it off and on again.
            absolute_mode = absolute_start();
            return;
        }
        uint32_t waiting = status & 0xFFFF;
        if (waiting < 4) {
            return;
        }
        vm_call(VM_POINTER_DATA, 4, r);
        struct mouse_event event = { .absolute = true };
        if (r[0] & VM_LEFT) event.buttons |= MOUSE_BUTTON_LEFT;
        if (r[0] & VM_RIGHT) event.buttons |= MOUSE_BUTTON_RIGHT;
        if (r[0] & VM_MIDDLE) event.buttons |= MOUSE_BUTTON_MIDDLE;
        uint32_t width = graphics_screen_width(), height = graphics_screen_height();
        uint32_t x = (r[1] & 0xFFFF) * width / (VM_RANGE + 1);
        uint32_t y = (r[2] & 0xFFFF) * height / (VM_RANGE + 1);
        event.x = (uint16_t)x;
        event.y = (uint16_t)y;
        queue_event(event);
    }
}

static void on_mouse_interrupt(struct interrupt_frame *frame) {
    (void)frame;
    uint8_t byte = inb(DATA_PORT);
    if (absolute_mode) {
        // The PS/2 bytes are just a doorbell now: the real news is in the back door.
        absolute_read();
        return;
    }

    // The first byte always has bit 3 set. If not, we're out of step;
    // drop bytes until we line up again.
    if (packet_bytes == 0 && !(byte & PACKET_ALWAYS_ONE)) {
        return;
    }
    packet[packet_bytes++] = byte;
    if (packet_bytes < packet_size) {
        return;
    }
    packet_bytes = 0;

    if (packet[0] & PACKET_OVERFLOW) {
        return;  // moved too fast to measure; ignore this report
    }
    int16_t dx = (int16_t)packet[1] - ((packet[0] & PACKET_X_NEGATIVE) ? 256 : 0);
    int16_t dy = (int16_t)packet[2] - ((packet[0] & PACKET_Y_NEGATIVE) ? 256 : 0);

    struct mouse_event event = {
        .dx = dx,
        .dy = (int16_t)-dy,          // the mouse counts up as positive; screens count down
        .buttons = packet[0] & 0x07,
    };
    queue_event(event);
}

void mouse_init(void) {
    send_command(CMD_ENABLE_AUX);

    // Turn on the mouse's interrupt in the controller's settings.
    send_command(CMD_READ_CONFIG);
    wait_to_read();
    uint8_t config = inb(DATA_PORT);
    config |= CONFIG_AUX_IRQ;
    config &= (uint8_t)~CONFIG_AUX_DISABLED;
    send_command(CMD_WRITE_CONFIG);
    wait_to_write();
    outb(DATA_PORT, config);

    send_to_mouse(MOUSE_SET_DEFAULTS);

    // The BIOS may have switched on scroll-wheel mode, which makes each
    // report 4 bytes instead of 3. Ask the mouse which it's doing.
    send_to_mouse(MOUSE_GET_ID);
    uint8_t id = read_from_mouse();
    packet_size = (id == MOUSE_ID_WHEEL || id == MOUSE_ID_5_BUTTONS) ? 4 : 3;

    send_to_mouse(MOUSE_ENABLE);

    // In an emulator, follow your real pointer exactly if we can.
    absolute_mode = absolute_start();

    irq_set_handler(IRQ_MOUSE, on_mouse_interrupt);
}

bool mouse_try_get_event(struct mouse_event *out) {
    if (queue_head == queue_tail) {
        return false;
    }
    *out = queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_SIZE;
    return true;
}
