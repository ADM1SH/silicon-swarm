#ifndef KERNEL_PERF_H
#define KERNEL_PERF_H

#include <stdint.h>

// Enables PMCCNTR_EL0 (the cycle counter). Call once at boot, after
// exceptions_init() -- if MDCR_EL2/PMCR_EL0 configuration is ever wrong
// on some QEMU/hvf combination, we want a loud diagnostic from the
// exception handler, not a silent hang.
void perf_init(void);

// Free-running core-clock cycle count. Not a fixed frequency and not the
// same clock domain as the generic timer (kernel/timer.h) -- only valid
// to compare as a delta against another perf_cycles() call, never
// converted to wall-clock time here.
uint64_t perf_cycles(void);

#endif
