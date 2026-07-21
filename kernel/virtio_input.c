// virtio-mmio modern (version 2) input driver, split virtqueue, polled.
// QEMU `virt` exposes 32 virtio-mmio transports at 0x0a000000 + 0x200*i
// (README §5); virtio-input is device ID 18. One queue (eventq #0) of
// 8-byte input_event buffers that the DEVICE writes; we recycle each
// buffer back onto the avail ring after consuming it.
#include "kernel/virtio_input.h"
#include "kernel/uart.h"

#define VIRTIO_MMIO_BASE 0x0a000000UL
#define VIRTIO_MMIO_STRIDE 0x200
#define VIRTIO_MMIO_SLOTS 32

#define REG(off) (*(volatile uint32_t *)(g_base + (off)))
#define R_MAGIC 0x00
#define R_VERSION 0x04
#define R_DEVICE_ID 0x08
#define R_DEV_FEAT 0x10
#define R_DEV_FEAT_SEL 0x14
#define R_DRV_FEAT 0x20
#define R_DRV_FEAT_SEL 0x24
#define R_QUEUE_SEL 0x30
#define R_QUEUE_NUM_MAX 0x34
#define R_QUEUE_NUM 0x38
#define R_QUEUE_READY 0x44
#define R_QUEUE_NOTIFY 0x50
#define R_STATUS 0x70
#define R_QUEUE_DESC_LO 0x80
#define R_QUEUE_DESC_HI 0x84
#define R_QUEUE_DRV_LO 0x90
#define R_QUEUE_DRV_HI 0x94
#define R_QUEUE_DEV_LO 0xa0
#define R_QUEUE_DEV_HI 0xa4

#define ST_ACK 1
#define ST_DRIVER 2
#define ST_DRIVER_OK 4
#define ST_FEATURES_OK 8

#define QSIZE 64
#define DESC_F_WRITE 2

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QSIZE];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[QSIZE];
} __attribute__((packed));

struct input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

#define EV_KEY 1

static uint64_t g_base;
static struct virtq_desc g_desc[QSIZE] __attribute__((aligned(64)));
static struct virtq_avail g_avail __attribute__((aligned(64)));
static struct virtq_used g_used __attribute__((aligned(64)));
static struct input_event g_events[QSIZE] __attribute__((aligned(64)));
static uint16_t g_last_used;
static int g_ready;

// Same cache-maintenance story as kernel/framebuffer.c's ramfb path: QEMU's
// device emulation reads/writes guest RAM directly, so publish our writes
// (clean) and discard stale lines before reading the device's (invalidate).
static uint64_t dline(void) {
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    return 4ull << ((ctr >> 16) & 0xf);
}
static void clean(const void *p, uint64_t n) {
    uint64_t l = dline(), a = (uint64_t)p & ~(l - 1), e = (uint64_t)p + n;
    for (; a < e; a += l) __asm__ volatile("dc cvac, %0" ::"r"(a));
    __asm__ volatile("dsb sy");
}
static void inval(const void *p, uint64_t n) {
    uint64_t l = dline(), a = (uint64_t)p & ~(l - 1), e = (uint64_t)p + n;
    for (; a < e; a += l) __asm__ volatile("dc civac, %0" ::"r"(a));
    __asm__ volatile("dsb sy");
}

static void queue_push_avail(uint16_t desc_id) {
    g_avail.ring[g_avail.idx % QSIZE] = desc_id;
    __asm__ volatile("dsb sy");
    g_avail.idx++;
    clean(&g_avail, sizeof(g_avail));
    REG(R_QUEUE_NOTIFY) = 0;
}

