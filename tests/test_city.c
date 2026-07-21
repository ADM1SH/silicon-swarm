// Host-side test for the v4 city sim: zoning growth, demand, taxes,
// density milestones, demolish semantics, land value bounds.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "engine/terrain.h"
#include "game/city.h"
#include "kernel/framebuffer.h"

static uint32_t fb[FB_WIDTH * FB_HEIGHT];
uint32_t *framebuffer_pixels(void) { return fb; }
void neon_fill32(uint32_t *dst, uint32_t value, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) dst[i] = value;
}

int main(void) {
    terrain_init();
    city_init();
    assert(world_tile[CITY_CORE_GY][CITY_CORE_GX] == CITY_CORE);
    assert(city_money == 500 && city_pop == 0);

    int gx = CITY_CORE_GX + 2, gy = CITY_CORE_GY; // plateau: flat, dry

    // Paint a road and an R zone next to it.
    assert(city_place(gx, gy, CITY_ROAD));
    assert(city_place(gx + 1, gy, CITY_ZONE_R));
    assert(!city_place(gx + 1, gy, CITY_ZONE_C)); // occupied

    // With base jobs and no housing, R demand is positive and the zone
    // grows into a tier-1 building within a bounded number of ticks.
    int grew = 0;
    for (int t = 0; t < 30 && !grew; t++) {
        city_sim_tick();
        grew = (world_tile[gy][gx + 1] == CITY_R1);
    }
    assert(grew);
    assert(city_demand[0] != 0 || city_demand[1] != 0 || city_demand[2] != 0);

    // Population rises toward housing; taxes pay something eventually.
    for (int t = 0; t < 20; t++) city_sim_tick();
    assert(city_pop > 0);

    // Density milestone: below POP_MILESTONE_T2 a tier-1 building never
    // upgrades no matter how long we wait.
    assert(city_pop < POP_MILESTONE_T2);
    for (int t = 0; t < 100; t++) city_sim_tick();
    assert(world_tile[gy][gx + 1] == CITY_R1);

    // Zones need flat dry land.
    for (int i = 0; i < MAX_HEIGHT; i++) terrain_edit_tile(gx + 20, gy + 20, -1);
    assert(!city_place(gx + 20, gy + 20, CITY_ZONE_R)); // underwater

    // Demolish: building reverts to zone (no refund), zone clears.
    int32_t before = city_money;
    assert(city_demolish(gx + 1, gy));
    assert(world_tile[gy][gx + 1] == CITY_ZONE_R && city_money == before);
    assert(city_demolish(gx + 1, gy));
    assert(world_tile[gy][gx + 1] == CITY_EMPTY);
    assert(!city_demolish(CITY_CORE_GX, CITY_CORE_GY)); // core immortal

    // Granary stockpiles food; land value stays in range.
    assert(city_place(gx, gy + 2, CITY_GRANARY));
    int32_t food0 = city_food;
    city_sim_tick();
    assert(city_food == food0 + 1);
    int lv = city_land_value(gx + 1, gy);
    assert(lv >= 0 && lv <= 63);

    // Tax bounds are the caller's job (kmain clamps 0..20); sim just uses
    // them — a high tax rate lowers demand versus a low one.
    city_tax[0] = 20;
    city_sim_tick();
    int32_t hi_tax_dem = city_demand[0];
    city_tax[0] = 0;
    city_sim_tick();
    assert(city_demand[0] > hi_tax_dem);

    // Render overlay with sim content doesn't crash.
    terrain_render(-640, 300, gx, gy, city_draw_tile);

    printf("test_city: OK\n");
    return 0;
}
