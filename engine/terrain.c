// Isometric terrain renderer (v2 Phase 1). See terrain.h for the projection.
//
// Draw order is painter's algorithm by diagonal (gx+gy ascending): tiles
// nearer the camera have a larger diagonal sum and are drawn later, so
// elevation overlaps resolve correctly with no z-buffer.
//
// Each tile is a quad of its four projected corners, split along the N-S
// screen diagonal into two triangles, scanline-rasterized into horizontal
// spans, each span filled by the NEON asm fill (engine/blit_neon.S). Flat
// shading per tile: base color ramps with height (grass -> rock), brightness
// tilts with the west-east slope (light from the west).
#include "engine/terrain.h"
#include "engine/blit_neon.h"
#include "kernel/framebuffer.h"

uint8_t world_height[WORLD_H + 1][WORLD_W + 1];

// Deterministic integer hash -> value noise; no libm, no rand on bare metal.
static uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Bilinear value noise: random heights on a coarse 16-corner lattice,
// interpolated between lattice points. Gives smooth rolling hills.
static int noise_height(int cx, int cy) {
    const int STEP = 16;
    int x0 = cx / STEP, y0 = cy / STEP;
    int fx = cx % STEP, fy = cy % STEP;
    int h00 = (int)(hash2(x0, y0) % (MAX_HEIGHT - 2));
    int h10 = (int)(hash2(x0 + 1, y0) % (MAX_HEIGHT - 2));
    int h01 = (int)(hash2(x0, y0 + 1) % (MAX_HEIGHT - 2));
    int h11 = (int)(hash2(x0 + 1, y0 + 1) % (MAX_HEIGHT - 2));
    int top = h00 * (STEP - fx) + h10 * fx;
    int bot = h01 * (STEP - fx) + h11 * fx;
    return (top * (STEP - fy) + bot * fy) / (STEP * STEP);
}

// RCT slope rule: orthogonally adjacent corners differ by at most 1 height
// unit. Steep edits/noise get relaxed into terraced slopes. This is also
// what guarantees a tile's projected quad never folds over itself (max
// corner offset ELEV_STEP <= TILE_H/2), so the rasterizer needs no
// concave-quad handling.
// mode +1: raise the LOWER side of a too-steep pair (converges upward,
// preserves peaks — used by init and raise edits). mode -1: lower the
// HIGHER side (used by lower edits, so pulling one tile down drags its
// neighbors down instead of the edit being relaxed away).
static void relax_dir(int mode) {
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int cy = 0; cy <= WORLD_H; cy++) {
            for (int cx = 0; cx <= WORLD_W; cx++) {
                int h = world_height[cy][cx];
                if (cx < WORLD_W && world_height[cy][cx + 1] > h + 1) {
                    if (mode > 0) world_height[cy][cx] = (uint8_t)(world_height[cy][cx + 1] - 1);
                    else world_height[cy][cx + 1] = (uint8_t)(h + 1);
                    changed = 1;
                }
                if (cx < WORLD_W && world_height[cy][cx + 1] < h - 1) {
                    if (mode > 0) world_height[cy][cx + 1] = (uint8_t)(h - 1);
                    else world_height[cy][cx] = (uint8_t)(world_height[cy][cx + 1] + 1);
                    changed = 1;
                }
                h = world_height[cy][cx];
                if (cy < WORLD_H && world_height[cy + 1][cx] > h + 1) {
                    if (mode > 0) world_height[cy][cx] = (uint8_t)(world_height[cy + 1][cx] - 1);
                    else world_height[cy + 1][cx] = (uint8_t)(h + 1);
                    changed = 1;
                }
                if (cy < WORLD_H && world_height[cy + 1][cx] < h - 1) {
                    if (mode > 0) world_height[cy + 1][cx] = (uint8_t)(h - 1);
                    else world_height[cy][cx] = (uint8_t)(world_height[cy + 1][cx] + 1);
                    changed = 1;
                }
            }
        }
    }
}

void terrain_enforce_slope(void) {
    relax_dir(+1);
}

// Raise (delta +1) or lower (delta -1) all four corners of tile (gx, gy),
// then propagate so the slope rule keeps holding — RCT terraforming.
void terrain_edit_tile(int gx, int gy, int delta) {
    if (gx < 0 || gx >= WORLD_W || gy < 0 || gy >= WORLD_H) {
        return;
    }
    for (int dy = 0; dy <= 1; dy++) {
        for (int dx = 0; dx <= 1; dx++) {
            int h = world_height[gy + dy][gx + dx] + delta;
            if (h < 0) h = 0;
            if (h > MAX_HEIGHT) h = MAX_HEIGHT;
            world_height[gy + dy][gx + dx] = (uint8_t)h;
        }
    }
    relax_dir(delta > 0 ? +1 : -1);
}

