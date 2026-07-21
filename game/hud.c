// 3x5 font, one uint16_t per glyph: bits 14..12 = top row (left bit = MSB),
// down to bits 2..0 = bottom row.
#include "game/hud.h"
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
