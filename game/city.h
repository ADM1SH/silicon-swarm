// v4 city simulation: Cities-Skylines-style zoning on top of the terrain.
// You paint R/C/I zones; buildings GROW when demand, road access, land
// value, and population milestones allow — you never place houses.
// Services are siege-flavored: watchtowers fight, granaries stockpile food
// that can save the city from a lost siege, barracks turn population into
// militia when the wave hits.
#ifndef GAME_CITY_H
#define GAME_CITY_H

#include <stdint.h>
#include "engine/terrain.h"

typedef enum {
    CITY_EMPTY = 0,
    // infrastructure / defense (placed directly)
    CITY_ROAD,       // $5
    CITY_AVENUE,     // $15 -- growth bonus, relieves congestion
    CITY_BARRICADE,  // $10
    CITY_TURRET,     // $50
    CITY_CORE,
    CITY_WATCHTOWER, // $40 -- fights in sieges, small land-value boost
    CITY_GRANARY,    // $60 -- +food/sec, boosts nearby C growth
    CITY_BARRACKS,   // $80 -- spawns militia from population at siege start
    // painted zones ($2/tile) -- empty until they grow
    CITY_ZONE_R,
    CITY_ZONE_C,
    CITY_ZONE_I,
    // grown buildings, 3 density tiers each
    CITY_R1, CITY_R2, CITY_R3,
    CITY_C1, CITY_C2, CITY_C3,
    CITY_I1, CITY_I2, CITY_I3,
    CITY_TILE_COUNT,
} city_tile_t;

extern uint8_t world_tile[WORLD_H][WORLD_W];
extern int32_t city_money;
extern int32_t city_pop;      // headline number
extern int32_t city_food;     // granary stockpile (capped)
extern int8_t city_tax[3];    // percent 0..20, indexed R=0, C=1, I=2
extern int32_t city_demand[3]; // current RCI demand (signed)

#define CITY_CORE_GX (WORLD_W / 2)
#define CITY_CORE_GY (WORLD_H / 2)

#define POP_MILESTONE_T2 50  // density tier 2 unlocks
#define POP_MILESTONE_T3 200 // density tier 3 unlocks

// Attackers path around anything solid; zones and roads are open ground.
static inline int city_tile_solid(uint8_t t) {
    return t >= CITY_BARRICADE && t != CITY_CORE && t != CITY_ZONE_R &&
           t != CITY_ZONE_C && t != CITY_ZONE_I;
}

void city_init(void);
int city_place(int gx, int gy, city_tile_t type); // 1 = placed/painted
int city_demolish(int gx, int gy);                // buildings revert to zone; zones clear
// One simulation step per second: demand, growth, upgrades, taxes, food.
void city_sim_tick(void);
void city_draw_tile(int gx, int gy, int bx, int by);
// Land value 0..63 of a tile (for tests/HUD probing).
int city_land_value(int gx, int gy);

#endif
