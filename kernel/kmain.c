#include "kernel/exceptions.h"
#include "kernel/mmu.h"
#include "kernel/uart.h"

void kmain(void) {
    uart_init();
    uart_puts("SILICON SWARM BOOT OK\n");

    exceptions_init();

    mmu_init();
    uart_puts("MMU + caches enabled\n");

    while (1) {
        // Phase 1: nothing else runs yet. Phase 4 replaces this with a real
        // timer-driven main loop.
    }
}
