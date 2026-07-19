#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

// Virtual timer, PPI 11 -> GIC interrupt ID 27. Verified against
// `make dumpdtb` (README §5). Using the virtual timer rather than the
// physical one (PPI 14 / ID 30) is deliberate, not the doc's original
// choice: under QEMU 11.0.2's `-cpu host` + `-accel hvf`, writing
// CNTP_TVAL_EL0 or CNTP_CVAL_EL0 (physical timer) takes a synchronous
// "Unknown reason" (ESR EC=0) exception — reproducible, and specific to
// that accelerator/CPU combination (identical code works under
// `-accel tcg`). CNTV_* is the architecturally-correct register set for a
// guest under virtualization anyway (CNTVOFF_EL2 exists precisely so a
// hypervisor can give each guest its own timeline), and it works cleanly
// under hvf.
#define TIMER_IRQ_ID 27

// Programs CNTV_CVAL_EL0/CNTV_CTL_EL0 for a periodic tick at approximately
// `hz` ticks/sec and enables its GIC interrupt. Does not unmask IRQs itself
// — the caller does that once everything is wired up.
void timer_init(uint32_t hz);

// Installed via irq_set_handler(); reloads the timer and increments the
// tick counter. Not meant to be called directly.
void timer_irq_handler(void);

uint64_t timer_get_ticks(void);

#endif
