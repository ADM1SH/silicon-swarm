#include "game/siege_phase.h"
#include <stddef.h>

#include "engine/blit_neon.h"
#include "engine/entity_soa.h"
#include "engine/flowfield.h"
#include "engine/spatial_hash.h"
#include "game/city.h"
#include "kernel/perf.h"

// Entity coordinates are Q16.16 "world units": FLOWFIELD_CELL_SIZE (16)
// units per world tile, so unit -> tile is a >>4 after the >>16.
#define WORLD_UNITS_W (WORLD_W * FLOWFIELD_CELL_SIZE)
#define WORLD_UNITS_H (WORLD_H * FLOWFIELD_CELL_SIZE)

#define ENTITY_TYPE_ATTACKER 0
#define ENTITY_TYPE_DEFENDER 1

#define NUM_ATTACKERS 2000
#define ATTACKER_HP 4
#define DEFENDER_HP 40
#define ENTITY_SPEED (2 << 16) // 2 px/tick in Q16.16
#define COMBAT_RANGE_PX 10 // attacker melee reach (world units)
#define COMBAT_RANGE_SQ (COMBAT_RANGE_PX * COMBAT_RANGE_PX)
#define TURRET_RANGE 40 // defenders shoot: ~2.5 tiles (3x3 hash cells caps this at 48)
#define TURRET_RANGE_SQ (TURRET_RANGE * TURRET_RANGE)
#define COMBAT_DAMAGE 2
#define CORE_HP_START 60
#define CORE_DAMAGE_PER_HIT 2

// Multi-cell-deep spawn band, not a single pixel row/column -- see Phase
// 10's commit for why (collapsing thousands of entities into one row of
// spatial-hash cells is the pathological density case
// engine/spatial_hash.h documents).
#define EDGE_BAND_CELLS 4
#define EDGE_BAND_PX (EDGE_BAND_CELLS * SPATIAL_HASH_CELL_SIZE)

static int32_t g_core_hp;
static uint32_t g_rng_state = 12345;
static uint64_t g_last_steer_cycles;
static uint64_t g_last_combat_cycles;

static uint32_t next_rand(void) {
    g_rng_state = g_rng_state * 1103515245u + 12345u;
    return (g_rng_state >> 16) & 0x7fff;
}

static void spawn_attackers(void) {
    for (int i = 0; i < NUM_ATTACKERS; i++) {
        int32_t x, y;
        switch (i % 4) {
        case 0: // near north edge
            x = (int32_t)(next_rand() % WORLD_UNITS_W) << 16;
            y = (int32_t)(next_rand() % EDGE_BAND_PX) << 16;
            break;
        case 1: // near south edge
            x = (int32_t)(next_rand() % WORLD_UNITS_W) << 16;
            y = (int32_t)(WORLD_UNITS_H - 1 - (int32_t)(next_rand() % EDGE_BAND_PX)) << 16;
            break;
        case 2: // near west edge
            x = (int32_t)(next_rand() % EDGE_BAND_PX) << 16;
            y = (int32_t)(next_rand() % WORLD_UNITS_H) << 16;
            break;
        default: // near east edge
            x = (int32_t)(WORLD_UNITS_W - 1 - (int32_t)(next_rand() % EDGE_BAND_PX)) << 16;
            y = (int32_t)(next_rand() % WORLD_UNITS_H) << 16;
            break;
        }
        entity_spawn(x, y, 0, 0, ATTACKER_HP, ENTITY_TYPE_ATTACKER);
    }
}

static void spawn_turret_defenders(void) {
    for (int gy = 0; gy < WORLD_H; gy++) {
        for (int gx = 0; gx < WORLD_W; gx++) {
            if (world_tile[gy][gx] != CITY_TURRET) {
                continue;
            }
            int32_t x = (int32_t)(gx * FLOWFIELD_CELL_SIZE + FLOWFIELD_CELL_SIZE / 2) << 16;
            int32_t y = (int32_t)(gy * FLOWFIELD_CELL_SIZE + FLOWFIELD_CELL_SIZE / 2) << 16;
            entity_spawn(x, y, 0, 0, DEFENDER_HP, ENTITY_TYPE_DEFENDER);
        }
    }
}

// Attackers path around anything solid; roads and open ground are free.
static void build_cost_from_city(void) {
    for (int gy = 0; gy < WORLD_H; gy++) {
        for (int gx = 0; gx < WORLD_W; gx++) {
            uint8_t t = world_tile[gy][gx];
            flowfield_cost[gy][gx] =
                (t == CITY_HOUSE || t == CITY_BARRICADE || t == CITY_TURRET) ? 255 : 0;
        }
    }
}

void siege_phase_start(void) {
    entity_soa_init();
    build_cost_from_city();
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
    int32_t d2 = dx * dx + dy * dy;
    // Asymmetric ranges: defenders (turrets) out-range attacker melee, so
    // walls of turrets actually defend instead of being pathed around.
    if (entity_type[a] == ENTITY_TYPE_ATTACKER) {
        if (d2 <= TURRET_RANGE_SQ) entity_hp[a] -= COMBAT_DAMAGE; // shot by turret
    } else {
        if (d2 <= COMBAT_RANGE_SQ) entity_hp[a] -= COMBAT_DAMAGE; // melee on turret
    }
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
    uint64_t t0 = perf_cycles();
    entity_steer();
    uint64_t t1 = perf_cycles();
    g_last_steer_cycles = t1 - t0;

    uint32_t attackers, defenders;
    siege_phase_counts(&attackers, &defenders);
    if (attackers > 0 && defenders > 0) {
        spatial_hash_build();
        spatial_hash_for_each_nearby_pair(combat_pair_callback, NULL);
    }
    g_last_combat_cycles = perf_cycles() - t1;

    resolve_deaths();
}

uint64_t siege_phase_last_steer_cycles(void) {
    return g_last_steer_cycles;
}

uint64_t siege_phase_last_combat_cycles(void) {
    return g_last_combat_cycles;
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
