// v4 city simulation. All integer math (bare metal, no FPU dependency in
// the sim), everything O(map) once per second — not per frame.
#include "game/city.h"
#include "engine/terrain.h"

uint8_t world_tile[WORLD_H][WORLD_W];
int32_t city_money;
int32_t city_pop;
int32_t city_food;
int8_t city_tax[3];
int32_t city_demand[3];

static uint8_t g_growth[WORLD_H][WORLD_W]; // 0..GROW_DONE growth timer
static uint8_t g_value[WORLD_H][WORLD_W];  // cached land value 0..63

#define GROW_DONE 60        // seconds of accumulated growth points to build
#define FOOD_CAP 999
#define ZONE_COST 2

static const int16_t cost_of[CITY_TILE_COUNT] = {
    [CITY_ROAD] = 5,  [CITY_AVENUE] = 15,   [CITY_BARRICADE] = 10,
    [CITY_TURRET] = 50, [CITY_WATCHTOWER] = 40, [CITY_GRANARY] = 60,
    [CITY_BARRACKS] = 80, [CITY_ZONE_R] = ZONE_COST, [CITY_ZONE_C] = ZONE_COST,
    [CITY_ZONE_I] = ZONE_COST,
};

// Residential capacity / commercial jobs / industrial jobs per tier.
static const int16_t r_cap[3] = {4, 12, 32};
static const int16_t c_jobs[3] = {3, 8, 20};
static const int16_t i_jobs[3] = {5, 14, 36};

void city_init(void) {
    for (int gy = 0; gy < WORLD_H; gy++) {
        for (int gx = 0; gx < WORLD_W; gx++) {
            world_tile[gy][gx] = CITY_EMPTY;
            g_growth[gy][gx] = 0;
            g_value[gy][gx] = 0;
        }
    }
    world_tile[CITY_CORE_GY][CITY_CORE_GX] = CITY_CORE;
    city_money = 500;
    city_pop = 0;
    city_food = 0;
    city_tax[0] = city_tax[1] = city_tax[2] = 9;
    city_demand[0] = city_demand[1] = city_demand[2] = 0;
}

static int tile_is_flat(int gx, int gy) {
    int h = world_height[gy][gx];
    return world_height[gy][gx + 1] == h && world_height[gy + 1][gx] == h &&
           world_height[gy + 1][gx + 1] == h;
}

static int road_adjacent(int gx, int gy) {
    for (int k = 0; k < 4; k++) {
        int nx = gx + (k == 0) - (k == 1), ny = gy + (k == 2) - (k == 3);
        if ((unsigned)nx < WORLD_W && (unsigned)ny < WORLD_H) {
            uint8_t t = world_tile[ny][nx];
            if (t == CITY_ROAD || t == CITY_AVENUE) {
                return 1;
            }
        }
    }
    return 0;
}

int city_place(int gx, int gy, city_tile_t type) {
    if ((unsigned)gx >= WORLD_W || (unsigned)gy >= WORLD_H) {
        return 0;
    }
    if (world_tile[gy][gx] != CITY_EMPTY || !tile_is_flat(gx, gy) ||
        terrain_tile_underwater(gx, gy)) {
        return 0;
    }
    if (type <= CITY_EMPTY || type > CITY_ZONE_I || type == CITY_CORE) {
        return 0;
    }
    if (city_money < cost_of[type]) {
        return 0;
    }
    city_money -= cost_of[type];
    world_tile[gy][gx] = (uint8_t)type;
    g_growth[gy][gx] = 0;
    return 1;
}

int city_demolish(int gx, int gy) {
    if ((unsigned)gx >= WORLD_W || (unsigned)gy >= WORLD_H) {
        return 0;
    }
    uint8_t t = world_tile[gy][gx];
    if (t == CITY_EMPTY || t == CITY_CORE) {
        return 0;
    }
    if (t >= CITY_R1) { // grown building: revert to its zone, no refund
        world_tile[gy][gx] = (uint8_t)(t >= CITY_I1   ? CITY_ZONE_I
                                       : t >= CITY_C1 ? CITY_ZONE_C
                                                      : CITY_ZONE_R);
    } else {
        city_money += cost_of[t] / 2;
        world_tile[gy][gx] = CITY_EMPTY;
    }
    g_growth[gy][gx] = 0;
    return 1;
}

