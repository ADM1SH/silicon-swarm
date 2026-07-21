// v2: isometric city/siege game. Phase 1 = terrain view + camera pan.
// The v1 2D pixel-grid game loop was removed here (git history has it);
// build/siege game logic returns on the iso world in Phases 3-4.
#include "engine/entity_soa.h"
#include "engine/flowfield.h"
#include "engine/spatial_hash.h"
#include "engine/terrain.h"
#include "game/build_phase.h"
#include "game/input.h"
#include "game/siege_phase.h"
#include "kernel/alloc.h"
#include "kernel/exceptions.h"
#include "kernel/framebuffer.h"
#include "kernel/gic.h"
#include "kernel/mmu.h"
#include "kernel/perf.h"
#include "kernel/timer.h"
#include "kernel/uart.h"

#define CAM_STEP 32 // px per pan keypress

static void print_dec64(uint64_t v) {
    char buf[20];
    int i = 0;
    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0) {
        buf[i++] = '0' + (char)(v % 10);
        v /= 10;
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

void kmain(void) {
    uart_init();
    uart_puts("SILICON SWARM BOOT OK\n");

    exceptions_init();
    perf_init(); // right after exceptions_init(): a bad MDCR_EL2/PMCR_EL0
                 // setup should trap and print a diagnostic, not hang silently

    mmu_init();
    uart_puts("MMU + caches enabled\n");

    gic_init();
    timer_init(60);
    irq_set_handler(timer_irq_handler);
    __asm__ volatile("msr daifclr, #2"); // unmask IRQ
    uart_puts("GIC + timer enabled (60Hz)\n");

    alloc_init();
    entity_soa_init();
    spatial_hash_init();

    int fb_ok = framebuffer_init();
    terrain_init();

    // Center the camera on the middle of the world (the city plateau),
    // accounting for the plateau's actual surface height.
    int cam_x = ((WORLD_W / 2 - WORLD_H / 2) * (TILE_W / 2)) - FB_WIDTH / 2;
    int cam_y = ((WORLD_W / 2 + WORLD_H / 2) * (TILE_H / 2)) -
                world_height[WORLD_H / 2][WORLD_W / 2] * ELEV_STEP - FB_HEIGHT / 2;

    if (fb_ok) {
        uart_puts("v2 Phase 1: ISO TERRAIN -- WASD pans the camera\n");
    }

    uint64_t last_reported = 0;
    uint64_t last_frame_tick = timer_get_ticks();
    uint64_t frame_count = 0;
    uint64_t last_render_cycles = 0;

    while (1) {
        uint64_t ticks = timer_get_ticks();
        if (ticks - last_reported >= 60) {
            uart_puts("tick count = ");
            print_dec64(ticks);
            uart_puts(", frames = ");
            print_dec64(frame_count);
            uart_puts(", render_cyc = ");
            print_dec64(last_render_cycles);
            uart_putc('\n');
            last_reported = ticks;
        }

        if (fb_ok) {
            input_action_t action = input_poll();
            switch (action) {
            case INPUT_UP:
                cam_y -= CAM_STEP;
                break;
            case INPUT_DOWN:
                cam_y += CAM_STEP;
                break;
            case INPUT_LEFT:
                cam_x -= CAM_STEP;
                break;
            case INPUT_RIGHT:
                cam_x += CAM_STEP;
                break;
            default:
                break;
            }

            if (ticks != last_frame_tick) {
                last_frame_tick = ticks;
                uint64_t render_t0 = perf_cycles();
                framebuffer_fill(0x00101018);
                terrain_render(cam_x, cam_y);
                framebuffer_flush();
                last_render_cycles = perf_cycles() - render_t0;
                frame_count++;
            }
        }
    }
}
