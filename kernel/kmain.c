#include "kernel/exceptions.h"
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
