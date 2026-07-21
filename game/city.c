// City layer implementation. Rendering draws each building as an extruded
// prism over its tile: top diamond + the two camera-facing side faces
// (SW and SE in this projection), each flat-shaded darker than the top.
#include "game/city.h"
#include "engine/terrain.h"

uint8_t world_tile[WORLD_H][WORLD_W];
int32_t city_money;

static const int16_t cost_of[] = {0, COST_ROAD, COST_HOUSE, COST_BARRICADE, COST_TURRET, 0};

void city_init(void) {
    for (int gy = 0; gy < WORLD_H; gy++) {
        for (int gx = 0; gx < WORLD_W; gx++) {
            world_tile[gy][gx] = CITY_EMPTY;
        }
    }
    world_tile[CITY_CORE_GY][CITY_CORE_GX] = CITY_CORE;
    city_money = 200;
}

static int tile_is_flat(int gx, int gy) {
    int h = world_height[gy][gx];
    return world_height[gy][gx + 1] == h && world_height[gy + 1][gx] == h &&
           world_height[gy + 1][gx + 1] == h;
}

int city_place(int gx, int gy, city_tile_t type) {
    if (gx < 0 || gx >= WORLD_W || gy < 0 || gy >= WORLD_H) {
        return 0;
    }
    if (world_tile[gy][gx] != CITY_EMPTY || !tile_is_flat(gx, gy) ||
        terrain_tile_underwater(gx, gy)) {
        return 0;
    }
    if (type <= CITY_EMPTY || type >= CITY_CORE) {
        return 0;
    }
    if (city_money < cost_of[type]) {
        return 0;
    }
    city_money -= cost_of[type];
    world_tile[gy][gx] = (uint8_t)type;
    return 1;
}

int city_demolish(int gx, int gy) {
    if (gx < 0 || gx >= WORLD_W || gy < 0 || gy >= WORLD_H) {
        return 0;
    }
    uint8_t t = world_tile[gy][gx];
    if (t == CITY_EMPTY || t == CITY_CORE) {
        return 0;
    }
    city_money += cost_of[t] / 2;
    world_tile[gy][gx] = CITY_EMPTY;
    return 1;
}

static int road_adjacent(int gx, int gy) {
    return (gx > 0 && world_tile[gy][gx - 1] == CITY_ROAD) ||
           (gx < WORLD_W - 1 && world_tile[gy][gx + 1] == CITY_ROAD) ||
           (gy > 0 && world_tile[gy - 1][gx] == CITY_ROAD) ||
           (gy < WORLD_H - 1 && world_tile[gy + 1][gx] == CITY_ROAD);
}

void city_income_tick(void) {
    for (int gy = 0; gy < WORLD_H; gy++) {
        for (int gx = 0; gx < WORLD_W; gx++) {
            if (world_tile[gy][gx] == CITY_HOUSE && road_adjacent(gx, gy)) {
                city_money += HOUSE_INCOME;
            }
        }
    }
}

// ---- rendering ----

// {top, prism px height} per type; side faces derived by darkening top.
static const struct {
    uint32_t color;
    int16_t ph;
} looks[] = {
    [CITY_ROAD] = {0x00555560, 0},
    [CITY_HOUSE] = {0x00C8A878, 20},
    [CITY_BARRICADE] = {0x00802020, 12},
    [CITY_TURRET] = {0x00FFD060, 24},
    [CITY_CORE] = {0x0040FF40, 32},
};

static uint32_t darken(uint32_t c, int num) { // num/16ths of each channel
    uint32_t r = ((c >> 16) & 0xff) * (uint32_t)num / 16;
    uint32_t g = ((c >> 8) & 0xff) * (uint32_t)num / 16;
    uint32_t b = (c & 0xff) * (uint32_t)num / 16;
    return (r << 16) | (g << 8) | b;
}

void city_draw_tile(int gx, int gy, int bx, int by) {
    uint8_t t = world_tile[gy][gx];
    if (t == CITY_EMPTY) {
        return;
    }
    // Buildings only exist on flat tiles, so one height serves all corners.
    int top_y = by - world_height[gy][gx] * ELEV_STEP;
    int ph = looks[t].ph;
    uint32_t c = looks[t].color;

    // Ground-level diamond corners: N, E, S, W.
    int ny = top_y, ey = top_y + TILE_H / 2, sy = top_y + TILE_H, wy = ey;
    int nx = bx, ex = bx + TILE_W / 2, sx = bx, wx = bx - TILE_W / 2;

    if (ph > 0) {
        // Side faces first (they sit behind/below the top face).
        uint32_t cse = darken(c, 10), csw = darken(c, 7);
        iso_fill_tri(ex, ey - ph, sx, sy - ph, sx, sy, cse);
        iso_fill_tri(ex, ey - ph, sx, sy, ex, ey, cse);
        iso_fill_tri(wx, wy - ph, sx, sy - ph, sx, sy, csw);
        iso_fill_tri(wx, wy - ph, sx, sy, wx, wy, csw);
    }
    iso_fill_tri(nx, ny - ph, ex, ey - ph, sx, sy - ph, c);
    iso_fill_tri(nx, ny - ph, sx, sy - ph, wx, wy - ph, c);
}
