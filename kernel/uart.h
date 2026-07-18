#ifndef KERNEL_UART_H
#define KERNEL_UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

// Non-blocking read: returns 1 and stores the byte in *out if one was
// available, 0 otherwise. Used by Phase 6 input polling.
int uart_getc_nonblock(uint8_t *out);

#endif
