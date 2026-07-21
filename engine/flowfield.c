#include "engine/flowfield.h"
#include "kernel/alloc.h"

#define UNREACHABLE 0xffffu

uint8_t flowfield_cost[FLOWFIELD_H][FLOWFIELD_W];
static uint16_t g_dist[FLOWFIELD_H][FLOWFIELD_W];
static uint16_t *g_queue; // FLOWFIELD_W*FLOWFIELD_H cells, from the bump allocator

static const int NEIGHBOR_DX[4] = {0, 0, -1, 1};
static const int NEIGHBOR_DY[4] = {-1, 1, 0, 0};

void flowfield_init(void) {
    // 64B alignment (Phase 12), not sizeof(uint16_t) -- every major
    // engine array gets its own cache line, not just the ones declared
    // with __attribute__((aligned(64))) directly (entity_soa.c's arrays).
    g_queue = (uint16_t *)bump_alloc((size_t)FLOWFIELD_W * FLOWFIELD_H * sizeof(uint16_t), 64);
    for (int gy = 0; gy < FLOWFIELD_H; gy++) {
        for (int gx = 0; gx < FLOWFIELD_W; gx++) {
            flowfield_cost[gy][gx] = 0;
        }
    }
}

void flowfield_build(int target_gx, int target_gy) {
    for (int gy = 0; gy < FLOWFIELD_H; gy++) {
        for (int gx = 0; gx < FLOWFIELD_W; gx++) {
            g_dist[gy][gx] = UNREACHABLE;
        }
    }
    if ((unsigned)target_gx >= FLOWFIELD_W || (unsigned)target_gy >= FLOWFIELD_H ||
        flowfield_cost[target_gy][target_gx] == 255) {
        return; // no valid target -- every cell stays unreachable
    }

    uint32_t head = 0, tail = 0;
    g_dist[target_gy][target_gx] = 0;
    g_queue[tail++] = (uint16_t)(target_gy * FLOWFIELD_W + target_gx);

    while (head < tail) {
        uint16_t idx = g_queue[head++];
        int gx = idx % FLOWFIELD_W;
        int gy = idx / FLOWFIELD_W;
        uint16_t d = g_dist[gy][gx];

        for (int i = 0; i < 4; i++) {
            int nx = gx + NEIGHBOR_DX[i];
            int ny = gy + NEIGHBOR_DY[i];
            if ((unsigned)nx >= FLOWFIELD_W || (unsigned)ny >= FLOWFIELD_H) {
                continue;
            }
            if (flowfield_cost[ny][nx] == 255 || g_dist[ny][nx] != UNREACHABLE) {
                continue;
            }
            g_dist[ny][nx] = (uint16_t)(d + 1);
            g_queue[tail++] = (uint16_t)(ny * FLOWFIELD_W + nx);
        }
    }
}

int flowfield_step(int gx, int gy, int *out_gx, int *out_gy) {
    if ((unsigned)gx >= FLOWFIELD_W || (unsigned)gy >= FLOWFIELD_H) {
        return 0;
    }
    uint16_t best = g_dist[gy][gx];
    if (best == UNREACHABLE) {
        return 0;
    }
    int best_gx = gx, best_gy = gy;

    for (int i = 0; i < 4; i++) {
        int nx = gx + NEIGHBOR_DX[i];
        int ny = gy + NEIGHBOR_DY[i];
        if ((unsigned)nx >= FLOWFIELD_W || (unsigned)ny >= FLOWFIELD_H) {
            continue;
        }
        if (g_dist[ny][nx] < best) {
            best = g_dist[ny][nx];
            best_gx = nx;
            best_gy = ny;
        }
    }
    *out_gx = best_gx;
    *out_gy = best_gy;
    return 1;
}
