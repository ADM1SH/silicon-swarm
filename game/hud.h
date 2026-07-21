// Minimal HUD text: 3x5 bitmap font scaled up, digits + A-Z + a few
// symbols. Enough for money/tool/HP readouts and banners.
#ifndef GAME_HUD_H
#define GAME_HUD_H

#include <stdint.h>

#define HUD_SCALE 3
#define HUD_CHAR_W (4 * HUD_SCALE) // 3px glyph + 1px gap
#define HUD_CHAR_H (6 * HUD_SCALE)

// Draws s at pixel (x, y). Unknown chars render as space.
void hud_text(int x, int y, const char *s, uint32_t color);
// Right-aligned unsigned number ending at pixel x_end.
void hud_number(int x_end, int y, uint32_t v, uint32_t color);

#endif
