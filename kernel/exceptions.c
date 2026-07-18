#include "kernel/exceptions.h"
#include "kernel/uart.h"

extern uint8_t vectors[]; // defined in boot/vectors.S

#define EXC_TYPE_IRQ_SPX 5

static irq_handler_fn g_irq_handler = 0;

static const char *exc_name(uint64_t type) {
    static const char *names[16] = {
        "Synchronous (SP0)",  "IRQ (SP0)",  "FIQ (SP0)",  "SError (SP0)",
        "Synchronous (SPx)",  "IRQ (SPx)",  "FIQ (SPx)",  "SError (SPx)",
        "Synchronous (Lower64)", "IRQ (Lower64)", "FIQ (Lower64)", "SError (Lower64)",
        "Synchronous (Lower32)", "IRQ (Lower32)", "FIQ (Lower32)", "SError (Lower32)",
    };
    return (type < 16) ? names[type] : "UNKNOWN";
}

static void print_hex64(uint64_t v) {
    static const char digits[] = "0123456789abcdef";
    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(v >> shift) & 0xf]);
    }
}

void exceptions_init(void) {
    __asm__ volatile("msr vbar_el1, %0" ::"r"(vectors));
    __asm__ volatile("isb");
}

void irq_set_handler(irq_handler_fn handler) {
    g_irq_handler = handler;
}

void exc_dispatch(uint64_t type, uint64_t esr, struct trap_frame *ctx) {
    if (type == EXC_TYPE_IRQ_SPX && g_irq_handler) {
        g_irq_handler();
        return;
    }

    uint64_t far;
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));

    uart_puts("\n*** UNHANDLED EXCEPTION: ");
    uart_puts(exc_name(type));
    uart_puts(" ***\n");
    uart_puts("ESR_EL1  = "); print_hex64(esr); uart_putc('\n');
    uart_puts("ELR_EL1  = "); print_hex64(ctx->elr); uart_putc('\n');
    uart_puts("FAR_EL1  = "); print_hex64(far); uart_putc('\n');
    uart_puts("SPSR_EL1 = "); print_hex64(ctx->spsr); uart_putc('\n');
    uart_puts("HALTED.\n");

    __asm__ volatile("msr daifset, #0xf"); // mask all further exceptions
    for (;;) {
        __asm__ volatile("wfe");
    }
}