// ---- land value ----
// Water nearby is good, rough terrain is bad, industry pollutes, services
// and avenues lift it. Cached per sim tick, zone/building tiles only.
static uint8_t land_value_of(int gx, int gy) {
    int v = 16;
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            int nx = gx + dx, ny = gy + dy;
            if ((unsigned)nx >= WORLD_W || (unsigned)ny >= WORLD_H) {
                continue;
            }
            if (terrain_tile_underwater(nx, ny)) {
                v += 12;
                dy = 4; // one waterfront bonus is enough
                break;
            }
        }
    }
    int h = world_height[gy][gx];
    int rough = 0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int nx = gx + dx, ny = gy + dy;
            if ((unsigned)nx <= WORLD_W && (unsigned)ny <= WORLD_H) {
                int d = world_height[ny][nx] - h;
                rough += d < 0 ? -d : d;
            }
        }
    }
    v -= rough / 8;
    for (int dy = -8; dy <= 8; dy++) {
        for (int dx = -8; dx <= 8; dx++) {
            int nx = gx + dx, ny = gy + dy;
            if ((unsigned)nx >= WORLD_W || (unsigned)ny >= WORLD_H) {
                continue;
            }
            uint8_t t = world_tile[ny][nx];
            int man = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (t == CITY_GRANARY && man <= 8) v += 8;
            else if (t == CITY_WATCHTOWER && man <= 6) v += 4;
            else if (t >= CITY_I1 && t <= CITY_I3 && man <= 2) v -= 10; // pollution
            else if (t == CITY_AVENUE && man <= 1) v += 4;
        }
    }
    if (v < 0) v = 0;
    if (v > 63) v = 63;
    return (uint8_t)v;
}

int city_land_value(int gx, int gy) {
    return g_value[gy][gx];
}

// Congestion: a packed neighborhood with no avenue nearby grows at half
// speed. ponytail: proxy for real traffic flow; per-segment flow if this
// ever needs to be deeper.
static int congested(int gx, int gy) {
    int built = 0, avenue = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = gx + dx, ny = gy + dy;
            if ((unsigned)nx >= WORLD_W || (unsigned)ny >= WORLD_H) {
                continue;
            }
            uint8_t t = world_tile[ny][nx];
            built += (t >= CITY_R1);
            avenue += (t == CITY_AVENUE);
        }
    }
    return built > 5 && !avenue;
}

// ---- the sim tick ----

static void tally(int32_t *housing, int32_t *cj, int32_t *ij) {
    *housing = 0;
    *cj = 0;
    *ij = 0;
    for (int gy = 0; gy < WORLD_H; gy++) {
        for (int gx = 0; gx < WORLD_W; gx++) {
            uint8_t t = world_tile[gy][gx];
            if (t >= CITY_R1 && t <= CITY_R3) *housing += r_cap[t - CITY_R1];
            else if (t >= CITY_C1 && t <= CITY_C3) *cj += c_jobs[t - CITY_C1];
            else if (t >= CITY_I1 && t <= CITY_I3) *ij += i_jobs[t - CITY_I1];
        }
    }
}

