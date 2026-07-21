#ifndef ENGINE_SPATIAL_HASH_H
#define ENGINE_SPATIAL_HASH_H

#include <stdint.h>

#include "engine/terrain.h" // v2: hash cells cover the world tile grid

#define SPATIAL_HASH_CELL_SIZE 16 // entity units per cell; power of two
#define SPATIAL_HASH_W WORLD_W
#define SPATIAL_HASH_H WORLD_H

// Allocates bucket bookkeeping from the bump allocator (kernel/alloc.h) --
// call once, after alloc_init().
void spatial_hash_init(void);

// Buckets every entity in [0, entity_count) by its current cell via a
// counting sort (O(n + cells), no per-cell dynamic allocation). Call once
// per tick before querying, not per-entity -- rebuild is O(n), not the
// per-query cost.
void spatial_hash_build(void);

// Calls cb(a, b, userdata) once for every ordered pair (a, b) where b is in
// a's cell or one of its 8 neighbors (3x3 neighborhood), a != b. This is an
// upper bound on "close enough to interact" (same-cell/neighbor-cell, not
// an exact radius) -- callers that need a precise distance check re-check
// it themselves. Both (a, b) and (b, a) get visited, so a callback that
// applies an effect only to its first argument naturally ends up applying
// it once per entity per tick, not twice.
//
// Cost is O(sum of (bucket occupancy)^2 over all buckets), which is O(n)
// for roughly-uniform entity density -- the thing this phase exists to
// avoid is a naive O(n^2) all-pairs scan. A pathological case (every
// entity crammed into one cell) degrades toward that same O(n^2); a real
// game addresses that with cell-size tuning or capacity limits, out of
// scope for v1.
typedef void (*spatial_hash_pair_fn)(uint32_t a, uint32_t b, void *userdata);
void spatial_hash_for_each_nearby_pair(spatial_hash_pair_fn cb, void *userdata);

// Read-only view of one cell's bucket from the last build: *ids points at
// the grouped entity-index array, *count entries. For painter's-order
// entity rendering (draw each tile's entities with that tile).
void spatial_hash_cell_entities(int gx, int gy, const uint32_t **ids, uint32_t *count);

#endif
