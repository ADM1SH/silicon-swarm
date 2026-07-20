#include "engine/entity_soa.h"

int32_t entity_x[MAX_ENTITIES] __attribute__((aligned(64)));
int32_t entity_y[MAX_ENTITIES] __attribute__((aligned(64)));
int32_t entity_vx[MAX_ENTITIES] __attribute__((aligned(64)));
int32_t entity_vy[MAX_ENTITIES] __attribute__((aligned(64)));
int16_t entity_hp[MAX_ENTITIES] __attribute__((aligned(64)));
uint8_t entity_type[MAX_ENTITIES] __attribute__((aligned(64)));
uint8_t entity_alive[MAX_ENTITIES] __attribute__((aligned(64)));

uint32_t entity_count;

void entity_soa_init(void) {
    entity_count = 0;
    for (uint32_t i = 0; i < MAX_ENTITIES; i++) {
        entity_alive[i] = 0;
    }
}

int entity_spawn(int32_t x, int32_t y, int32_t vx, int32_t vy, int16_t hp, uint8_t type) {
    if (entity_count >= MAX_ENTITIES) {
        return -1;
    }
    uint32_t i = entity_count++;
    entity_x[i] = x;
    entity_y[i] = y;
    entity_vx[i] = vx;
    entity_vy[i] = vy;
    entity_hp[i] = hp;
    entity_type[i] = type;
    entity_alive[i] = 1;
    return (int)i;
}

void entity_update_all(void) {
    for (uint32_t i = 0; i < entity_count; i++) {
        if (!entity_alive[i]) {
            continue;
        }
        entity_x[i] += entity_vx[i];
        entity_y[i] += entity_vy[i];
    }
}
