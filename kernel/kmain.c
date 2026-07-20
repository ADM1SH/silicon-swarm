#include "engine/entity_soa.h"
#include "engine/flowfield.h"
#include "game/input.h"
#include "kernel/alloc.h"
#include "kernel/exceptions.h"
#include "kernel/framebuffer.h"
#include "kernel/gic.h"
#include "kernel/mmu.h"
#include "kernel/timer.h"
#include "kernel/uart.h"

#define CURSOR_SIZE 8
#define CURSOR_STEP 8
#define CURSOR_COLOR 0x00FFFFFFu
#define ENTITY_COLOR 0x0060C0FFu
#define WALL_COLOR 0x00802020u
#define BG_COLOR 0x00101018u
#define NUM_DUMMY_ENTITIES 10000
#define ENTITY_SPEED (2 << 16) // 2 px/tick in Q16.16, toward the next flow-field cell

// City center: the flow field's target. A wall sits just to its left, wide
// enough to force a real detour, with 5-cell gaps top and bottom so
// (0,0)-reachability isn't lost -- see tests/test_flowfield.c for the BFS
// correctness this depends on.
#define TARGET_GX (FLOWFIELD_W / 2)
#define TARGET_GY (FLOWFIELD_H / 2)
#define WALL_GX_START (TARGET_GX - 5)
#define WALL_GX_END (TARGET_GX - 3)
#define WALL_GY_START 5
#define WALL_GY_END (FLOWFIELD_H - 6)

static uint32_t gradient_color(int x, int y) {
    uint32_t r = (uint32_t)(x * 255 / (FB_WIDTH - 1));
    uint32_t g = (uint32_t)(y * 255 / (FB_HEIGHT - 1));
    uint32_t b = 0x40;
    return (r << 16) | (g << 8) | b;
}

static void draw_cursor(int x, int y, uint32_t color) {
    for (int dy = 0; dy < CURSOR_SIZE; dy++) {
        for (int dx = 0; dx < CURSOR_SIZE; dx++) {
            framebuffer_set_pixel(x + dx, y + dy, color);
        }
    }
}

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

// Deterministic pseudo-random spread (LCG, same constants glibc's rand()
// uses) -- no libc, and reproducible runs matter more than true randomness
// for a dummy movement demo.
static uint32_t g_rng_state = 12345;
static uint32_t next_rand(void) {
    g_rng_state = g_rng_state * 1103515245u + 12345u;
    return (g_rng_state >> 16) & 0x7fff;
}

// Phase 8's obstacle: a wall block just left of the city center, with
// 5-cell gaps top and bottom so entities can still detour around it.
static void setup_wall(void) {
    for (int gy = WALL_GY_START; gy <= WALL_GY_END; gy++) {
        for (int gx = WALL_GX_START; gx <= WALL_GX_END; gx++) {
            flowfield_cost[gy][gx] = 255;
        }
    }
}

static void draw_walls(void) {
    for (int gy = 0; gy < FLOWFIELD_H; gy++) {
        for (int gx = 0; gx < FLOWFIELD_W; gx++) {
            if (flowfield_cost[gy][gx] != 255) {
                continue;
            }
            int px0 = gx * FLOWFIELD_CELL_SIZE;
            int py0 = gy * FLOWFIELD_CELL_SIZE;
            for (int dy = 0; dy < FLOWFIELD_CELL_SIZE; dy++) {
                for (int dx = 0; dx < FLOWFIELD_CELL_SIZE; dx++) {
                    framebuffer_set_pixel(px0 + dx, py0 + dy, WALL_COLOR);
                }
            }
        }
    }
}

// Spawns at screen edges per the phase spec, spread round-robin across all
// four. Velocity starts at zero -- entity_tick() steers every entity from
// the flow field each tick, not from an initial throw.
static void spawn_dummy_entities(void) {
    for (int i = 0; i < NUM_DUMMY_ENTITIES; i++) {
        int32_t x, y;
        switch (i % 4) {
        case 0: // top edge
            x = (int32_t)(next_rand() % FB_WIDTH) << 16;
            y = 0;
            break;
        case 1: // bottom edge
            x = (int32_t)(next_rand() % FB_WIDTH) << 16;
            y = (int32_t)(FB_HEIGHT - 1) << 16;
            break;
        case 2: // left edge
            x = 0;
            y = (int32_t)(next_rand() % FB_HEIGHT) << 16;
            break;
        default: // right edge
            x = (int32_t)(FB_WIDTH - 1) << 16;
            y = (int32_t)(next_rand() % FB_HEIGHT) << 16;
            break;
        }
        entity_spawn(x, y, 0, 0, 1, 0);
    }
}

static int32_t clamp_step(int32_t delta, int32_t max_step) {
    if (delta > max_step) return max_step;
    if (delta < -max_step) return -max_step;
    return delta;
}

