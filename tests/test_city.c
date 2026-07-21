// Host-side test for game/city.c: placement rules, money, demolish refund,
// road-adjacency income.
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
    assert(city_money == 200);

    int gx = CITY_CORE_GX + 2, gy = CITY_CORE_GY; // plateau: flat, empty

    // Occupied rejection.
    assert(!city_place(CITY_CORE_GX, CITY_CORE_GY, CITY_ROAD));

    // Road + house economy.
    assert(city_place(gx, gy, CITY_ROAD));
    assert(city_money == 200 - COST_ROAD);
    assert(city_place(gx + 1, gy, CITY_HOUSE));
    assert(city_money == 200 - COST_ROAD - COST_HOUSE);
    city_income_tick();
    assert(city_money == 200 - COST_ROAD - COST_HOUSE + HOUSE_INCOME);

    // Isolated house earns nothing.
    assert(city_place(gx + 4, gy + 4, CITY_HOUSE));
    int32_t before = city_money;
    city_income_tick();
    assert(city_money == before + HOUSE_INCOME); // still only the connected one

    // Demolish refunds half; core cannot be demolished.
    assert(city_demolish(gx + 4, gy + 4));
    assert(city_money == before + HOUSE_INCOME + COST_HOUSE / 2);
    assert(!city_demolish(CITY_CORE_GX, CITY_CORE_GY));

    // Broke: drain money, expensive place must fail.
    while (city_money >= COST_TURRET) {
        assert(city_place(gx, gy + 2, CITY_TURRET));
        assert(city_demolish(gx, gy + 2)); // refund half, net drain
    }
    assert(!city_place(gx, gy + 2, CITY_TURRET));

    // Overlay render with buildings doesn't crash.
    terrain_render(-640, 300, gx, gy, city_draw_tile);

    printf("test_city: OK\n");
    return 0;
}
