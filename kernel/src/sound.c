// sound.c - the sound card driver (Intel AC'97, which QEMU can pretend to be).
//
// How an AC'97 card plays sound: we give it a list of 32 buffers in memory
// (the "buffer descriptor list"). It reads them one after another by itself
// ("DMA") and plays them, then goes round again. Our job is to keep the
// buffers just ahead of it topped up with sound, or with silence.
//
// The card plays 48,000 stereo samples a second. The speech engine makes
// 16,000 mono samples a second, so each one is turned into three, smoothly.
//
// Sound waits in a "ring" (sound_queue adds to it); the timer tick moves
// it into the card's buffers.

#include "sound.h"
#include "pci.h"
#include "pmm.h"
#include "io.h"
#include "cpu.h"
#include "timer.h"

#define AC97_VENDOR 0x8086
#define AC97_DEVICE 0x2415

// Mixer registers (first block of ports, "NAM").
#define MIXER_RESET          0x00
#define MIXER_MASTER_VOLUME  0x02
#define MIXER_PCM_VOLUME     0x18
// Bus master registers (second block, "NABM"), for PCM out (the speakers).
#define PO_BUFFER_LIST       0x10   // 32-bit: where the buffer list is
#define PO_CURRENT           0x14   // 8-bit: buffer being played now
#define PO_LAST_VALID        0x15   // 8-bit: last buffer we've filled
#define PO_STATUS            0x16   // 16-bit
#define PO_CONTROL           0x1B   // 8-bit
#define GLOBAL_CONTROL       0x2C   // 32-bit
#define CONTROL_RUN          0x01
#define CONTROL_RESET        0x02
#define STATUS_HALTED        0x01

#define BUFFER_COUNT   32
#define BUFFER_FRAMES  768                      // 16 ms of stereo sound at 48 kHz
#define BUFFER_SAMPLES (BUFFER_FRAMES * 2)      // left + right
#define KEEP_AHEAD     3                        // buffers filled ahead of the card (~48 ms)

struct buffer_entry {
    uint32_t address;     // physical address of the buffer
    uint16_t samples;     // how many 16-bit samples it holds
    uint16_t flags;
} __attribute__((packed));

static bool ready = false;
static uint16_t mixer, bus;               // the two port blocks
static struct buffer_entry *list;         // the buffer descriptor list
static int16_t *buffers[BUFFER_COUNT];
static int last_filled;

// Speech waiting to be played: 16 kHz mono.
#define RING_SIZE 32768                   // about 2 seconds
static int16_t ring[RING_SIZE];
static volatile uint32_t ring_read = 0, ring_write = 0;
static int16_t previous_sample = 0;       // for smoothing between samples
static volatile bool flush_requested = false;

bool sound_init(void) {
    struct pci_device card;
    if (!pci_find(AC97_VENDOR, AC97_DEVICE, &card)) {
        return false;
    }
    pci_enable_io_and_dma(card);
    mixer = pci_io_base(card, 0);
    bus = pci_io_base(card, 1);

    // Wake the card up ("cold reset" off), then reset the mixer.
    outl(bus + GLOBAL_CONTROL, 0x00000002);
    timer_sleep_ms(20);
    outw(mixer + MIXER_RESET, 1);
    timer_sleep_ms(5);
    outw(mixer + MIXER_MASTER_VOLUME, 0x0000);   // full volume, not muted
    outw(mixer + MIXER_PCM_VOLUME, 0x0808);      // normal level

    // One page for the list, and one for every 2 buffers (3 KB each).
    list = pmm_alloc_pages(1);
    if (list == NULL) return false;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        buffers[i] = pmm_alloc_pages(1);
        if (buffers[i] == NULL) return false;
        for (int s = 0; s < BUFFER_SAMPLES; s++) buffers[i][s] = 0;
        list[i].address = (uint32_t)pmm_physical_address(buffers[i]);
        list[i].samples = BUFFER_SAMPLES;
        list[i].flags = 0;
    }

    // Reset the PCM-out engine, point it at our list and start it.
    outb(bus + PO_CONTROL, CONTROL_RESET);
    for (int tries = 0; tries < 1000 && (inb(bus + PO_CONTROL) & CONTROL_RESET); tries++) { }
    outl(bus + PO_BUFFER_LIST, (uint32_t)pmm_physical_address(list));
    last_filled = KEEP_AHEAD;
    outb(bus + PO_LAST_VALID, (uint8_t)last_filled);
    outb(bus + PO_CONTROL, CONTROL_RUN);
    ready = true;
    return true;
}

bool sound_available(void) {
    return ready;
}

uint32_t sound_room(void) {
    return RING_SIZE - 1 - (ring_write - ring_read) % RING_SIZE;
}

bool sound_busy(void) {
    return ring_read != ring_write;
}

void sound_queue(const int16_t *samples, uint32_t count) {
    uint32_t w = ring_write;
    for (uint32_t i = 0; i < count; i++) {
        ring[w] = samples[i];
        w = (w + 1) % RING_SIZE;
    }
    ring_write = w;
}

void sound_set_volume(int percent) {
    if (!ready) return;
    if (percent <= 0) {
        outw(mixer + MIXER_MASTER_VOLUME, 0x8000);   // mute
        return;
    }
    if (percent > 100) percent = 100;
    // The card counts "how much quieter" in 1.5 dB steps, 0-31.
    uint16_t quieter = (uint16_t)((100 - percent) * 31 / 100);
    outw(mixer + MIXER_MASTER_VOLUME, (uint16_t)((quieter << 8) | quieter));
}

void sound_flush(void) {
    flush_requested = true;
}

// Fill one card buffer: three output frames for every speech sample,
// blending from the previous sample to the next so it sounds smooth.
static void fill(int16_t *out) {
    uint32_t r = ring_read;
    for (int frame = 0; frame < BUFFER_FRAMES; frame += 3) {
        int32_t next = previous_sample;
        if (r != ring_write) {
            next = ring[r];
            r = (r + 1) % RING_SIZE;
        } else {
            next = previous_sample - previous_sample / 8;   // fade to silence, no click
            if (next > -8 && next < 8) next = 0;
        }
        int32_t a = previous_sample;
        int16_t s1 = (int16_t)((2 * a + next) / 3);
        int16_t s2 = (int16_t)((a + 2 * next) / 3);
        int16_t s3 = (int16_t)next;
        int16_t *o = out + frame * 2;
        o[0] = s1; o[1] = s1;
        o[2] = s2; o[3] = s2;
        o[4] = s3; o[5] = s3;
        previous_sample = (int16_t)next;
    }
    ring_read = r;
}

void sound_tick(void) {
    if (!ready) return;
    if (flush_requested) {
        flush_requested = false;
        ring_read = ring_write;
    }
    int current = inb(bus + PO_CURRENT) & 31;
    // Keep KEEP_AHEAD buffers filled past the one playing now.
    while (((last_filled - current) & 31) < KEEP_AHEAD) {
        last_filled = (last_filled + 1) & 31;
        fill(buffers[last_filled]);
        outb(bus + PO_LAST_VALID, (uint8_t)last_filled);
    }
    // If the card caught up and stopped, start it again.
    if (inw(bus + PO_STATUS) & STATUS_HALTED) {
        outw(bus + PO_STATUS, 0x1C);   // clear its status flags
        outb(bus + PO_CONTROL, CONTROL_RUN);
    }
}
