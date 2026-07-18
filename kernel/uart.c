// PL011 UART driver — polling only (no IRQ) for v1. Base address and layout
// per the QEMU `virt` machine's PL011 instance (README §5): 0x09000000.
#include "kernel/uart.h"

#define UART0_BASE 0x09000000UL

#define UART_DR   (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR   (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART_IBRD (*(volatile uint32_t *)(UART0_BASE + 0x24))
#define UART_FBRD (*(volatile uint32_t *)(UART0_BASE + 0x28))
#define UART_LCRH (*(volatile uint32_t *)(UART0_BASE + 0x2c))
#define UART_CR   (*(volatile uint32_t *)(UART0_BASE + 0x30))
#define UART_ICR  (*(volatile uint32_t *)(UART0_BASE + 0x44))

#define UART_FR_TXFF (1u << 5) // TX FIFO full
#define UART_FR_RXFE (1u << 4) // RX FIFO empty

#define UART_CR_UARTEN (1u << 0)
#define UART_CR_TXE    (1u << 8)
#define UART_CR_RXE    (1u << 9)

#define UART_LCRH_FEN   (1u << 4) // enable FIFOs
#define UART_LCRH_WLEN8 (3u << 5) // 8 data bits

void uart_init(void) {
    UART_CR = 0; // disable while reconfiguring

    UART_ICR = 0x7ff; // clear any pending interrupt status

    // 115200 8N1, FIFOs on. QEMU's PL011 model doesn't actually gate on the
    // baud divisor, but setting a real value keeps this correct if ever run
    // against real PL011 hardware.
    UART_IBRD = 13;
    UART_FBRD = 1;
    UART_LCRH = UART_LCRH_WLEN8 | UART_LCRH_FEN;

    UART_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
}

void uart_putc(char c) {
    while (UART_FR & UART_FR_TXFF) {
    }
    UART_DR = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

int uart_getc_nonblock(uint8_t *out) {
    if (UART_FR & UART_FR_RXFE) {
        return 0;
    }
    *out = (uint8_t)(UART_DR & 0xff);
    return 1;
}
