// 3x5 font, one uint16_t per glyph: bits 14..12 = top row (left bit = MSB),
// down to bits 2..0 = bottom row.
#include "game/hud.h"
#include "engine/entity_soa.h"
#include "engine/flowfield.h"
#include "engine/terrain.h"
#include "game/city.h"
#include "kernel/framebuffer.h"

#define G(r0, r1, r2, r3, r4) \
    (uint16_t)((r0 << 12) | (r1 << 9) | (r2 << 6) | (r3 << 3) | r4)

static const uint16_t digits[10] = {
    G(07, 05, 05, 05, 07), // 0
    G(02, 06, 02, 02, 07), // 1
    G(07, 01, 07, 04, 07), // 2
    G(07, 01, 07, 01, 07), // 3
    G(05, 05, 07, 01, 01), // 4
    G(07, 04, 07, 01, 07), // 5
    G(07, 04, 07, 05, 07), // 6
    G(07, 01, 01, 01, 01), // 7
    G(07, 05, 07, 05, 07), // 8
    G(07, 05, 07, 01, 07), // 9
};

static const uint16_t letters[26] = {
    G(07, 05, 07, 05, 05), // A
    G(06, 05, 06, 05, 06), // B
    G(07, 04, 04, 04, 07), // C
    G(06, 05, 05, 05, 06), // D
    G(07, 04, 07, 04, 07), // E
    G(07, 04, 07, 04, 04), // F
    G(07, 04, 05, 05, 07), // G
    G(05, 05, 07, 05, 05), // H
    G(07, 02, 02, 02, 07), // I
    G(01, 01, 01, 05, 07), // J
    G(05, 06, 04, 06, 05), // K
    G(04, 04, 04, 04, 07), // L
    G(05, 07, 07, 05, 05), // M
    G(07, 05, 05, 05, 05), // N
    G(07, 05, 05, 05, 07), // O
    G(07, 05, 07, 04, 04), // P
    G(07, 05, 05, 07, 01), // Q
    G(07, 05, 06, 05, 05), // R
    G(07, 04, 07, 01, 07), // S
    G(07, 02, 02, 02, 02), // T
    G(05, 05, 05, 05, 07), // U
    G(05, 05, 05, 05, 02), // V
    G(05, 05, 07, 07, 05), // W
    G(05, 05, 02, 05, 05), // X
    G(05, 05, 02, 02, 02), // Y
    G(07, 01, 02, 04, 07), // Z
};

static uint16_t glyph_for(char c) {
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= 'a' && c <= 'z') return letters[c - 'a'];
    if (c == '-') return G(00, 00, 07, 00, 00);
    if (c == ':') return G(00, 02, 00, 02, 00);
    if (c == '$') return G(03, 06, 02, 03, 06);
    return 0; // space / unknown
}

static void draw_glyph(int x, int y, uint16_t g, uint32_t color) {
    for (int r = 0; r < 5; r++) {
        int bits = (g >> (12 - r * 3)) & 07;
        for (int c = 0; c < 3; c++) {
            if (!(bits & (4 >> c))) {
                continue;
            }
            for (int dy = 0; dy < HUD_SCALE; dy++) {
                for (int dx = 0; dx < HUD_SCALE; dx++) {
                    framebuffer_set_pixel(x + c * HUD_SCALE + dx,
                                          y + r * HUD_SCALE + dy, color);
                }
            }
        }
    }
}

void hud_text(int x, int y, const char *s, uint32_t color) {
    for (; *s; s++, x += HUD_CHAR_W) {
        uint16_t g = glyph_for(*s);
        if (g) {
            draw_glyph(x, y, g, color);
        }
    }
}

void hud_number(int x_end, int y, uint32_t v, uint32_t color) {
    do {
        x_end -= HUD_CHAR_W;
        draw_glyph(x_end, y, digits[v % 10], color);
        v /= 10;
    } while (v > 0);
}

// ---- minimap ----

#define MM_SCALE 2
#define MM_MARGIN 16

static uint32_t minimap_tile_color(int gx, int gy) {
    uint8_t t = world_tile[gy][gx];
    switch (t) {
    case CITY_ROAD: case CITY_AVENUE: return 0x00808088;
    case CITY_BARRICADE: return 0x00A03030;
    case CITY_TURRET: case CITY_WATCHTOWER: return 0x00FFD060;
    case CITY_CORE: return 0x0040FF40;
    case CITY_GRANARY: case CITY_BARRACKS: return 0x00C08050;
    default:
        if (t >= CITY_R1 && t <= CITY_R3) return 0x0070C070;
        if (t >= CITY_C1 && t <= CITY_C3) return 0x006090D0;
        if (t >= CITY_I1 && t <= CITY_I3) return 0x00C0A040;
        if (t >= CITY_ZONE_R && t <= CITY_ZONE_I) return 0x00405048;
        break;
    }
    if (terrain_tile_underwater(gx, gy)) {
        return 0x001E4066;
    }
    // Terrain: darker in valleys, lighter on peaks.
    int h = world_height[gy][gx];
    return 0x00184020 + (uint32_t)(h * 0x040804);
}

void hud_minimap(int cur_gx, int cur_gy, int show_entities) {
    int ox = FB_WIDTH - WORLD_W * MM_SCALE - MM_MARGIN;
    int oy = FB_HEIGHT - WORLD_H * MM_SCALE - MM_MARGIN;
    for (int gy = 0; gy < WORLD_H; gy++) {
        for (int gx = 0; gx < WORLD_W; gx++) {
            uint32_t c = minimap_tile_color(gx, gy);
            int px = ox + gx * MM_SCALE, py = oy + gy * MM_SCALE;
            for (int d = 0; d < MM_SCALE * MM_SCALE; d++) {
                framebuffer_set_pixel(px + d % MM_SCALE, py + d / MM_SCALE, c);
            }
        }
    }
    if (show_entities) {
        for (uint32_t i = 0; i < entity_count; i++) {
            int gx = (entity_x[i] >> 16) / FLOWFIELD_CELL_SIZE;
            int gy = (entity_y[i] >> 16) / FLOWFIELD_CELL_SIZE;
            if ((unsigned)gx < WORLD_W && (unsigned)gy < WORLD_H) {
                framebuffer_set_pixel(ox + gx * MM_SCALE, oy + gy * MM_SCALE,
                                      entity_type[i] == 1 ? 0x00FFD060 : 0x0060C0FF);
            }
        }
    }
    if (cur_gx >= 0) { // cursor cross
        for (int d = -2; d <= 2; d++) {
            framebuffer_set_pixel(ox + cur_gx * MM_SCALE + d, oy + cur_gy * MM_SCALE, 0x00FFFFFF);
            framebuffer_set_pixel(ox + cur_gx * MM_SCALE, oy + cur_gy * MM_SCALE + d, 0x00FFFFFF);
        }
    }
}
