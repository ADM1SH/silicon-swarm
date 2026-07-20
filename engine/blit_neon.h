#ifndef ENGINE_BLIT_NEON_H
#define ENGINE_BLIT_NEON_H

#include <stdint.h>

// Vectorized (NEON, 4x int32 lanes/instruction) position update:
// x[i] += vx[i], y[i] += vy[i] for i in [0, count). No per-element alive
// check, unlike engine/entity_soa.c's entity_update_all() -- this is a raw
// bulk primitive for kmain's hot loop. entity_soa.c stays scalar and
// host-testable as the reference implementation (tests/test_entity_soa.c).
void entity_update_positions_neon(int32_t *x, int32_t *y,
                                   const int32_t *vx, const int32_t *vy,
                                   uint32_t count);

// Vectorized (NEON) 32-bit fill: dst[i] = value for i in [0, count).
void neon_fill32(uint32_t *dst, uint32_t value, uint32_t count);

#endif