void city_sim_tick(void) {
    int32_t housing, cj, ij;
    tally(&housing, &cj, &ij);
    int32_t jobs = cj + ij + 8; // the core itself employs a few

    // Demand (CS-style meters), dampened by that class's tax rate.
    city_demand[0] = jobs * 3 / 2 + 10 - housing + (9 - city_tax[0]) * 2;
    city_demand[1] = city_pop / 4 - cj + (9 - city_tax[1]) * 2;
    city_demand[2] = city_pop / 3 - ij + (9 - city_tax[2]) * 2;

    // Population eases toward what housing and jobs support.
    int32_t target = housing < jobs * 2 + 10 ? housing : jobs * 2 + 10;
    city_pop += (target - city_pop) / 8 + ((target > city_pop) ? 1 : 0);
    if (city_pop < 0) city_pop = 0;

    // Taxes: people and jobs pay per their class rate.
    city_money += city_pop * city_tax[0] / 40 + cj * city_tax[1] / 12 +
                  ij * city_tax[2] / 12;

    // Growth + upgrades + food + land value, one pass.
    for (int gy = 0; gy < WORLD_H; gy++) {
        for (int gx = 0; gx < WORLD_W; gx++) {
            uint8_t t = world_tile[gy][gx];
            if (t == CITY_GRANARY && city_food < FOOD_CAP) {
                city_food++;
                continue;
            }
            int cls = -1, tier = 0;
            if (t >= CITY_ZONE_R && t <= CITY_ZONE_I) {
                cls = t - CITY_ZONE_R;
            } else if (t >= CITY_R1 && t <= CITY_I3) {
                cls = (t - CITY_R1) / 3;
                tier = (t - CITY_R1) % 3 + 1;
            }
            if (cls < 0) {
                continue;
            }
            g_value[gy][gx] = land_value_of(gx, gy);
            if (tier == 3 || city_demand[cls] <= 0 || !road_adjacent(gx, gy)) {
                continue; // maxed, no demand, or cut off
            }
            // Milestone + land-value gates for the NEXT tier.
            if (tier >= 1) {
                if (tier == 1 && (city_pop < POP_MILESTONE_T2 || g_value[gy][gx] < 20)) continue;
                if (tier == 2 && (city_pop < POP_MILESTONE_T3 || g_value[gy][gx] < 35)) continue;
            }
            int speed = 6 + g_value[gy][gx] / 8;
            if (congested(gx, gy)) {
                speed /= 2;
            }
            int g = g_growth[gy][gx] + speed;
            if (g < GROW_DONE) {
                g_growth[gy][gx] = (uint8_t)g;
                continue;
            }
            g_growth[gy][gx] = 0;
            if (tier == 0) {
                world_tile[gy][gx] = (uint8_t)(CITY_R1 + cls * 3); // zone -> tier 1
            } else {
                world_tile[gy][gx]++; // tier up
            }
        }
    }
}

// ---- rendering ----

static const struct {
    uint32_t color;
    int16_t ph;
} looks[CITY_TILE_COUNT] = {
    [CITY_ROAD] = {0x00555560, 0},
    [CITY_AVENUE] = {0x00787888, 0},
    [CITY_BARRICADE] = {0x00802020, 12},
    [CITY_TURRET] = {0x00FFD060, 24},
    [CITY_CORE] = {0x0040FF40, 32},
    [CITY_WATCHTOWER] = {0x00C8C8D8, 40},
    [CITY_GRANARY] = {0x00B08040, 18},
    [CITY_BARRACKS] = {0x00A04848, 22},
    [CITY_ZONE_R] = {0x00355A35, 0},
    [CITY_ZONE_C] = {0x002E4A66, 0},
    [CITY_ZONE_I] = {0x0066582E, 0},
    [CITY_R1] = {0x0078C078, 14}, [CITY_R2] = {0x0068B068, 22}, [CITY_R3] = {0x0058A058, 34},
    [CITY_C1] = {0x006898D8, 16}, [CITY_C2] = {0x005888C8, 26}, [CITY_C3] = {0x004878B8, 40},
    [CITY_I1] = {0x00D0A840, 12}, [CITY_I2] = {0x00C09830, 18}, [CITY_I3] = {0x00B08820, 26},
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
    int top_y = by - world_height[gy][gx] * ELEV_STEP;
    int ph = looks[t].ph;
    uint32_t c = looks[t].color;

    int ny = top_y, ey = top_y + TILE_H / 2, sy = top_y + TILE_H, wy = ey;
    int nx = bx, ex = bx + TILE_W / 2, sx = bx, wx = bx - TILE_W / 2;

    if (ph > 0) {
        uint32_t cse = darken(c, 10), csw = darken(c, 7);
        iso_fill_tri(ex, ey - ph, sx, sy - ph, sx, sy, cse);
        iso_fill_tri(ex, ey - ph, sx, sy, ex, ey, cse);
        iso_fill_tri(wx, wy - ph, sx, sy - ph, sx, sy, csw);
        iso_fill_tri(wx, wy - ph, sx, sy, wx, wy, csw);
    }
    iso_fill_tri(nx, ny - ph, ex, ey - ph, sx, sy - ph, c);
    iso_fill_tri(nx, ny - ph, sx, sy - ph, wx, wy - ph, c);
}
