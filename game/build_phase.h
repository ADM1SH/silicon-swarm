#ifndef GAME_BUILD_PHASE_H
#define GAME_BUILD_PHASE_H

#include <stdint.h>

#include "engine/flowfield.h" // FLOWFIELD_W/H -- city_grid uses the same cells

#define BUILD_GRID_W FLOWFIELD_W
#define BUILD_GRID_H FLOWFIELD_H

typedef enum {
    TILE_EMPTY = 0,
    TILE_BARRICADE, // impassable -- mirrored into flowfield_cost
    TILE_TURRET,    // becomes a defender entity when the siege starts
    TILE_CORE,       // the city center -- occupied so nothing can be built over it
} tile_type_t;

// Canonical tile state. flowfield_cost (engine/flowfield.h) is kept in
// sync with TILE_BARRICADE cells by build_phase_place() -- city_grid is
// the single source of truth for what's built, not a second copy of it.
extern uint8_t city_grid[BUILD_GRID_H][BUILD_GRID_W];

void build_phase_init(void);

// Places `tool` at (gx, gy). Returns 1 on success, 0 if out of bounds or
// the cell is already occupied -- the caller decides what "occupied"
// means for the target/city-center cell (build_phase has no opinion on
// game-specific protected cells).
int build_phase_place(int gx, int gy, tile_type_t tool);

#endif
