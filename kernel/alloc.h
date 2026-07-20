#ifndef KERNEL_ALLOC_H
#define KERNEL_ALLOC_H

#include <stddef.h>

// Bump allocator over a static arena. No free() -- v1 never gives scratch
// memory back, so a freelist would be unused complexity.
void alloc_init(void);

// Returns `size` bytes aligned to `align` (must be a power of two), or
// NULL if the arena is exhausted.
void *bump_alloc(size_t size, size_t align);

#endif
