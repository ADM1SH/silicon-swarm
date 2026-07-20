#include "kernel/alloc.h"

// 16MB scratch. Phase 10's spatial hash is the biggest consumer:
// MAX_ENTITIES (engine/entity_soa.h) * 4 bytes for its entity-index array,
// currently ~4.8MB at MAX_ENTITIES=1,200,000 -- this must stay ahead of
// that as MAX_ENTITIES grows, or bump_alloc() silently returns NULL and
// the caller writes through it (exactly the segfault this comment
// replaced: the old 1MB arena was already too small for Phase 10).
#define ARENA_SIZE (16u * 1024 * 1024)

static unsigned char arena[ARENA_SIZE] __attribute__((aligned(64)));
static size_t offset;

void alloc_init(void) {
    offset = 0;
}

void *bump_alloc(size_t size, size_t align) {
    size_t aligned = (offset + (align - 1)) & ~(align - 1);
    if (aligned + size > ARENA_SIZE) {
        return (void *)0;
    }
    offset = aligned + size;
    return &arena[aligned];
}
