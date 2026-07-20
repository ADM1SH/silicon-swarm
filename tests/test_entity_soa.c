// Host-side unit test (no QEMU): entity_soa.c is pure freestanding-C logic
// with no MMIO, so it's testable directly on the build machine. Run via
// `make test-host`.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "engine/entity_soa.h"

int main(void) {
    entity_soa_init();
    assert(entity_count == 0);

    int a = entity_spawn(0, 0, 1 << 16, 0, 100, 1);              // +1.0 px/tick in x
    int b = entity_spawn(10 << 16, 10 << 16, 0, -(1 << 16), 50, 2); // -1.0 px/tick in y
    assert(a == 0 && b == 1);
    assert(entity_count == 2);
    assert(entity_alive[a] == 1 && entity_alive[b] == 1);

    entity_update_all();
    assert(entity_x[a] == (1 << 16));
    assert(entity_y[a] == 0);
    assert(entity_x[b] == (10 << 16));
    assert(entity_y[b] == (9 << 16));

    entity_alive[b] = 0; // kill b -- update_all must skip it
    int32_t before = entity_y[b];
    entity_update_all();
    assert(entity_y[b] == before);
    assert(entity_x[a] == (2 << 16)); // a keeps moving

    for (uint32_t i = entity_count; i < MAX_ENTITIES; i++) {
        int idx = entity_spawn(0, 0, 0, 0, 1, 0);
        assert(idx == (int)i);
    }
    assert(entity_spawn(0, 0, 0, 0, 1, 0) == -1); // arena full

    // entity_kill(): swap-and-pop compaction (Phase 10).
    entity_soa_init();
    int p = entity_spawn(1 << 16, 0, 0, 0, 1, 11); // will be killed
    int q = entity_spawn(2 << 16, 0, 0, 0, 1, 22); // becomes the new last slot after p dies
    int r = entity_spawn(3 << 16, 0, 0, 0, 1, 33); // starts as the last slot
    assert(p == 0 && q == 1 && r == 2);
    assert(entity_count == 3);

    entity_kill((uint32_t)p); // swaps r (the last entity) into p's slot
    assert(entity_count == 2);
    assert(entity_type[p] == 33); // r's data, not p's
    assert(entity_type[q] == 22); // untouched -- wasn't the last slot, and q is now the last slot

    entity_kill((uint32_t)(entity_count)); // out-of-range index: must be a no-op
    assert(entity_count == 2);

    entity_kill(entity_count - 1); // kills q (now the last slot) directly, no swap needed
    assert(entity_count == 1);
    assert(entity_type[0] == 33); // r's data, untouched by the no-swap case

    printf("test_entity_soa: OK\n");
    return 0;
}
