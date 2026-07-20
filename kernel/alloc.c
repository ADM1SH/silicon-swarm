#include "kernel/alloc.h"

#define ARENA_SIZE (1u * 1024 * 1024) // 1MB scratch; bump if a later phase needs more

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
