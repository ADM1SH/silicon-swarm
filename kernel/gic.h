#ifndef KERNEL_GIC_H
#define KERNEL_GIC_H

#include <stdint.h>

void gic_init(void);
void gic_enable_irq(uint32_t irq_id);
uint32_t gic_ack_irq(void);
void gic_end_irq(uint32_t irq_id);

#endif
