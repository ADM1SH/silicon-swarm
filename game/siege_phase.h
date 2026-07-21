#ifndef GAME_SIEGE_PHASE_H
#define GAME_SIEGE_PHASE_H

#include <stdint.h>

#include "engine/flowfield.h" // FLOWFIELD_W/H -- the target cell lives on this grid

// City center: the flow field's target, and the cell attackers are trying
// to reach. Same grid as game/build_phase.h's city_grid.
#define SIEGE_TARGET_GX (FLOWFIELD_W / 2)
#define SIEGE_TARGET_GY (FLOWFIELD_H / 2)

// Spawns one wave of attackers at the screen edges and a defender entity
// for every TILE_TURRET in game/build_phase.h's city_grid, rebuilds the
// flow field toward SIEGE_TARGET_GX/GY (city_grid's barricades may have
// changed flowfield_cost since the last build), and resets core HP. Call
// once when transitioning from build to siege.
void siege_phase_start(void);

// One simulation tick: attacker steering (flow field) plus defenders
// holding position, position update, an attacker reaching the target
// cell hits the core once and dies, then spatial-hash combat resolution
// (skipped whenever one side is already empty -- both for cost and
// because no combat is possible then; see Phase 10's commit for the bug
// this specifically fixes).
void siege_phase_tick(void);

void siege_phase_counts(uint32_t *out_attackers, uint32_t *out_defenders);
int32_t siege_phase_core_hp(void);

// Phase 12: core-clock cycles (kernel/perf.h) the last siege_phase_tick()
// spent in each phase, for identifying where the actual cost is (memory
// bandwidth vs. compute vs. spatial-hash overhead) at a given entity
// count. Both 0 before the first tick.
uint64_t siege_phase_last_steer_cycles(void);
uint64_t siege_phase_last_combat_cycles(void);

// True once the wave is fully resolved either way.
int siege_phase_is_won(void);  // no attackers left, core still standing
int siege_phase_is_lost(void); // core HP hit 0

#endif