void terrain_init(void) {
    for (int cy = 0; cy <= WORLD_H; cy++) {
        for (int cx = 0; cx <= WORLD_W; cx++) {
            world_height[cy][cx] = (uint8_t)noise_height(cx, cy);
        }
    }
    // Flat plateau in the middle for the city (city center at 64,64),
    // always above the waterline so the core can't spawn drowned.
    int ph = world_height[WORLD_H / 2][WORLD_W / 2];
    if (ph <= WATER_LEVEL) {
        ph = WATER_LEVEL + 1;
    }
    for (int cy = WORLD_H / 2 - 12; cy <= WORLD_H / 2 + 12; cy++) {
        for (int cx = WORLD_W / 2 - 12; cx <= WORLD_W / 2 + 12; cx++) {
            world_height[cy][cx] = (uint8_t)ph;
        }
    }
    terrain_enforce_slope();
}

// ---- flat-shaded triangle rasterizer ----

static inline void span(int y, int xl, int xr, uint32_t color) {
    if ((unsigned)y >= FB_HEIGHT || xr < 0 || xl >= FB_WIDTH) {
        return;
    }
    if (xl < 0) xl = 0;
    if (xr >= FB_WIDTH) xr = FB_WIDTH - 1;
    if (xr < xl) {
        return;
    }
    neon_fill32(framebuffer_pixels() + (uint32_t)y * FB_WIDTH + (uint32_t)xl,
                color, (uint32_t)(xr - xl + 1));
}

// 16.16 fixed-point edge walk, vertices sorted by y. Degenerate (zero-height)
// triangles fall out naturally: their loops run zero times. Exported so the
// city layer can rasterize building prisms with the same span pipeline.
void iso_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    int tx, ty;
    if (y0 > y1) { tx = x0; x0 = x1; x1 = tx; ty = y0; y0 = y1; y1 = ty; }
    if (y0 > y2) { tx = x0; x0 = x2; x2 = tx; ty = y0; y0 = y2; y2 = ty; }
    if (y1 > y2) { tx = x1; x1 = x2; x2 = tx; ty = y1; y1 = y2; y2 = ty; }
    if (y2 == y0) {
        return;
    }

    int32_t d02 = (int32_t)(((int64_t)(x2 - x0) << 16) / (y2 - y0));
    int32_t xa = x0 << 16;

    if (y1 > y0) {
        int32_t d01 = (int32_t)(((int64_t)(x1 - x0) << 16) / (y1 - y0));
        int32_t xb = x0 << 16;
        for (int y = y0; y < y1; y++) {
            int xl = xa >> 16, xr = xb >> 16;
            if (xl > xr) { int t = xl; xl = xr; xr = t; }
            span(y, xl, xr, color);
            xa += d02;
            xb += d01;
        }
    }
    if (y2 > y1) {
        int32_t d12 = (int32_t)(((int64_t)(x2 - x1) << 16) / (y2 - y1));
        int32_t xb = x1 << 16;
        for (int y = y1; y <= y2; y++) {
            int xl = xa >> 16, xr = xb >> 16;
            if (xl > xr) { int t = xl; xl = xr; xr = t; }
            span(y, xl, xr, color);
            xa += d02;
            xb += d12;
        }
    }
}

// Height ramp: valley grass -> highland grass -> rock. Indexed by avg corner
// height 0..MAX_HEIGHT.
static const uint32_t height_ramp[MAX_HEIGHT + 1] = {
    0x001E4D2A, 0x00225633, 0x00266039, 0x002A6A40, 0x002E7446,
    0x0033804D, 0x00388A54, 0x003D945A, 0x00429E60, 0x00509E66,
    0x00668F60, 0x00787F62, 0x00867670, 0x00948080, 0x00A28E8E, 0x00B09C9C,
};

