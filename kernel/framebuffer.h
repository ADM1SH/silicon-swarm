#ifndef KERNEL_FRAMEBUFFER_H
#define KERNEL_FRAMEBUFFER_H

#include <stdint.h>

#define FB_WIDTH 1920
#define FB_HEIGHT 1080

// Negotiates ramfb through fw_cfg DMA. Returns 1 on success, 0 if "etc/ramfb"
// isn't present in the fw_cfg file directory (prints a diagnostic either way).
int framebuffer_init(void);

// Raw pixel array of the BACK buffer (FB_WIDTH * FB_HEIGHT, row-major) —
// all drawing goes here; framebuffer_flush() flips it to the display.
uint32_t *framebuffer_pixels(void);

// x,y in pixels; color is 0x00RRGGBB.
void framebuffer_set_pixel(int x, int y, uint32_t color);
void framebuffer_fill(uint32_t color);

// Page flip: cleans the back buffer's cache lines to RAM, re-negotiates
// ramfb to scan out the back buffer, and swaps buffers. Double buffering —
// the display never samples a half-drawn frame (the v2 single-buffer
// "clear then redraw in place" was visible as flicker).
void framebuffer_flush(void);

#endif
