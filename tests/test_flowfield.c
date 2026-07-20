// Host-side unit test (no QEMU) -- see tests/test_entity_soa.c. Exercises
// BFS correctness with and without obstacles, since that's exactly what
// Phase 8's "path around a wall" behavior depends on.
#include <assert.h>
#include <stdio.h>

#include "engine/flowfield.h"
#include "kernel/alloc.h"

int main(void) {
    alloc_init();
    flowfield_init();

    // Open grid: every step from a far corner toward (0,0) must make
    // forward progress and terminate.
    flowfield_build(0, 0);
    int gx = FLOWFIELD_W - 1, gy = FLOWFIELD_H - 1;
    int steps = 0;
    while (!(gx == 0 && gy == 0)) {
        int nx, ny;
        int ok = flowfield_step(gx, gy, &nx, &ny);
        assert(ok);
        assert(nx != gx || ny != gy);
        gx = nx;
        gy = ny;
        steps++;
        assert(steps <= FLOWFIELD_W + FLOWFIELD_H); // must terminate
    }

    // Full wall: block an entire column -- the far side becomes unreachable.
    for (int y = 0; y < FLOWFIELD_H; y++) {
        flowfield_cost[y][5] = 255;
    }
    flowfield_build(0, 0);
    int out_gx, out_gy;
    assert(flowfield_step(FLOWFIELD_W - 1, 0, &out_gx, &out_gy) == 0);

    // Gap in the wall: far side becomes reachable again.
    flowfield_cost[FLOWFIELD_H - 1][5] = 0;
    flowfield_build(0, 0);
    assert(flowfield_step(FLOWFIELD_W - 1, 0, &out_gx, &out_gy) == 1);

    printf("test_flowfield: OK\n");
    return 0;
}
