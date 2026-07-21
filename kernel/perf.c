#include "kernel/perf.h"

void perf_init(void) {
    // PMCR_EL0: E (bit0, enable all counters), C (bit2, reset the cycle
    // counter to 0). Leaves the event counters and their config alone --
    // we only ever want the cycle counter.
    uint64_t pmcr;
    __asm__ volatile("mrs %0, pmcr_el0" : "=r"(pmcr));
    pmcr |= (1u << 0) | (1u << 2);
    __asm__ volatile("msr pmcr_el0, %0" ::"r"(pmcr));

    // PMCNTENSET_EL0 bit 31 enables the cycle counter specifically (the
    // event counters have their own bits, unused here).
    __asm__ volatile("msr pmcntenset_el0, %0" ::"r"((uint64_t)(1ull << 31)));

    // PMUSERENR_EL0.EN: lets EL0 read the counters directly. Not needed by
    // this kernel (everything runs at EL1), but harmless and matches what
    // the roadmap calls out explicitly.
    __asm__ volatile("msr pmuserenr_el0, %0" ::"r"((uint64_t)1));

    __asm__ volatile("isb");
}

uint64_t perf_cycles(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, pmccntr_el0" : "=r"(v));
    return v;
}
