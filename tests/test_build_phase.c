// Host-side unit test (no QEMU) -- see tests/test_entity_soa.c.
#include <assert.h>
#include <stdio.h>

#include "engine/flowfield.h"
#include "game/build_phase.h"

int main(void) {
    build_phase_init();
    assert(city_grid[0][0] == TILE_EMPTY);

    assert(build_phase_place(5, 5, TILE_BARRICADE) == 1);
    assert(city_grid[5][5] == TILE_BARRICADE);
    assert(flowfield_cost[5][5] == 255); // barricade is impassable

    assert(build_phase_place(6, 6, TILE_TURRET) == 1);
    assert(city_grid[6][6] == TILE_TURRET);
    assert(flowfield_cost[6][6] == 0); // turret doesn't block pathing

    assert(build_phase_place(5, 5, TILE_TURRET) == 0); // already occupied
    assert(city_grid[5][5] == TILE_BARRICADE);          // unchanged by the rejected placement

    assert(build_phase_place(-1, 5, TILE_BARRICADE) == 0);           // out of bounds
    assert(build_phase_place(5, BUILD_GRID_H, TILE_BARRICADE) == 0); // out of bounds

    printf("test_build_phase: OK\n");
    return 0;
}
