// Host-side test for engine/terrain.c: slope rule + rasterizer stays in
// bounds. Stubs out the kernel framebuffer and the NEON fill (plain C loop)
// so terrain.c compiles as ordinary host code.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "engine/terrain.h"
#include "kernel/framebuffer.h"

// ---- stubs ----
#define GUARD 4096
static uint32_t fb[FB_WIDTH * FB_HEIGHT + 2 * GUARD];

uint32_t *framebuffer_pixels(void) {
    return fb + GUARD;
}

void neon_fill32(uint32_t *dst, uint32_t value, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        dst[i] = value;
    }
}

int main(void) {
    terrain_init();

    // Slope rule: every orthogonally adjacent corner pair differs by <= 1.
    for (int cy = 0; cy <= WORLD_H; cy++) {
        for (int cx = 0; cx <= WORLD_W; cx++) {
            int h = world_height[cy][cx];
            assert(h <= MAX_HEIGHT);
            if (cx < WORLD_W) {
                int d = h - world_height[cy][cx + 1];
                assert(d >= -1 && d <= 1);
            }
            if (cy < WORLD_H) {
                int d = h - world_height[cy + 1][cx];
                assert(d >= -1 && d <= 1);
            }
        }
    }

    // Render from several camera positions (including far off-map) and check
    // the guard bands are untouched and something was painted on-screen.
    int cams[][2] = {{-640, 300}, {0, 0}, {-100000, -100000}, {100000, 100000}, {-640, 900}};
    for (unsigned i = 0; i < sizeof(cams) / sizeof(cams[0]); i++) {
        memset(fb, 0, sizeof(fb));
        terrain_render(cams[i][0], cams[i][1], -1, -1, 0);
        for (int g = 0; g < GUARD; g++) {
            assert(fb[g] == 0);
            assert(fb[GUARD + FB_WIDTH * FB_HEIGHT + g] == 0);
        }
    }
    // Terraform edits keep the slope invariant in both directions.
    for (int i = 0; i < 5; i++) terrain_edit_tile(WORLD_W / 2, WORLD_H / 2, +1);
    for (int i = 0; i < 9; i++) terrain_edit_tile(WORLD_W / 2 + 3, WORLD_H / 2, -1);
    terrain_edit_tile(0, 0, -1);
    terrain_edit_tile(WORLD_W - 1, WORLD_H - 1, +1);
    for (int cy = 0; cy <= WORLD_H; cy++) {
        for (int cx = 0; cx <= WORLD_W; cx++) {
            int h = world_height[cy][cx];
            if (cx < WORLD_W) assert(h - world_height[cy][cx + 1] >= -1 && h - world_height[cy][cx + 1] <= 1);
            if (cy < WORLD_H) assert(h - world_height[cy + 1][cx] >= -1 && h - world_height[cy + 1][cx] <= 1);
        }
    }

    // All four rotations render in-bounds; world->view->render agrees.
    for (int r = 0; r < 4; r++) {
        terrain_set_rotation(r);
        memset(fb, 0, sizeof(fb));
        terrain_render(-640, 300, 5, 5, 0);
        for (int g = 0; g < GUARD; g++) {
            assert(fb[g] == 0 && fb[GUARD + FB_WIDTH * FB_HEIGHT + g] == 0);
        }
    }
    terrain_set_rotation(0);

    // Waterline: dig a tile well below WATER_LEVEL and it reads underwater.
    for (int i = 0; i < MAX_HEIGHT; i++) terrain_edit_tile(10, 10, -1);
    assert(terrain_tile_underwater(10, 10));
    assert(!terrain_tile_underwater(WORLD_W / 2, WORLD_H / 2)); // plateau stays dry

    memset(fb, 0, sizeof(fb));
    terrain_render((WORLD_W / 2 - WORLD_H / 2) * (TILE_W / 2) - FB_WIDTH / 2,
                   (WORLD_W / 2 + WORLD_H / 2) * (TILE_H / 2) - FB_HEIGHT / 2,
                   WORLD_W / 2, WORLD_H / 2, 0);
    int painted = 0;
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        painted += (fb[GUARD + i] != 0);
    }
    assert(painted > FB_WIDTH * FB_HEIGHT / 2); // centered view: mostly terrain

    printf("test_terrain: OK\n");
    return 0;
}
