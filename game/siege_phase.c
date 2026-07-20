#include "game/siege_phase.h"
#include <stddef.h>

#include "engine/blit_neon.h"
#include "engine/entity_soa.h"
#include "engine/flowfield.h"
#include "engine/spatial_hash.h"
#include "game/build_phase.h"

#define ENTITY_TYPE_ATTACKER 0
#define ENTITY_TYPE_DEFENDER 1

#define NUM_ATTACKERS 2000
#define ATTACKER_HP 4
#define DEFENDER_HP 10
#define ENTITY_SPEED (2 << 16) // 2 px/tick in Q16.16
#define COMBAT_RANGE_PX 10
#define COMBAT_RANGE_SQ (COMBAT_RANGE_PX * COMBAT_RANGE_PX)
#define COMBAT_DAMAGE 2
#define CORE_HP_START 20
#define CORE_DAMAGE_PER_HIT 2

// Multi-cell-deep spawn band, not a single pixel row/column -- see Phase
// 10's commit for why (collapsing thousands of entities into one row of
// spatial-hash cells is the pathological density case
// engine/spatial_hash.h documents).
#define EDGE_BAND_CELLS 4
#define EDGE_BAND_PX (EDGE_BAND_CELLS * SPATIAL_HASH_CELL_SIZE)

static int32_t g_core_hp;
static uint32_t g_rng_state = 12345;

static uint32_t next_rand(void) {
    g_rng_state = g_rng_state * 1103515245u + 12345u;
    return (g_rng_state >> 16) & 0x7fff;
}

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

static void spawn_turret_defenders(void) {
    for (int gy = 0; gy < BUILD_GRID_H; gy++) {
        for (int gx = 0; gx < BUILD_GRID_W; gx++) {
            if (city_grid[gy][gx] != TILE_TURRET) {
                continue;
            }
            int32_t x = (int32_t)(gx * FLOWFIELD_CELL_SIZE + FLOWFIELD_CELL_SIZE / 2) << 16;
            int32_t y = (int32_t)(gy * FLOWFIELD_CELL_SIZE + FLOWFIELD_CELL_SIZE / 2) << 16;
            entity_spawn(x, y, 0, 0, DEFENDER_HP, ENTITY_TYPE_DEFENDER);
        }
    }
}

void siege_phase_start(void) {
    entity_soa_init();
    flowfield_build(SIEGE_TARGET_GX, SIEGE_TARGET_GY);
    spawn_turret_defenders();
    spawn_attackers();
    g_core_hp = CORE_HP_START;
}

static int32_t clamp_step(int32_t delta, int32_t max_step) {
    if (delta > max_step) return max_step;
    if (delta < -max_step) return -max_step;
    return delta;
}

// Per-entity steering is an O(1) flowfield_step() lookup -- inherently
// scalar (each entity reads a different, unpredictable cell), so no NEON
// win here; applying the resulting velocities is a uniform pass over
// contiguous SoA arrays, so that part uses the Phase 9 NEON primitive.
static void entity_steer(void) {
    for (uint32_t i = 0; i < entity_count; i++) {
        if (entity_type[i] != ENTITY_TYPE_ATTACKER) {
            entity_vx[i] = 0;
            entity_vy[i] = 0;
            continue;
        }
        int gx = (int)(entity_x[i] >> 16) / FLOWFIELD_CELL_SIZE;
        int gy = (int)(entity_y[i] >> 16) / FLOWFIELD_CELL_SIZE;
        if (gx == SIEGE_TARGET_GX && gy == SIEGE_TARGET_GY) {
            // Reached the core: hits it once and is spent. This is also
            // what keeps "arrived" attackers from piling into the target
            // cell forever the way Phase 10's demo did before entities
            // could actually die there -- an ever-growing same-type
            // cluster is exactly the density that made frame time degrade
            // without bound in that bug.
            g_core_hp -= CORE_DAMAGE_PER_HIT;
            entity_hp[i] = 0;
            entity_vx[i] = 0;
            entity_vy[i] = 0;
            continue;
        }
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

// spatial_hash_for_each_nearby_pair() visits both (a, b) and (b, a), so
// applying damage only to `a` here still means each entity takes exactly
// one hit per hostile neighbor per tick, not two.
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

// entity_kill() swap-and-pop shifts the last entity into a killed slot,
// so the newly-swapped-in entity at `i` must be re-checked, not skipped.
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

void siege_phase_tick(void) {
    entity_steer();
    uint32_t attackers, defenders;
    siege_phase_counts(&attackers, &defenders);
    if (attackers > 0 && defenders > 0) {
        spatial_hash_build();
        spatial_hash_for_each_nearby_pair(combat_pair_callback, NULL);
    }
    resolve_deaths();
}

void siege_phase_counts(uint32_t *out_attackers, uint32_t *out_defenders) {
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

int32_t siege_phase_core_hp(void) {
    return g_core_hp;
}

int siege_phase_is_won(void) {
    uint32_t a, d;
    siege_phase_counts(&a, &d);
    return g_core_hp > 0 && a == 0;
}

int siege_phase_is_lost(void) {
    return g_core_hp <= 0;
}
