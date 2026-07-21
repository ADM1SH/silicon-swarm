#ifndef KERNEL_FRAMEBUFFER_H
#define KERNEL_FRAMEBUFFER_H

#include <stdint.h>

#define FB_WIDTH 1280
#define FB_HEIGHT 720

// Negotiates ramfb through fw_cfg DMA. Returns 1 on success, 0 if "etc/ramfb"
// isn't present in the fw_cfg file directory (prints a diagnostic either way).
int framebuffer_init(void);

// Raw pixel array (FB_WIDTH * FB_HEIGHT, row-major) — for renderers that
// fill whole spans via neon_fill32 instead of per-pixel calls.
uint32_t *framebuffer_pixels(void);

// x,y in pixels; color is 0x00RRGGBB.
void framebuffer_set_pixel(int x, int y, uint32_t color);
void framebuffer_fill(uint32_t color);

// Cleans (write-back) the framebuffer's dirty cache lines to RAM so QEMU's
// display code — which reads guest RAM directly, not through the CPU's
// cache — sees what was actually drawn. Call after a batch of drawing.
void framebuffer_flush(void);

#endif
