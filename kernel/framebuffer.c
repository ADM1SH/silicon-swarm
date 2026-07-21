// ramfb: QEMU's minimal framebuffer, negotiated entirely through fw_cfg —
// no PCI/virtio device, no GPU driver. Base 0x09020000 (README §5): data
// register +0x00 (byte stream), 16-bit selector +0x08, 64-bit DMA address
// +0x10. The selector and DMA registers are BIG-ENDIAN regardless of CPU
// endianness (documented QEMU fw_cfg behavior on ARM) — every multi-byte
// field in the structs below has to be byte-swapped explicitly.
//
// Sequence: find "etc/ramfb"'s selector key in the fw_cfg file directory
// (selector 0x19), then use the fw_cfg DMA "write" operation to copy a
// RAMFBCfg struct (address/format/dimensions of our framebuffer) into that
// file, which is QEMU's signal to start scanning our buffer out to the
// display.
#include "engine/blit_neon.h"
#include "kernel/framebuffer.h"
#include "kernel/uart.h"

#define FW_CFG_BASE 0x09020000UL
#define FW_CFG_DATA (*(volatile uint8_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_SELECT (*(volatile uint16_t *)(FW_CFG_BASE + 0x08))
#define FW_CFG_DMA (*(volatile uint64_t *)(FW_CFG_BASE + 0x10))

#define FW_CFG_FILE_DIR 0x19

#define FW_CFG_DMA_CTL_ERROR (1u << 0)
#define FW_CFG_DMA_CTL_SELECT (1u << 3)
#define FW_CFG_DMA_CTL_WRITE (1u << 4)

struct fw_cfg_file {
    uint32_t size;   // big-endian
    uint16_t select; // big-endian
    uint16_t reserved;
    char name[56];
} __attribute__((packed));

struct fw_cfg_dma_access {
    uint32_t control; // big-endian
    uint32_t length;  // big-endian
    uint64_t address; // big-endian
} __attribute__((packed));

struct ramfb_cfg {
    uint64_t addr;   // big-endian, guest physical address of the framebuffer
    uint32_t fourcc; // big-endian
    uint32_t flags;  // big-endian, unused, must be 0
    uint32_t width;  // big-endian
    uint32_t height; // big-endian
    uint32_t stride; // big-endian, bytes per row
} __attribute__((packed));

#define DRM_FORMAT_XRGB8888 0x34325258u // fourcc('X','R','2','4')

static uint32_t g_framebuffer[2][FB_WIDTH * FB_HEIGHT] __attribute__((aligned(64)));
static int g_back; // buffer being drawn into; the other one is on screen
static uint16_t g_ramfb_select;
static struct ramfb_cfg g_ramfb_cfg __attribute__((aligned(64)));
static struct fw_cfg_dma_access g_dma __attribute__((aligned(64)));

static uint64_t dcache_line_size(void) {
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    return 4ull << ((ctr >> 16) & 0xf); // CTR_EL0.DminLine
}

// Write-back: makes our writes visible to QEMU's device-emulation code,
// which reads guest RAM directly and doesn't snoop the CPU's cache.
static void dcache_clean_range(const void *start, uint64_t size) {
    uint64_t line = dcache_line_size();
    uint64_t addr = (uint64_t)start & ~(line - 1);
    uint64_t end = (uint64_t)start + size;
    for (; addr < end; addr += line) {
        __asm__ volatile("dc cvac, %0" ::"r"(addr));
    }
    __asm__ volatile("dsb sy");
}

// Invalidate: makes a subsequent read see what QEMU wrote directly to RAM
// (e.g. the DMA status it writes back into g_dma.control), rather than a
// stale cached copy of what we wrote there ourselves.
static void dcache_invalidate_range(const void *start, uint64_t size) {
    uint64_t line = dcache_line_size();
    uint64_t addr = (uint64_t)start & ~(line - 1);
    uint64_t end = (uint64_t)start + size;
    for (; addr < end; addr += line) {
        __asm__ volatile("dc ivac, %0" ::"r"(addr));
    }
    __asm__ volatile("dsb sy");
}

