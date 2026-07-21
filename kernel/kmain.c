// v2: isometric city/siege game. Phase 1 = terrain view + camera pan.
// The v1 2D pixel-grid game loop was removed here (git history has it);
// build/siege game logic returns on the iso world in Phases 3-4.
#include "engine/entity_soa.h"
#include "engine/flowfield.h"
#include "engine/spatial_hash.h"
#include "engine/terrain.h"
#include "game/build_phase.h"
#include "game/city.h"
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
    city_init();

    int cur_gx = WORLD_W / 2, cur_gy = WORLD_H / 2;
    city_tile_t sel_tool = CITY_ROAD;

    if (fb_ok) {
        uart_puts("v2 Phase 3: CITY -- WASD cursor, q/e terrain, "
                   "1=barricade 2=turret 3=road 4=house, space=place, x=demolish\n");
    }

    uint64_t last_reported = 0;
    uint64_t last_frame_tick = timer_get_ticks();
    uint64_t frame_count = 0;
    uint64_t last_render_cycles = 0;

    while (1) {
        uint64_t ticks = timer_get_ticks();
        if (ticks - last_reported >= 60) {
            city_income_tick(); // once per second, alongside the report
            uart_puts("tick count = ");
            print_dec64(ticks);
            uart_puts(", frames = ");
            print_dec64(frame_count);
            uart_puts(", money = ");
            print_dec64((uint64_t)(city_money > 0 ? city_money : 0));
            uart_puts(", render_cyc = ");
            print_dec64(last_render_cycles);
            uart_putc('\n');
            last_reported = ticks;
        }

        if (fb_ok) {
            input_action_t action = input_poll();
            switch (action) {
            case INPUT_UP:
                if (cur_gy > 0) cur_gy--;
                break;
            case INPUT_DOWN:
                if (cur_gy < WORLD_H - 1) cur_gy++;
                break;
            case INPUT_LEFT:
                if (cur_gx > 0) cur_gx--;
                break;
            case INPUT_RIGHT:
                if (cur_gx < WORLD_W - 1) cur_gx++;
                break;
            case INPUT_RAISE:
                terrain_edit_tile(cur_gx, cur_gy, +1);
                break;
            case INPUT_LOWER:
                terrain_edit_tile(cur_gx, cur_gy, -1);
                break;
            case INPUT_TOOL_BARRICADE:
                sel_tool = CITY_BARRICADE;
                uart_puts("tool: barricade\n");
                break;
            case INPUT_TOOL_TURRET:
                sel_tool = CITY_TURRET;
                uart_puts("tool: turret\n");
                break;
            case INPUT_TOOL_ROAD:
                sel_tool = CITY_ROAD;
                uart_puts("tool: road\n");
                break;
            case INPUT_TOOL_HOUSE:
                sel_tool = CITY_HOUSE;
                uart_puts("tool: house\n");
                break;
            case INPUT_PLACE:
                uart_puts(city_place(cur_gx, cur_gy, sel_tool)
                              ? "placed\n"
                              : "rejected (occupied/sloped/broke)\n");
                break;
            case INPUT_DEMOLISH:
                if (city_demolish(cur_gx, cur_gy)) {
                    uart_puts("demolished\n");
                }
                break;
            default:
                break;
            }

            if (ticks != last_frame_tick) {
                last_frame_tick = ticks;
                // Camera keeps the cursor tile centered (simplest follow;
                // free panning went away with v1 -- the cursor IS the view).
                int cam_x = ((cur_gx - cur_gy) * (TILE_W / 2)) - FB_WIDTH / 2;
                int cam_y = ((cur_gx + cur_gy) * (TILE_H / 2)) -
                            world_height[cur_gy][cur_gx] * ELEV_STEP - FB_HEIGHT / 2;
                uint64_t render_t0 = perf_cycles();
                framebuffer_fill(0x00101018);
                terrain_render(cam_x, cam_y, cur_gx, cur_gy, city_draw_tile);
                framebuffer_flush();
                last_render_cycles = perf_cycles() - render_t0;
                frame_count++;
            }
        }
    }
}
