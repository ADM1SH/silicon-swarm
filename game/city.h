// v2 city layer: tile map + money on top of the terrain heightmap.
// Buildings need flat ground (all 4 corners equal) — terraform first,
// RCT-style. Houses only earn income while orthogonally adjacent to a road
// (the Cities: Skylines part).
#ifndef GAME_CITY_H
#define GAME_CITY_H

#include <stdint.h>
#include "engine/terrain.h"

typedef enum {
    CITY_EMPTY = 0,
    CITY_ROAD,
    CITY_HOUSE,
    CITY_BARRICADE,
    CITY_TURRET,
    CITY_CORE,
} city_tile_t;

extern uint8_t world_tile[WORLD_H][WORLD_W];
extern int32_t city_money;

#define CITY_CORE_GX (WORLD_W / 2)
#define CITY_CORE_GY (WORLD_H / 2)

// Costs (place) / half refunded on demolish.
#define COST_ROAD 5
#define COST_HOUSE 20
#define COST_BARRICADE 10
#define COST_TURRET 50
#define HOUSE_INCOME 2 // per second per road-connected house

void city_init(void);
// 1 on success; 0 if occupied, not flat, out of bounds, or unaffordable.
int city_place(int gx, int gy, city_tile_t type);
// 1 if something was demolished (refunds half cost). The core is immortal.
int city_demolish(int gx, int gy);
// Call once per second: pays HOUSE_INCOME per road-adjacent house.
void city_income_tick(void);

// terrain_render overlay: draws the building prism on tile (gx, gy).
// bx/by = projected base-point of the tile's north corner (no elevation).
void city_draw_tile(int gx, int gy, int bx, int by);

#endif
