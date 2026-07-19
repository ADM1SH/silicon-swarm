#include "kernel/exceptions.h"
#include "kernel/framebuffer.h"
#include "kernel/gic.h"
#include "kernel/mmu.h"
#include "kernel/timer.h"
#include "kernel/uart.h"

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

    if (framebuffer_init()) {
        framebuffer_fill(0x001040A0); // solid fill
        framebuffer_flush();

        uint64_t wait_until = timer_get_ticks() + 60; // ~1s, so the solid
        while (timer_get_ticks() < wait_until) {}       // fill is separately observable

        for (int y = 0; y < FB_HEIGHT; y++) {
            for (int x = 0; x < FB_WIDTH; x++) {
                uint32_t r = (uint32_t)(x * 255 / (FB_WIDTH - 1));
                uint32_t g = (uint32_t)(y * 255 / (FB_HEIGHT - 1));
                uint32_t b = 0x40;
                framebuffer_set_pixel(x, y, (r << 16) | (g << 8) | b);
            }
        }
        framebuffer_flush();
        uart_puts("test pattern drawn\n");
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
    }
}
