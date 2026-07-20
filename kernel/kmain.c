#include "engine/blit_neon.h"
#include "engine/entity_soa.h"
#include "engine/flowfield.h"
#include "engine/spatial_hash.h"
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
#define ATTACKER_COLOR 0x0060C0FFu
#define DEFENDER_COLOR 0x00FFD060u
#define WALL_COLOR 0x00802020u
#define BG_COLOR 0x00101018u
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

// Phase 10: two opposing groups. Attackers spawn at screen edges and path
// toward the center (Phase 8); defenders hold a static cluster around the
// target, so the swarm has to fight through them, not just walk past.
#define ENTITY_TYPE_ATTACKER 0
#define ENTITY_TYPE_DEFENDER 1
#define NUM_ATTACKERS 50000
#define NUM_DEFENDERS 500
#define ATTACKER_HP 4
#define DEFENDER_HP 10
#define COMBAT_RANGE_PX 10
#define COMBAT_RANGE_SQ (COMBAT_RANGE_PX * COMBAT_RANGE_PX)
#define COMBAT_DAMAGE 2

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

// Spawns near screen edges per Phase 8's spec, spread round-robin across
// all four. "Near" is a multi-cell-deep band (EDGE_BAND_CELLS), not a
// single pixel row/column -- pinning thousands of entities to exactly
// y=0 (etc.) collapses them all into one row of spatial-hash cells, the
// pathological high-density case documented in
// engine/spatial_hash.h -- discovered by hitting it directly with 50,000
// attackers before this fix (dense cells means the spatial hash's O(n)
// bound degrades toward the O(n^2) it exists to avoid). Velocity starts
// at zero -- entity_tick() steers every attacker from the flow field each
// tick, not from an initial throw.
#define EDGE_BAND_CELLS 4
#define EDGE_BAND_PX (EDGE_BAND_CELLS * SPATIAL_HASH_CELL_SIZE)

static void spawn_attackers(void) {
    for (int i = 0; i < NUM_ATTACKERS; i++) {
        int32_t x, y;
        switch (i % 4) {
        case 0: // near top edge
            x = (int32_t)(next_rand() % FB_WIDTH) << 16;
            y = (int32_t)(next_rand() % EDGE_BAND_PX) << 16;
            break;
        case 1: // near bottom edge
            x = (int32_t)(next_rand() % FB_WIDTH) << 16;
            y = (int32_t)(FB_HEIGHT - 1 - (int32_t)(next_rand() % EDGE_BAND_PX)) << 16;
            break;
        case 2: // near left edge
            x = (int32_t)(next_rand() % EDGE_BAND_PX) << 16;
            y = (int32_t)(next_rand() % FB_HEIGHT) << 16;
            break;
        default: // near right edge
            x = (int32_t)(FB_WIDTH - 1 - (int32_t)(next_rand() % EDGE_BAND_PX)) << 16;
            y = (int32_t)(next_rand() % FB_HEIGHT) << 16;
            break;
        }
        entity_spawn(x, y, 0, 0, ATTACKER_HP, ENTITY_TYPE_ATTACKER);
    }
}

// Clustered around the city center, jittered so they're not all in one
// pixel -- a static garrison the attacker swarm has to fight through.
static void spawn_defenders(void) {
    int32_t center_x = (int32_t)(TARGET_GX * FLOWFIELD_CELL_SIZE + FLOWFIELD_CELL_SIZE / 2) << 16;
    int32_t center_y = (int32_t)(TARGET_GY * FLOWFIELD_CELL_SIZE + FLOWFIELD_CELL_SIZE / 2) << 16;
    for (int i = 0; i < NUM_DEFENDERS; i++) {
        int32_t jitter_x = (int32_t)((next_rand() % 41) - 20) << 16; // +/-20px
        int32_t jitter_y = (int32_t)((next_rand() % 41) - 20) << 16;
        entity_spawn(center_x + jitter_x, center_y + jitter_y, 0, 0, DEFENDER_HP,
                     ENTITY_TYPE_DEFENDER);
    }
}

static int32_t clamp_step(int32_t delta, int32_t max_step) {
    if (delta > max_step) return max_step;
    if (delta < -max_step) return -max_step;
    return delta;
}

