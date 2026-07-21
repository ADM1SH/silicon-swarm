#include "engine/spatial_hash.h"
#include "engine/entity_soa.h"
#include "kernel/alloc.h"

#define NUM_CELLS (SPATIAL_HASH_W * SPATIAL_HASH_H)

static uint32_t *g_cell_count;   // NUM_CELLS
static uint32_t *g_cell_start;   // NUM_CELLS
static uint32_t *g_cursor;       // NUM_CELLS scratch, reused each build
static uint32_t *g_entity_index; // MAX_ENTITIES, entities grouped by cell

static int cell_of(uint32_t i, int *out_gx, int *out_gy) {
    int gx = (int)(entity_x[i] >> 16) / SPATIAL_HASH_CELL_SIZE;
    int gy = (int)(entity_y[i] >> 16) / SPATIAL_HASH_CELL_SIZE;
    if ((unsigned)gx >= SPATIAL_HASH_W || (unsigned)gy >= SPATIAL_HASH_H) {
        return 0;
    }
    *out_gx = gx;
    *out_gy = gy;
    return 1;
}

void spatial_hash_init(void) {
    // 64B alignment (Phase 12), not sizeof(uint32_t) -- each array gets
    // its own cache line.
    g_cell_count = (uint32_t *)bump_alloc(NUM_CELLS * sizeof(uint32_t), 64);
    g_cell_start = (uint32_t *)bump_alloc(NUM_CELLS * sizeof(uint32_t), 64);
    g_cursor = (uint32_t *)bump_alloc(NUM_CELLS * sizeof(uint32_t), 64);
    g_entity_index = (uint32_t *)bump_alloc((size_t)MAX_ENTITIES * sizeof(uint32_t), 64);
}

void spatial_hash_build(void) {
    for (int c = 0; c < NUM_CELLS; c++) {
        g_cell_count[c] = 0;
    }
    for (uint32_t i = 0; i < entity_count; i++) {
        int gx, gy;
        if (!cell_of(i, &gx, &gy)) {
            continue;
        }
        g_cell_count[gy * SPATIAL_HASH_W + gx]++;
    }

    uint32_t offset = 0;
    for (int c = 0; c < NUM_CELLS; c++) {
        g_cell_start[c] = offset;
        g_cursor[c] = offset;
        offset += g_cell_count[c];
    }

    for (uint32_t i = 0; i < entity_count; i++) {
        int gx, gy;
        if (!cell_of(i, &gx, &gy)) {
            continue;
        }
        int c = gy * SPATIAL_HASH_W + gx;
        g_entity_index[g_cursor[c]++] = i;
    }
}

void spatial_hash_for_each_nearby_pair(spatial_hash_pair_fn cb, void *userdata) {
    for (uint32_t i = 0; i < entity_count; i++) {
        int gx, gy;
        if (!cell_of(i, &gx, &gy)) {
            continue;
        }
        for (int dy = -1; dy <= 1; dy++) {
            int ny = gy + dy;
            if ((unsigned)ny >= SPATIAL_HASH_H) {
                continue;
            }
            for (int dx = -1; dx <= 1; dx++) {
                int nx = gx + dx;
                if ((unsigned)nx >= SPATIAL_HASH_W) {
                    continue;
                }
                int c = ny * SPATIAL_HASH_W + nx;
                uint32_t start = g_cell_start[c];
                uint32_t end = start + g_cell_count[c];
                for (uint32_t k = start; k < end; k++) {
                    uint32_t j = g_entity_index[k];
                    if (j == i) {
                        continue;
                    }
                    cb(i, j, userdata);
                }
            }
        }
    }
}
