// Isometric heightmap terrain (v2). World is a 128x128 tile grid with
// heights stored at the 129x129 tile CORNERS (RCT-style: a tile's quad is
// its four corner heights, so slopes come out of the data for free).
//
// Projection: classic 2:1 diamond. Tile corner (cx, cy) at height h lands at
//   sx = (cx - cy) * (TILE_W/2)          - cam_x
//   sy = (cx + cy) * (TILE_H/2) - h*ELEV - cam_y
// cam_x/cam_y are in this projected pixel space.
#pragma once
#include <stdint.h>

#define WORLD_W 128
#define WORLD_H 128
#define TILE_W 32
#define TILE_H 16
#define ELEV_STEP 8
#define MAX_HEIGHT 15

extern uint8_t world_height[WORLD_H + 1][WORLD_W + 1];

void terrain_init(void);
void terrain_render(int cam_x, int cam_y);

// Relax the heightmap so orthogonally adjacent corners differ by <= 1 unit
// (the RCT slope rule). Call after any bulk height edit.
void terrain_enforce_slope(void);