// Only attackers path via the flow field; defenders hold position. Per-
// entity steering is a fixed O(1) flowfield_step() lookup -- no search, so
// cost doesn't grow with obstacle count (see tests/test_flowfield.c and
// README's Phase 8 note). This part is inherently scalar: each entity
// reads a different, unpredictable grid cell, so there's no sequential
// access pattern for NEON to exploit (would need gather loads -- SVE, not
// base NEON).
//
// Applying the resulting velocities is the opposite case -- a uniform pass
// over contiguous SoA arrays -- so that part calls the Phase 9 NEON
// primitive (engine/blit_neon.S) instead of entity_soa.c's scalar
// entity_update_all(). No alive check needed: entity_kill() (Phase 10)
// keeps [0, entity_count) fully alive and dense by construction, so this
// loop never has to branch around a dead slot.
static void entity_steer(void) {
    for (uint32_t i = 0; i < entity_count; i++) {
        if (entity_type[i] != ENTITY_TYPE_ATTACKER) {
            entity_vx[i] = 0;
            entity_vy[i] = 0;
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
    entity_update_positions_neon(entity_x, entity_y, entity_vx, entity_vy, entity_count);
}

// spatial_hash_for_each_nearby_pair() visits both (a, b) and (b, a) for
// every candidate pair, so applying damage only to `a` here still means
// each entity takes exactly one hit per hostile neighbor per tick, not
// two -- see engine/spatial_hash.h.
static void combat_pair_callback(uint32_t a, uint32_t b, void *userdata) {
    (void)userdata;
    if (entity_type[a] == entity_type[b]) {
        return; // no friendly fire
    }
    int32_t dx = (entity_x[a] - entity_x[b]) >> 16;
    int32_t dy = (entity_y[a] - entity_y[b]) >> 16;
    if (dx * dx + dy * dy > COMBAT_RANGE_SQ) {
        return;
    }
    entity_hp[a] -= COMBAT_DAMAGE;
}

// Swap-and-pop compaction (entity_kill()) shifts the last entity into a
// killed slot, so the newly-swapped-in entity at `i` must be re-checked,
// not skipped -- hence no unconditional i++ here.
static void resolve_deaths(void) {
    uint32_t i = 0;
    while (i < entity_count) {
        if (entity_hp[i] <= 0) {
            entity_kill(i);
        } else {
            i++;
        }
    }
}

static void count_by_type(uint32_t *out_attackers, uint32_t *out_defenders) {
    uint32_t a = 0, d = 0;
    for (uint32_t i = 0; i < entity_count; i++) {
        if (entity_type[i] == ENTITY_TYPE_DEFENDER) {
            d++;
        } else {
            a++;
        }
    }
    *out_attackers = a;
    *out_defenders = d;
}

// Once one side is wiped out, combat is over -- entities that have
// "arrived" at the flow field's single target cell have nowhere else to
// go and just sit there, so their count in that one cell keeps growing
// every tick regardless of whether combat runs. Querying the spatial hash
// against an ever-growing same-type cluster gets more expensive over
// time even though it can never find anything to fight (found by hitting
// exactly this: frame throughput kept degrading tick after tick with
// entity_count and defender_count both flat at 0, which a bounded O(n)
// per-tick cost can't explain -- only a workload that keeps growing while
// counts stay still can). Skipping the pass once either side hits 0 is
// both the fix and the game-correct behavior: there's nothing left to
// resolve.
static void entity_tick(void) {
    entity_steer();
    uint32_t attackers, defenders;
    count_by_type(&attackers, &defenders);
    if (attackers > 0 && defenders > 0) {
        spatial_hash_build();
        spatial_hash_for_each_nearby_pair(combat_pair_callback, NULL);
        resolve_deaths();
    }
}

// One full frame: background, walls, every entity (colored by type), then
// the cursor on top. A full clear + redraw each tick (rather than erase-
// by-old-position) is the only sane option once thousands of entities can
// overlap the same pixel.
static void render_frame(int cursor_x, int cursor_y) {
    framebuffer_fill(BG_COLOR);
    draw_walls();
    for (uint32_t i = 0; i < entity_count; i++) {
        uint32_t color = (entity_type[i] == ENTITY_TYPE_DEFENDER) ? DEFENDER_COLOR : ATTACKER_COLOR;
        framebuffer_set_pixel(entity_x[i] >> 16, entity_y[i] >> 16, color);
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
    spatial_hash_init();
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

        spawn_attackers();
        spawn_defenders();
        uart_puts("Phase 10: spawned ");
        print_dec64(entity_count);
        uart_puts(" entities (attackers at edges, defenders at the center)\n");
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
            // is what would reveal dropped frames. attackers/defenders
            // shrinking over time is the evidence combat is actually
            // resolving, not just running.
            uint32_t attackers, defenders;
            count_by_type(&attackers, &defenders);
            uart_puts("tick count = ");
            print_dec64(ticks);
            uart_puts(", frames = ");
            print_dec64(frame_count);
            uart_puts(", attackers = ");
            print_dec64(attackers);
            uart_puts(", defenders = ");
            print_dec64(defenders);
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
