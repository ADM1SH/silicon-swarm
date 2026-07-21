#ifndef ENGINE_FLOWFIELD_H
#define ENGINE_FLOWFIELD_H

#include <stdint.h>

#include "engine/terrain.h" // v2: the flow field IS the world tile grid

#define FLOWFIELD_CELL_SIZE 16 // entity units per tile; power of two so unit->cell is a shift
#define FLOWFIELD_W WORLD_W
#define FLOWFIELD_H WORLD_H

// 0 = passable, 255 = impassable (wall/obstacle). Game code owns writing
// into this before calling flowfield_build().
extern uint8_t flowfield_cost[FLOWFIELD_H][FLOWFIELD_W];

// Allocates the BFS scratch queue from the bump allocator (kernel/alloc.h)
// -- call once, after alloc_init().
void flowfield_init(void);

// Wavefront/BFS distance from (target_gx, target_gy), respecting
// flowfield_cost. O(grid cells), independent of entity count -- call this
// once (or whenever walls change), never per-entity.
void flowfield_build(int target_gx, int target_gy);

// O(1) downhill lookup: the neighbor of (gx, gy) to step toward next, per
// the last flowfield_build(). Returns 0 (leaves *out_gx/*out_gy untouched)
// if (gx, gy) is out of bounds or unreachable from the target.
int flowfield_step(int gx, int gy, int *out_gx, int *out_gy);

#endif
