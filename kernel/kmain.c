#include "game/input.h"
#include "kernel/exceptions.h"
#include "kernel/framebuffer.h"
#include "kernel/gic.h"
#include "kernel/mmu.h"
#include "kernel/timer.h"
#include "kernel/uart.h"

#define CURSOR_SIZE 8
#define CURSOR_STEP 8
#define CURSOR_COLOR 0x00FFFFFFu

// Shared by the initial test pattern and the cursor's erase-by-redraw, so
// the two can't drift out of sync.
static uint32_t gradient_color(int x, int y) {
    uint32_t r = (uint32_t)(x * 255 / (FB_WIDTH - 1));
    uint32_t g = (uint32_t)(y * 255 / (FB_HEIGHT - 1));
    uint32_t b = 0x40;
    return (r << 16) | (g << 8) | b;
}

static void draw_cursor(int x, int y, uint32_t color) {
    for (int dy = 0; dy < CURSOR_SIZE; dy++) {
        for (int dx = 0; dx < CURSOR_SIZE; dx++) {
            framebuffer_set_pixel(x + dx, y + dy, color);
        }
    }
}

// No shadow framebuffer exists yet, so "erase" just recomputes what the
// background gradient would be at these pixels.
static void erase_cursor(int x, int y) {
    for (int dy = 0; dy < CURSOR_SIZE; dy++) {
        for (int dx = 0; dx < CURSOR_SIZE; dx++) {
            framebuffer_set_pixel(x + dx, y + dy, gradient_color(x + dx, y + dy));
        }
    }
}

static void print_dec64(uint64_t v) {
    char buf[20];
    int i = 0;
    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0) {
        buf[i++] = '0' + (char)(v % 10);
        v /= 10;
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

void kmain(void) {
    uart_init();
    uart_puts("SILICON SWARM BOOT OK\n");

    exceptions_init();

    mmu_init();
    uart_puts("MMU + caches enabled\n");

    gic_init();
    timer_init(60);
    irq_set_handler(timer_irq_handler);
    __asm__ volatile("msr daifclr, #2"); // unmask IRQ
    uart_puts("GIC + timer enabled (60Hz)\n");

    int fb_ok = framebuffer_init();
    int cursor_x = FB_WIDTH / 2 - CURSOR_SIZE / 2;
    int cursor_y = FB_HEIGHT / 2 - CURSOR_SIZE / 2;
    if (fb_ok) {
        framebuffer_fill(0x001040A0); // solid fill
        framebuffer_flush();

        uint64_t wait_until = timer_get_ticks() + 60; // ~1s, so the solid
        while (timer_get_ticks() < wait_until) {}       // fill is separately observable

        for (int y = 0; y < FB_HEIGHT; y++) {
            for (int x = 0; x < FB_WIDTH; x++) {
                framebuffer_set_pixel(x, y, gradient_color(x, y));
            }
        }
        framebuffer_flush();
        uart_puts("test pattern drawn\n");

        draw_cursor(cursor_x, cursor_y, CURSOR_COLOR);
        framebuffer_flush();
        uart_puts("Phase 6: WASD to move the cursor\n");
    }

    uint64_t last_reported = 0;
    while (1) {
        uint64_t ticks = timer_get_ticks();
        if (ticks - last_reported >= 60) {
            uart_puts("tick count = ");
            print_dec64(ticks);
            uart_putc('\n');
            last_reported = ticks;
        }

        if (fb_ok) {
            input_action_t action = input_poll();
            if (action != INPUT_NONE) {
                int nx = cursor_x, ny = cursor_y;
                switch (action) {
                case INPUT_UP:
                    ny -= CURSOR_STEP;
                    break;
                case INPUT_DOWN:
                    ny += CURSOR_STEP;
                    break;
                case INPUT_LEFT:
                    nx -= CURSOR_STEP;
                    break;
                case INPUT_RIGHT:
                    nx += CURSOR_STEP;
                    break;
                default:
                    break;
                }
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx > FB_WIDTH - CURSOR_SIZE) nx = FB_WIDTH - CURSOR_SIZE;
                if (ny > FB_HEIGHT - CURSOR_SIZE) ny = FB_HEIGHT - CURSOR_SIZE;
                if (nx != cursor_x || ny != cursor_y) {
                    erase_cursor(cursor_x, cursor_y);
                    cursor_x = nx;
                    cursor_y = ny;
                    draw_cursor(cursor_x, cursor_y, CURSOR_COLOR);
                    framebuffer_flush();
                    uart_puts("cursor -> (");
                    print_dec64((uint64_t)cursor_x);
                    uart_puts(", ");
                    print_dec64((uint64_t)cursor_y);
                    uart_puts(")\n");
                }
            }
        }
    }
}
