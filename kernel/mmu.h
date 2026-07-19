#ifndef KERNEL_MMU_H
#define KERNEL_MMU_H

// Defined in boot/mmu.S. Builds the identity-mapped Stage-1 translation
// tables and enables the MMU + I/D caches via SCTLR_EL1.
void mmu_init(void);

#endif
