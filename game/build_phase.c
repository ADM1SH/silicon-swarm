#include "game/build_phase.h"
#include "engine/flowfield.h"

uint8_t city_grid[BUILD_GRID_H][BUILD_GRID_W];

void build_phase_init(void) {
    for (int gy = 0; gy < BUILD_GRID_H; gy++) {
        for (int gx = 0; gx < BUILD_GRID_W; gx++) {
            city_grid[gy][gx] = TILE_EMPTY;
        }
    }
}

int build_phase_place(int gx, int gy, tile_type_t tool) {
    if ((unsigned)gx >= BUILD_GRID_W || (unsigned)gy >= BUILD_GRID_H) {
        return 0;
    }
    if (city_grid[gy][gx] != TILE_EMPTY) {
        return 0;
    }
    city_grid[gy][gx] = (uint8_t)tool;
    flowfield_cost[gy][gx] = (tool == TILE_BARRICADE) ? 255 : 0;
    return 1;
}
