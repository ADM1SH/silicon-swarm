// GICv2 driver — distributor + CPU interface, single core (v1). Addresses
// verified against `make dumpdtb` output (README §5): GICD @ 0x08000000,
// GICC @ 0x08010000.
#include "kernel/gic.h"

#define GICD_BASE 0x08000000UL
#define GICC_BASE 0x08010000UL

#define GICD_CTLR (*(volatile uint32_t *)(GICD_BASE + 0x000))
#define GICD_ISENABLER(n) (*(volatile uint32_t *)(GICD_BASE + 0x100 + 4 * (n)))
#define GICD_IPRIORITYR(n) (*(volatile uint8_t *)(GICD_BASE + 0x400 + (n)))
#define GICD_ITARGETSR(n) (*(volatile uint8_t *)(GICD_BASE + 0x800 + (n)))

#define GICC_CTLR (*(volatile uint32_t *)(GICC_BASE + 0x000))
#define GICC_PMR (*(volatile uint32_t *)(GICC_BASE + 0x004))
#define GICC_IAR (*(volatile uint32_t *)(GICC_BASE + 0x00c))
#define GICC_EOIR (*(volatile uint32_t *)(GICC_BASE + 0x010))

void gic_init(void) {
    GICD_CTLR = 1; // enable the distributor (forward IRQs to CPU interfaces)

    GICC_PMR = 0xff; // accept all priorities
    GICC_CTLR = 1;    // enable this CPU's interface (signal IRQs to the core)
}

void gic_enable_irq(uint32_t irq_id) {
    GICD_IPRIORITYR(irq_id) = 0x80; // mid-range priority; only one source in v1
    GICD_ITARGETSR(irq_id) = 0x1;   // route to CPU0 (PPIs ignore this field
                                     // on real hardware, harmless to set)
    GICD_ISENABLER(irq_id / 32) = (1u << (irq_id % 32));
}

uint32_t gic_ack_irq(void) {
    return GICC_IAR & 0x3ff; // low 10 bits = interrupt ID
}

void gic_end_irq(uint32_t irq_id) {
    GICC_EOIR = irq_id;
}
