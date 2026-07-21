#include "engine/entity_soa.h"
#include "engine/flowfield.h"
#include "engine/spatial_hash.h"
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

#define CURSOR_SIZE 6
#define CURSOR_STEP FLOWFIELD_CELL_SIZE // grid-aligned movement, one cell per press
#define CURSOR_COLOR 0x00FFFFFFu
#define ATTACKER_COLOR 0x0060C0FFu
#define DEFENDER_COLOR 0x00FFD060u
#define BARRICADE_COLOR 0x00802020u
#define CORE_COLOR 0x0040FF40u
#define BG_COLOR 0x00101018u
#define GRID_COLOR 0x00202030u

typedef enum {
    GAME_BUILD,
    GAME_SIEGE,
    GAME_WON,
    GAME_LOST,
} game_state_t;

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

static void draw_cursor(int x, int y, uint32_t color) {
    // Offset into the cell rather than filling it, so a tile already
    // there stays visible underneath.
    int ox = x + (FLOWFIELD_CELL_SIZE - CURSOR_SIZE) / 2;
    int oy = y + (FLOWFIELD_CELL_SIZE - CURSOR_SIZE) / 2;
    for (int dy = 0; dy < CURSOR_SIZE; dy++) {
        for (int dx = 0; dx < CURSOR_SIZE; dx++) {
            framebuffer_set_pixel(ox + dx, oy + dy, color);
        }
    }
}

static void draw_tiles(void) {
    for (int gy = 0; gy < BUILD_GRID_H; gy++) {
        for (int gx = 0; gx < BUILD_GRID_W; gx++) {
            uint32_t color;
            switch (city_grid[gy][gx]) {
            case TILE_BARRICADE:
                color = BARRICADE_COLOR;
                break;
            case TILE_TURRET:
                color = DEFENDER_COLOR;
                break;
            case TILE_CORE:
                color = CORE_COLOR;
                break;
            default:
                continue; // empty -- leave the background showing
            }
            int px0 = gx * FLOWFIELD_CELL_SIZE;
            int py0 = gy * FLOWFIELD_CELL_SIZE;
            for (int dy = 0; dy < FLOWFIELD_CELL_SIZE; dy++) {
                for (int dx = 0; dx < FLOWFIELD_CELL_SIZE; dx++) {
                    framebuffer_set_pixel(px0 + dx, py0 + dy, color);
                }
            }
        }
    }
}

// Faint cell grid, build phase only. The first build frame used to be near
// solid black (background + one core tile), which players reported as a
// "blank window" -- the grid makes the play field visibly present.
static void draw_grid_lines(void) {
    for (int y = 0; y < FB_HEIGHT; y += FLOWFIELD_CELL_SIZE) {
        for (int x = 0; x < FB_WIDTH; x++) {
            framebuffer_set_pixel(x, y, GRID_COLOR);
        }
    }
    for (int x = 0; x < FB_WIDTH; x += FLOWFIELD_CELL_SIZE) {
        for (int y = 0; y < FB_HEIGHT; y++) {
            framebuffer_set_pixel(x, y, GRID_COLOR);
        }
    }
}

static void render_build_frame(int cursor_x, int cursor_y) {
    framebuffer_fill(BG_COLOR);
    draw_grid_lines();
    draw_tiles();
    draw_cursor(cursor_x, cursor_y, CURSOR_COLOR);
    framebuffer_flush();
}