int virtio_input_init(void) {
    for (int i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        g_base = VIRTIO_MMIO_BASE + (uint64_t)i * VIRTIO_MMIO_STRIDE;
        if (REG(R_MAGIC) == 0x74726976 && REG(R_VERSION) == 2 &&
            REG(R_DEVICE_ID) == 18) {
            break;
        }
        g_base = 0;
    }
    if (!g_base) {
        uart_puts("virtio-input: no device (serial input only)\n");
        return 0;
    }

    REG(R_STATUS) = 0; // reset
    REG(R_STATUS) = ST_ACK;
    REG(R_STATUS) = ST_ACK | ST_DRIVER;

    // Accept only VIRTIO_F_VERSION_1 (feature bit 32).
    REG(R_DRV_FEAT_SEL) = 1;
    REG(R_DRV_FEAT) = 1;
    REG(R_DRV_FEAT_SEL) = 0;
    REG(R_DRV_FEAT) = 0;
    REG(R_STATUS) = ST_ACK | ST_DRIVER | ST_FEATURES_OK;
    if (!(REG(R_STATUS) & ST_FEATURES_OK)) {
        uart_puts("virtio-input: features rejected\n");
        return 0;
    }

    REG(R_QUEUE_SEL) = 0;
    if (REG(R_QUEUE_NUM_MAX) < QSIZE) {
        uart_puts("virtio-input: eventq too small\n");
        return 0;
    }
    REG(R_QUEUE_NUM) = QSIZE;

    for (int i = 0; i < QSIZE; i++) {
        g_desc[i].addr = (uint64_t)(uintptr_t)&g_events[i];
        g_desc[i].len = sizeof(struct input_event);
        g_desc[i].flags = DESC_F_WRITE;
        g_desc[i].next = 0;
    }
    g_avail.flags = 0;
    g_avail.idx = 0;
    g_used.flags = 0;
    g_used.idx = 0;
    clean(g_desc, sizeof(g_desc));
    clean(&g_used, sizeof(g_used));
    clean(g_events, sizeof(g_events));

    REG(R_QUEUE_DESC_LO) = (uint32_t)(uintptr_t)g_desc;
    REG(R_QUEUE_DESC_HI) = (uint32_t)((uint64_t)(uintptr_t)g_desc >> 32);
    REG(R_QUEUE_DRV_LO) = (uint32_t)(uintptr_t)&g_avail;
    REG(R_QUEUE_DRV_HI) = (uint32_t)((uint64_t)(uintptr_t)&g_avail >> 32);
    REG(R_QUEUE_DEV_LO) = (uint32_t)(uintptr_t)&g_used;
    REG(R_QUEUE_DEV_HI) = (uint32_t)((uint64_t)(uintptr_t)&g_used >> 32);
    REG(R_QUEUE_READY) = 1;
    REG(R_STATUS) = ST_ACK | ST_DRIVER | ST_FEATURES_OK | ST_DRIVER_OK;

    for (uint16_t i = 0; i < QSIZE; i++) {
        queue_push_avail(i);
    }
    g_last_used = 0;
    g_ready = 1;
    uart_puts("virtio-input: keyboard ready\n");
    return 1;
}

static int g_shift; // live modifier state from press/release events

// Linux input keycode -> the game's ASCII alphabet. Shift+WASD emits the
// capital, which the input layer maps to fast cursor moves.
static uint8_t map_key(uint16_t code) {
    switch (code) {
    case 17: return g_shift ? 'W' : 'w';
    case 30: return g_shift ? 'A' : 'a';
    case 31: return g_shift ? 'S' : 's';
    case 32: return g_shift ? 'D' : 'd';
    case 33: return 'f';
    case 16: return 'q';
    case 18: return 'e';
    case 19: return 'r';
    case 2: return '1';
    case 3: return '2';
    case 4: return '3';
    case 5: return '4';
    case 6: return '5';
    case 7: return '6';
    case 8: return '7';
    case 9: return '8';
    case 10: return '9';
    case 11: return '0';
    case 20: return 't';
    case 12: return '-';
    case 13: return '=';
    case 45: return 'x';
    case 57: return ' ';
    case 28: return '\r';
    default: return 0;
    }
}

int virtio_input_poll_char(uint8_t *out) {
    if (!g_ready) {
        return 0;
    }
    for (;;) {
        inval(&g_used, sizeof(g_used));
        if (g_used.idx == g_last_used) {
            return 0;
        }
        struct virtq_used_elem e = g_used.ring[g_last_used % QSIZE];
        g_last_used++;
        uint16_t id = (uint16_t)e.id;
        inval(&g_events[id], sizeof(struct input_event));
        struct input_event ev = g_events[id];
        queue_push_avail(id); // recycle the buffer either way
        if (ev.type == EV_KEY && (ev.code == 42 || ev.code == 54)) {
            g_shift = (ev.value != 0); // track shift press AND release
            continue;
        }
        if (ev.type == EV_KEY && (ev.value == 1 || ev.value == 2)) {
            uint8_t c = map_key(ev.code);
            if (c) {
                *out = c;
                return 1;
            }
        }
        // non-key or key-release event: keep draining
    }
}
