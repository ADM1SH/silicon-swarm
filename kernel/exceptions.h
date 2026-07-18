#ifndef KERNEL_EXCEPTIONS_H
#define KERNEL_EXCEPTIONS_H

#include <stdint.h>

// Mirrors the stack layout vectors.S builds on exception entry:
// x0..x30 (31 regs), then the saved ELR_EL1/SPSR_EL1 for this exception.
struct trap_frame {
    uint64_t x[31];
    uint64_t elr;
    uint64_t spsr;
};

typedef void (*irq_handler_fn)(void);

void exceptions_init(void);

// Phase 4 installs the GIC/timer IRQ handler here; until then any IRQ falls
// through to the default panic-and-halt path like every other exception.
void irq_set_handler(irq_handler_fn handler);

// Called from vectors.S — needs external linkage to be reachable from asm,
// not meant to be called directly from other C code.
void exc_dispatch(uint64_t type, uint64_t esr, struct trap_frame *ctx);

#endif