// Per-entity steering is a fixed O(1) flowfield_step() lookup -- no search,
// so cost doesn't grow with obstacle count (see tests/test_flowfield.c and
// README's Phase 8 note). engine/entity_soa.c's entity_update_all() then
// applies the velocities this computes, same as Phase 7.
static void entity_tick(void) {
    for (uint32_t i = 0; i < entity_count; i++) {
        if (!entity_alive[i]) {
            continue;
        }
        int gx = (int)(entity_x[i] >> 16) / FLOWFIELD_CELL_SIZE;
        int gy = (int)(entity_y[i] >> 16) / FLOWFIELD_CELL_SIZE;
        int next_gx, next_gy;
        if (!flowfield_step(gx, gy, &next_gx, &next_gy) ||
            (next_gx == gx && next_gy == gy)) {
            entity_vx[i] = 0; // unreachable, or already at the target cell
            entity_vy[i] = 0;
            continue;
        }
        int32_t target_px = (int32_t)(next_gx * FLOWFIELD_CELL_SIZE + FLOWFIELD_CELL_SIZE / 2) << 16;
        int32_t target_py = (int32_t)(next_gy * FLOWFIELD_CELL_SIZE + FLOWFIELD_CELL_SIZE / 2) << 16;
        entity_vx[i] = clamp_step(target_px - entity_x[i], ENTITY_SPEED);
        entity_vy[i] = clamp_step(target_py - entity_y[i], ENTITY_SPEED);
    }
    entity_update_all();
}

// Diagnostic only: since walls in this layout never fully block a path (see
// setup_wall()), an idle entity (vx==vy==0) has reached the target cell,
// not gotten stuck -- a growing count here over time is the empirical
// "entities actually path to the center" evidence, since there's no
// display to look at over a serial connection.
static uint32_t entities_arrived(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < entity_count; i++) {
        if (entity_alive[i] && entity_vx[i] == 0 && entity_vy[i] == 0) {
            n++;
        }
    }
    return n;
}

// One full frame: background, walls, every alive entity, then the cursor
// on top. A full clear + redraw each tick (rather than erase-by-old-
// position) is the only sane option once thousands of entities can overlap
// the same pixel.
static void render_frame(int cursor_x, int cursor_y) {
    framebuffer_fill(BG_COLOR);
    draw_walls();
    for (uint32_t i = 0; i < entity_count; i++) {
        if (!entity_alive[i]) {
            continue;
        }
        framebuffer_set_pixel(entity_x[i] >> 16, entity_y[i] >> 16, ENTITY_COLOR);
    }
    draw_cursor(cursor_x, cursor_y, CURSOR_COLOR);
    framebuffer_flush();
}

void kmain(void) {
    uart_init();
    uart_puts("SILICON SWARM BOOT OK\n");

    exceptions_init();

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
    setup_wall();
    flowfield_build(TARGET_GX, TARGET_GY);
    uart_puts("Phase 8: flow field built toward city center\n");

    int fb_ok = framebuffer_init();
    int cursor_x = FB_WIDTH / 2 - CURSOR_SIZE / 2;
    int cursor_y = FB_HEIGHT / 2 - CURSOR_SIZE / 2;
    if (fb_ok) {
        framebuffer_fill(0x001040A0); // solid fill
        framebuffer_flush();

        uint64_t wait_until = timer_get_ticks() + 60; // ~1s, so the solid
        while (timer_get_ticks() < wait_until) {}       // fill is separately observable

        for (int y = 0; y < FB_HEIGHT; y++) {
            for (int x = 0; x < FB_WIDTH; x++) {
                framebuffer_set_pixel(x, y, gradient_color(x, y));
            }
        }
        framebuffer_flush();
        uart_puts("test pattern drawn\n");

        spawn_dummy_entities();
        uart_puts("Phase 8: spawned ");
        print_dec64(entity_count);
        uart_puts(" entities at screen edges, pathing around the wall\n");
        uart_puts("Phase 6: WASD to move the cursor\n");
    }

    uint64_t last_reported = 0;
    uint64_t last_frame_tick = timer_get_ticks();
    uint64_t frame_count = 0;
    while (1) {
        uint64_t ticks = timer_get_ticks();
        if (ticks - last_reported >= 60) {
            // frame_count vs. ticks is the actual "stable 60Hz" evidence --
            // the tick counter itself is IRQ-driven and keeps incrementing
            // regardless of whether the render loop below is keeping up,
            // so a growing gap between them (not just the tick count alone)
            // is what would reveal dropped frames.
            uart_puts("tick count = ");
            print_dec64(ticks);
            uart_puts(", frames = ");
            print_dec64(frame_count);
            uart_puts(", arrived = ");
            print_dec64(entities_arrived());
            uart_putc('\n');
            last_reported = ticks;
        }

        if (fb_ok) {
            input_action_t action = input_poll();
            if (action != INPUT_NONE) {
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
                default:
                    break;
                }
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx > FB_WIDTH - CURSOR_SIZE) nx = FB_WIDTH - CURSOR_SIZE;
                if (ny > FB_HEIGHT - CURSOR_SIZE) ny = FB_HEIGHT - CURSOR_SIZE;
                if (nx != cursor_x || ny != cursor_y) {
                    cursor_x = nx;
                    cursor_y = ny;
                    uart_puts("cursor -> (");
                    print_dec64((uint64_t)cursor_x);
                    uart_puts(", ");
                    print_dec64((uint64_t)cursor_y);
                    uart_puts(")\n");
                }
            }

            // One simulation + render step per new tick -- this is what
            // "stable 60Hz" means: if entity_tick()+render_frame() ever
            // took longer than a tick's budget, ticks would advance faster
            // than frames render and this gate would fall behind visibly.
            if (ticks != last_frame_tick) {
                last_frame_tick = ticks;
                entity_tick();
                render_frame(cursor_x, cursor_y);
                frame_count++;
            }
        }
    }
}
