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
#define WATER_LEVEL 3 // tiles whose avg corner height is below this are water

extern uint8_t world_height[WORLD_H + 1][WORLD_W + 1];

void terrain_init(void);

// cur_gx/cur_gy: tile to draw highlighted as the cursor (-1,-1 for none).
// tile_overlay (may be NULL): called per visible tile right after its
// terrain quad, in painter's order, with the projected base point of the
// tile's north corner (bx, by; no elevation applied) — for buildings etc.
void terrain_render(int cam_x, int cam_y, int cur_gx, int cur_gy,
                    void (*tile_overlay)(int gx, int gy, int bx, int by));

// Flat-shaded triangle into the framebuffer (screen px, any winding).
void iso_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);

// Relax the heightmap so orthogonally adjacent corners differ by <= 1 unit
// (the RCT slope rule). Call after any bulk height edit.
void terrain_enforce_slope(void);

// RCT terraforming: shift tile (gx, gy)'s four corners by delta (+1/-1),
// clamped to [0, MAX_HEIGHT], propagating so the slope rule still holds.
void terrain_edit_tile(int gx, int gy, int delta);

// True if the tile's average corner height is below WATER_LEVEL. Water
// blocks building and attacker pathing — dig moats.
int terrain_tile_underwater(int gx, int gy);

// 4-way view rotation (0..3, 90-degree steps). The world data never moves;
// rendering iterates view space and maps back to world coordinates.
void terrain_set_rotation(int rot);
int terrain_get_rotation(void);
// World entity units (16/tile) -> view units under the current rotation.
void terrain_world_to_view_units(int wux, int wuy, int *vux, int *vuy);
