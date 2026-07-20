// Host-side unit test (no QEMU) -- see tests/test_entity_soa.c.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/alloc.h"

int main(void) {
    alloc_init();

    void *a = bump_alloc(10, 4);
    void *b = bump_alloc(10, 4);
    assert(a != (void *)0);
    assert(b != (void *)0);
    assert(((uintptr_t)a % 4) == 0);
    assert(((uintptr_t)b % 4) == 0);
    assert((uintptr_t)b >= (uintptr_t)a + 10);

    void *aligned64 = bump_alloc(1, 64);
    assert(((uintptr_t)aligned64 % 64) == 0);

    void *huge = bump_alloc(32u * 1024 * 1024, 4); // bigger than the whole arena
    assert(huge == (void *)0);

    printf("test_alloc: OK\n");
    return 0;
}
