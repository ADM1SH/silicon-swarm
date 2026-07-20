#ifndef ENGINE_FLOWFIELD_H
#define ENGINE_FLOWFIELD_H

#include <stdint.h>

#include "kernel/framebuffer.h" // FB_WIDTH/FB_HEIGHT -- grid dims derive from screen size

#define FLOWFIELD_CELL_SIZE 16 // px per cell; power of two so px->cell is a shift
#define FLOWFIELD_W (FB_WIDTH / FLOWFIELD_CELL_SIZE)
#define FLOWFIELD_H (FB_HEIGHT / FLOWFIELD_CELL_SIZE)

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