static void fw_cfg_read_bytes(void *dst, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < len; i++) {
        d[i] = FW_CFG_DATA;
    }
}

// Returns "etc/ramfb"'s fw_cfg selector key, or 0 if not found (0 is never
// a valid file selector — the legacy fixed selectors start at 1).
static uint16_t fw_cfg_find_ramfb_selector(void) {
    FW_CFG_SELECT = __builtin_bswap16(FW_CFG_FILE_DIR);
    __asm__ volatile("dsb sy");

    uint32_t count_be;
    fw_cfg_read_bytes(&count_be, 4);
    uint32_t count = __builtin_bswap32(count_be);

    for (uint32_t i = 0; i < count; i++) {
        struct fw_cfg_file f;
        fw_cfg_read_bytes(&f, sizeof(f));
        int match = 1;
        const char *want = "etc/ramfb";
        for (int j = 0; want[j]; j++) {
            if (f.name[j] != want[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            return __builtin_bswap16(f.select);
        }
    }
    return 0;
}

// (Re)points ramfb's scanout at `buf` via a fw_cfg DMA write. Returns 1 on
// success. Cheap enough to do per frame — this IS the page flip.
static int ramfb_set_scanout(const uint32_t *buf) {
    g_ramfb_cfg.addr = __builtin_bswap64((uint64_t)(uintptr_t)buf);
    g_ramfb_cfg.fourcc = __builtin_bswap32(DRM_FORMAT_XRGB8888);
    g_ramfb_cfg.flags = 0;
    g_ramfb_cfg.width = __builtin_bswap32((uint32_t)FB_WIDTH);
    g_ramfb_cfg.height = __builtin_bswap32((uint32_t)FB_HEIGHT);
    g_ramfb_cfg.stride = __builtin_bswap32((uint32_t)(FB_WIDTH * 4));
    dcache_clean_range(&g_ramfb_cfg, sizeof(g_ramfb_cfg));

    g_dma.control = __builtin_bswap32(((uint32_t)g_ramfb_select << 16) |
                                       FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE);
    g_dma.length = __builtin_bswap32((uint32_t)sizeof(g_ramfb_cfg));
    g_dma.address = __builtin_bswap64((uint64_t)(uintptr_t)&g_ramfb_cfg);
    dcache_clean_range(&g_dma, sizeof(g_dma));

    FW_CFG_DMA = __builtin_bswap64((uint64_t)(uintptr_t)&g_dma);
    __asm__ volatile("dsb sy");

    // QEMU writes its result directly back into g_dma.control (clearing
    // handled bits, setting ERROR on failure) — invalidate before reading
    // so we see that write, not our own stale cached copy.
    dcache_invalidate_range(&g_dma, sizeof(g_dma));
    return !(__builtin_bswap32(g_dma.control) & FW_CFG_DMA_CTL_ERROR);
}

int framebuffer_init(void) {
    g_ramfb_select = fw_cfg_find_ramfb_selector();
    if (g_ramfb_select == 0) {
        uart_puts("NOT YET IMPLEMENTED: ramfb not present in fw_cfg\n");
        return 0;
    }
    g_back = 1; // buffer 0 goes on screen first, draw into 1
    if (!ramfb_set_scanout(g_framebuffer[0])) {
        uart_puts("ramfb: fw_cfg DMA write reported an error\n");
        return 0;
    }
    uart_puts("ramfb negotiated (double-buffered)\n");
    return 1;
}

uint32_t *framebuffer_pixels(void) {
    return g_framebuffer[g_back];
}

void framebuffer_set_pixel(int x, int y, uint32_t color) {
    if ((unsigned)x >= FB_WIDTH || (unsigned)y >= FB_HEIGHT) {
        return;
    }
    g_framebuffer[g_back][y * FB_WIDTH + x] = color;
}

void framebuffer_fill(uint32_t color) {
    neon_fill32(g_framebuffer[g_back], color, (uint32_t)(FB_WIDTH * FB_HEIGHT));
}

void framebuffer_flush(void) {
    dcache_clean_range(g_framebuffer[g_back], sizeof(g_framebuffer[0]));
    ramfb_set_scanout(g_framebuffer[g_back]);
    g_back ^= 1;
}