// Tiles stay visible under the entities that grew out of them (a turret
// tile's defender spawns right on top of it) -- keeps the board legible
// during the fight, not just at build time.
static void render_siege_frame(void) {
    framebuffer_fill(BG_COLOR);
    draw_tiles();
    for (uint32_t i = 0; i < entity_count; i++) {
        uint32_t color = (entity_type[i] == 1) ? DEFENDER_COLOR : ATTACKER_COLOR;
        framebuffer_set_pixel(entity_x[i] >> 16, entity_y[i] >> 16, color);
    }
    framebuffer_flush();
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
    flowfield_init();
    spatial_hash_init();
    build_phase_init();
    city_grid[SIEGE_TARGET_GY][SIEGE_TARGET_GX] = TILE_CORE; // protect the city center from placement

    int fb_ok = framebuffer_init();
    int cursor_x = SIEGE_TARGET_GX * FLOWFIELD_CELL_SIZE;
    int cursor_y = SIEGE_TARGET_GY * FLOWFIELD_CELL_SIZE;
    tile_type_t selected_tool = TILE_BARRICADE;
    game_state_t state = GAME_BUILD;
    int result_printed = 0;

    if (fb_ok) {
        render_build_frame(cursor_x, cursor_y);
        uart_puts("Phase 11: BUILD PHASE -- WASD move, 1=barricade 2=turret, "
                   "space=place, enter=start siege\n");
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
            if (state == GAME_SIEGE || state == GAME_WON || state == GAME_LOST) {
                uint32_t attackers, defenders;
                siege_phase_counts(&attackers, &defenders);
                int32_t core_hp = siege_phase_core_hp();
                uart_puts(", attackers = ");
                print_dec64(attackers);
                uart_puts(", defenders = ");
                print_dec64(defenders);
                uart_puts(", core_hp = ");
                print_dec64((uint64_t)(core_hp > 0 ? core_hp : 0));
                // Phase 12: core-clock cycles for the last tick's three
                // phases -- comparing these directly (not converting to
                // wall-clock time) is what identifies the bottleneck at
                // this entity count.
                uart_puts(", steer_cyc = ");
                print_dec64(siege_phase_last_steer_cycles());
                uart_puts(", combat_cyc = ");
                print_dec64(siege_phase_last_combat_cycles());
                uart_puts(", render_cyc = ");
                print_dec64(last_render_cycles);
            }
            uart_putc('\n');
            last_reported = ticks;
        }

        if (fb_ok) {
            input_action_t action = input_poll();
            if (state == GAME_BUILD && action != INPUT_NONE) {
                int nx = cursor_x, ny = cursor_y;
                switch (action) {
                case INPUT_UP:
                    ny -= CURSOR_STEP;
                    break;
                case INPUT_DOWN:
                    ny += CURSOR_STEP;
                    break;
                case INPUT_LEFT:
                    nx -= CURSOR_STEP;
                    break;
                case INPUT_RIGHT:
                    nx += CURSOR_STEP;
                    break;
                case INPUT_TOOL_BARRICADE:
                    selected_tool = TILE_BARRICADE;
                    uart_puts("tool: barricade\n");
                    break;
                case INPUT_TOOL_TURRET:
                    selected_tool = TILE_TURRET;
                    uart_puts("tool: turret\n");
                    break;
                case INPUT_PLACE: {
                    int gx = cursor_x / FLOWFIELD_CELL_SIZE;
                    int gy = cursor_y / FLOWFIELD_CELL_SIZE;
                    if (build_phase_place(gx, gy, selected_tool)) {
                        uart_puts("placed\n");
                    } else {
                        uart_puts("placement rejected (occupied or out of bounds)\n");
                    }
                    break;
                }
                case INPUT_START:
                    siege_phase_start();
                    state = GAME_SIEGE;
                    uart_puts("Phase 11: SIEGE PHASE started\n");
                    break;
                default:
                    break;
                }
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx > (BUILD_GRID_W - 1) * FLOWFIELD_CELL_SIZE) nx = (BUILD_GRID_W - 1) * FLOWFIELD_CELL_SIZE;
                if (ny > (BUILD_GRID_H - 1) * FLOWFIELD_CELL_SIZE) ny = (BUILD_GRID_H - 1) * FLOWFIELD_CELL_SIZE;
                cursor_x = nx;
                cursor_y = ny;
            }

            // One simulation + render step per new tick.
            if (ticks != last_frame_tick) {
                last_frame_tick = ticks;

                if (state == GAME_SIEGE) {
                    siege_phase_tick();
                    if (siege_phase_is_lost()) {
                        state = GAME_LOST;
                    } else if (siege_phase_is_won()) {
                        state = GAME_WON;
                    }
                }

                if (!result_printed && (state == GAME_WON || state == GAME_LOST)) {
                    uart_puts(state == GAME_WON ? "Phase 11: SIEGE WON -- city center held\n"
                                                 : "Phase 11: SIEGE LOST -- city center destroyed\n");
                    result_printed = 1;
                }

                uint64_t render_t0 = perf_cycles();
                if (state == GAME_BUILD) {
                    render_build_frame(cursor_x, cursor_y);
                } else {
                    render_siege_frame();
                }
                last_render_cycles = perf_cycles() - render_t0;
                frame_count++;
            }
        }
    }
}
