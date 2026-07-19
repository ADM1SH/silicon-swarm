#include "kernel/timer.h"
#include "kernel/gic.h"

static volatile uint64_t g_ticks = 0;
static uint32_t g_reload;

static inline uint64_t read_cntvct(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

void timer_init(uint32_t hz) {
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    g_reload = (uint32_t)(freq / hz);

    gic_enable_irq(TIMER_IRQ_ID);

    uint64_t cval = read_cntvct() + g_reload;
    __asm__ volatile("msr cntv_cval_el0, %0; isb" ::"r"(cval));
    __asm__ volatile("msr cntv_ctl_el0, %0; isb" ::"r"((uint64_t)1)); // ENABLE=1, IMASK=0
}

void timer_irq_handler(void) {
    uint32_t id = gic_ack_irq();
    if (id == TIMER_IRQ_ID) {
        uint64_t cval = read_cntvct() + g_reload;
        __asm__ volatile("msr cntv_cval_el0, %0" ::"r"(cval));
        g_ticks++;
    }
    gic_end_irq(id);
}

uint64_t timer_get_ticks(void) {
    return g_ticks;
}