// Scale an 0x00RRGGBB color by s/64 (s in [0,128] -> 0..2x, saturating).
static uint32_t shade(uint32_t c, int s) {
    int r = (int)((c >> 16) & 0xff) * s / 64;
    int g = (int)((c >> 8) & 0xff) * s / 64;
    int b = (int)(c & 0xff) * s / 64;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// ---- 4-way rotation ----
// The world never moves; the renderer walks VIEW space (painter's order is
// a view-space property) and maps each view tile/corner back into world
// coordinates to fetch heights, city data, and the cursor.

static int g_rot;

void terrain_set_rotation(int rot) {
    g_rot = rot & 3;
}
int terrain_get_rotation(void) {
    return g_rot;
}

static void view_to_world_tile(int vx, int vy, int *gx, int *gy) {
    switch (g_rot) {
    case 0: *gx = vx; *gy = vy; break;
    case 1: *gx = vy; *gy = WORLD_H - 1 - vx; break;
    case 2: *gx = WORLD_W - 1 - vx; *gy = WORLD_H - 1 - vy; break;
    default: *gx = WORLD_W - 1 - vy; *gy = vx; break;
    }
}

static void view_to_world_corner(int cx, int cy, int *wx, int *wy) {
    switch (g_rot) {
    case 0: *wx = cx; *wy = cy; break;
    case 1: *wx = cy; *wy = WORLD_H - cx; break;
    case 2: *wx = WORLD_W - cx; *wy = WORLD_H - cy; break;
    default: *wx = WORLD_W - cy; *wy = cx; break;
    }
}

void terrain_world_to_view_units(int wux, int wuy, int *vux, int *vuy) {
    const int UW = WORLD_W * 16, UH = WORLD_H * 16;
    switch (g_rot) {
    case 0: *vux = wux; *vuy = wuy; break;
    case 1: *vux = UH - wuy; *vuy = wux; break;
    case 2: *vux = UW - wux; *vuy = UH - wuy; break;
    default: *vux = wuy; *vuy = UW - wux; break;
    }
}

int terrain_tile_underwater(int gx, int gy) {
    if ((unsigned)gx >= WORLD_W || (unsigned)gy >= WORLD_H) {
        return 0;
    }
    return (world_height[gy][gx] + world_height[gy][gx + 1] +
            world_height[gy + 1][gx] + world_height[gy + 1][gx + 1]) / 4 < WATER_LEVEL;
}

void terrain_render(int cam_x, int cam_y, int cur_gx, int cur_gy,
                    void (*tile_overlay)(int gx, int gy, int bx, int by)) {
    // ponytail: full repaint every frame; dirty-rect tracking if profiling
    // ever shows terrain fill dominating (Phase 6 will measure).
    for (int d = 0; d <= (WORLD_W - 1) + (WORLD_H - 1); d++) {
        int vx0 = d - (WORLD_H - 1);
        if (vx0 < 0) vx0 = 0;
        int vx1 = d;
        if (vx1 > WORLD_W - 1) vx1 = WORLD_W - 1;
        for (int vx = vx0; vx <= vx1; vx++) {
            int vy = d - vx;
            int gx, gy, wcx, wcy;
            view_to_world_tile(vx, vy, &gx, &gy);

            view_to_world_corner(vx, vy, &wcx, &wcy);
            int h00 = world_height[wcy][wcx];
            view_to_world_corner(vx + 1, vy, &wcx, &wcy);
            int h10 = world_height[wcy][wcx];
            view_to_world_corner(vx, vy + 1, &wcx, &wcy);
            int h01 = world_height[wcy][wcx];
            view_to_world_corner(vx + 1, vy + 1, &wcx, &wcy);
            int h11 = world_height[wcy][wcx];

            // Projected corners in VIEW space: N=(vx,vy), E, S, W.
            int bx = (vx - vy) * (TILE_W / 2) - cam_x;
            int by = (vx + vy) * (TILE_H / 2) - cam_y;
            int nx = bx, ny = by - h00 * ELEV_STEP;
            int ex = bx + TILE_W / 2, ey = by + TILE_H / 2 - h10 * ELEV_STEP;
            int sx = bx, sy = by + TILE_H - h11 * ELEV_STEP;
            int wx = bx - TILE_W / 2, wy = by + TILE_H / 2 - h01 * ELEV_STEP;

            // Cull: skip tiles fully off-screen horizontally or vertically.
            if (ex < 0 || wx >= FB_WIDTH) {
                continue;
            }
            int top = ny < ey ? ny : ey;
            if (wy < top) top = wy;
            int bot = sy > ey ? sy : ey;
            if (wy > bot) bot = wy;
            if (top >= FB_HEIGHT || bot < 0) {
                continue;
            }

            int havg = (h00 + h10 + h01 + h11) / 4;
            // West-east slope tilts brightness: light from the west.
            int slope = (h00 + h01) - (h10 + h11);
            uint32_t color = shade(height_ramp[havg], 64 + slope * 10);
            if (gx == cur_gx && gy == cur_gy) {
                color = 0x00FFE060; // cursor highlight, drawn in painter's order
            }

            iso_fill_tri(nx, ny, ex, ey, sx, sy, color);
            iso_fill_tri(nx, ny, sx, sy, wx, wy, color);

            // Flat water plane over submerged tiles, depth-shaded.
            int havg2 = (h00 + h10 + h01 + h11) / 4;
            if (havg2 < WATER_LEVEL) {
                int off = WATER_LEVEL * ELEV_STEP;
                uint32_t wcol = (WATER_LEVEL - havg2 >= 2) ? 0x00142E52 : 0x001E4066;
                iso_fill_tri(bx, by - off, bx + TILE_W / 2, by + TILE_H / 2 - off,
                             bx, by + TILE_H - off, wcol);
                iso_fill_tri(bx, by - off, bx, by + TILE_H - off,
                             bx - TILE_W / 2, by + TILE_H / 2 - off, wcol);
            }
            if (tile_overlay) {
                tile_overlay(gx, gy, bx, by);
            }
        }
    }
}
