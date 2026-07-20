#ifndef ENGINE_ENTITY_SOA_H
#define ENGINE_ENTITY_SOA_H

#include <stdint.h>

#define MAX_ENTITIES 1200000

// Q16.16 fixed-point: bits [31:16] are the integer part, [15:0] the
// fraction. 1 world unit == 1 framebuffer pixel, so straight-line motion
// can move sub-pixel amounts per tick without touching the FPU (README's
// fixed-point mandate for hot-path math).
extern int32_t entity_x[MAX_ENTITIES];
extern int32_t entity_y[MAX_ENTITIES];
extern int32_t entity_vx[MAX_ENTITIES];
extern int32_t entity_vy[MAX_ENTITIES];
extern int16_t entity_hp[MAX_ENTITIES];
extern uint8_t entity_type[MAX_ENTITIES];
extern uint8_t entity_alive[MAX_ENTITIES];

// Number of slots spawned so far. v1 is forward-only (no freelist/reuse) --
// nothing dies yet, so there's nothing to reclaim.
extern uint32_t entity_count;

void entity_soa_init(void);

// Returns the new entity's index, or -1 if MAX_ENTITIES is exhausted.
int entity_spawn(int32_t x, int32_t y, int32_t vx, int32_t vy, int16_t hp, uint8_t type);

// x/y += vx/vy for every alive entity. No bounds or collision handling --
// that's Phase 8 (flow field) and Phase 10 (spatial hash).
void entity_update_all(void);

#endif
