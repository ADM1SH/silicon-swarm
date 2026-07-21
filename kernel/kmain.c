// v2: isometric city-builder + swarm siege. Build roads/houses for income,
// terraform, wall the core with barricades/turrets, then hold off the wave.
// Win -> bounty, back to building. Lose -> enter restarts the game.
#include "engine/entity_soa.h"
#include "engine/flowfield.h"
#include "engine/spatial_hash.h"
#include "engine/terrain.h"
#include "game/city.h"
#include "game/hud.h"
#include "game/input.h"
#include "game/siege_phase.h"
#include "kernel/alloc.h"
#include "kernel/exceptions.h"
#include "kernel/framebuffer.h"
#include "kernel/gic.h"
#include "kernel/mmu.h"
#include "kernel/perf.h"
#include "kernel/timer.h"
#include "kernel/uart.h"
#include "kernel/virtio_input.h"

#define ATTACKER_COLOR 0x0060C0FF
#define DEFENDER_COLOR 0x00FFD060
#define WIN_BOUNTY 150

typedef enum {
    GAME_BUILD,
    GAME_SIEGE,
    GAME_WON, // transient: pays the bounty, returns to GAME_BUILD
    GAME_LOST,
} game_state_t;

static const char *tool_name(city_tile_t t) {
    switch (t) {
    case CITY_ROAD: return "ROAD $5";
    case CITY_AVENUE: return "AVENUE $15";
    case CITY_BARRICADE: return "WALL $10";
    case CITY_TURRET: return "TURRET $50";
    case CITY_WATCHTOWER: return "WATCHTOWER $40";
    case CITY_GRANARY: return "GRANARY $60";
    case CITY_BARRACKS: return "BARRACKS $80";
    case CITY_ZONE_R: return "ZONE R $2";
    case CITY_ZONE_C: return "ZONE C $2";
    case CITY_ZONE_I: return "ZONE I $2";
    default: return "";
    }
}

static void hud_signed(int x, int y, int32_t v, uint32_t color) {
    if (v < 0) {
        hud_text(x - HUD_CHAR_W, y, "-", color);
        v = -v;
    }
    hud_number(x + 4 * HUD_CHAR_W, y, (uint32_t)v, color);
}

static void render_hud(game_state_t state, city_tile_t sel_tool, int wave, int tax_sel) {
    int line = HUD_CHAR_H + 6;
    hud_text(16, 12, "$", 0x00FFE060);
    hud_number(16 + 7 * HUD_CHAR_W, 12, (uint32_t)(city_money > 0 ? city_money : 0), 0x00FFE060);
    hud_text(16 + 9 * HUD_CHAR_W, 12, "POP", 0x00FFFFFF);
    hud_number(16 + 18 * HUD_CHAR_W, 12, (uint32_t)city_pop, 0x00FFFFFF);
    hud_text(16 + 20 * HUD_CHAR_W, 12, "FOOD", 0x00B08040);
    hud_number(16 + 29 * HUD_CHAR_W, 12, (uint32_t)city_food, 0x00B08040);
    hud_text(FB_WIDTH - 10 * HUD_CHAR_W, 12, "WAVE", 0x00FFFFFF);
    hud_number(FB_WIDTH - 2 * HUD_CHAR_W, 12, (uint32_t)wave, 0x00FFFFFF);
    if (state == GAME_BUILD) {
        hud_text(16, 12 + line, tool_name(sel_tool), 0x00FFFFFF);
        // RCI demand meters (green = wants growth).
        static const char *rci = "RCI";
        for (int i = 0; i < 3; i++) {
            char lbl[2] = {rci[i], 0};
            int y = 12 + line * (2 + i);
            hud_text(16, y, lbl, 0x00A0A0B0);
            hud_signed(16 + 3 * HUD_CHAR_W, y, city_demand[i],
                       city_demand[i] > 0 ? 0x0060D060 : 0x00D06060);
            hud_text(16 + 9 * HUD_CHAR_W, y, i == tax_sel ? "TAX-" : "TAX", 0x00FFFFFF);
            hud_number(16 + 15 * HUD_CHAR_W, y, (uint32_t)city_tax[i],
                       i == tax_sel ? 0x00FFE060 : 0x00A0A0B0);
        }
        hud_text(16, FB_HEIGHT - HUD_CHAR_H - 12,
                 "WASD Q-E TERRAIN  R ROTATE  1-0 TOOLS  T TAX  - = RATE  SPACE  X  ENTER SIEGE",
                 0x00A0A0B0);
    } else {
        uint32_t attackers, defenders;
        siege_phase_counts(&attackers, &defenders);
        int32_t hp = siege_phase_core_hp();
        hud_text(16, 12 + HUD_CHAR_H + 6, "CORE HP", 0x0040FF40);
        hud_number(16 + 11 * HUD_CHAR_W, 12 + HUD_CHAR_H + 6, (uint32_t)(hp > 0 ? hp : 0), 0x0040FF40);
        hud_text(16, 12 + 2 * (HUD_CHAR_H + 6), "FOES", 0x0060C0FF);
        hud_number(16 + 11 * HUD_CHAR_W, 12 + 2 * (HUD_CHAR_H + 6), attackers, 0x0060C0FF);
    }
    if (state == GAME_LOST) {
        hud_text(FB_WIDTH / 2 - 13 * HUD_CHAR_W, FB_HEIGHT / 2 - HUD_CHAR_H,
                 "THE CITY HAS FALLEN", 0x00FF5050);
        hud_text(FB_WIDTH / 2 - 13 * HUD_CHAR_W, FB_HEIGHT / 2 + 6,
                 "PRESS ENTER - NEW GAME", 0x00FFFFFF);
    }
}

static void print_dec64(uint64_t v) {
    char buf[20];
    int i = 0;
    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0) {
        buf[i++] = '0' + (char)(v % 10);
        v /= 10;
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

// Entities live in world units (16 per tile), rotated into view units for
// projection: 1 view unit = 1 px in x, 1/2 px in y, minus terrain elevation.
static int g_cam_x, g_cam_y;
static int g_in_siege;

static void draw_entity(uint32_t i) {
    int wux = entity_x[i] >> 16, wuy = entity_y[i] >> 16;
    int gx = wux / FLOWFIELD_CELL_SIZE, gy = wuy / FLOWFIELD_CELL_SIZE;
    if ((unsigned)gx >= WORLD_W || (unsigned)gy >= WORLD_H) {
        return;
    }
    int vux, vuy;
    terrain_world_to_view_units(wux, wuy, &vux, &vuy);
    int sx = (vux - vuy) - g_cam_x;
    int sy = (vux + vuy) / 2 - world_height[gy][gx] * ELEV_STEP - g_cam_y;
    uint32_t c = (entity_type[i] == 1) ? DEFENDER_COLOR : ATTACKER_COLOR;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            framebuffer_set_pixel(sx + dx, sy + dy, c);
        }
    }
}

// terrain_render overlay: building prism, then this tile's entities from
// the spatial hash bucket — painter's order, so swarms occlude correctly
// behind hills and buildings.
static void tile_overlay(int gx, int gy, int bx, int by) {
    city_draw_tile(gx, gy, bx, by);
    if (!g_in_siege) {
        return;
    }
    const uint32_t *ids;
    uint32_t n;
    spatial_hash_cell_entities(gx, gy, &ids, &n);
    for (uint32_t k = 0; k < n; k++) {
        draw_entity(ids[k]);
    }
}

void kmain(void) {
    uart_init();
    uart_puts("SILICON SWARM BOOT OK\n");

    exceptions_init();
    perf_init();

    mmu_init();
    uart_puts("MMU + caches enabled\n");

    gic_init();
    timer_init(60);
    irq_set_handler(timer_irq_handler);
    __asm__ volatile("msr daifclr, #2"); // unmask IRQ
    uart_puts("GIC + timer enabled (60Hz)\n");

    alloc_init();
    entity_soa_init();
    flowfield_init();
    spatial_hash_init();

    int fb_ok = framebuffer_init();
    virtio_input_init();
    terrain_init();
    city_init();

    int cur_gx = WORLD_W / 2, cur_gy = WORLD_H / 2;
    city_tile_t sel_tool = CITY_ROAD;
    game_state_t state = GAME_BUILD;
    int result_printed = 0;
    int wave = 1;
    int tax_sel = 0;
    int sim_speed = 1; // 1x/2x/4x: siege ticks per frame, city ticks per second

    if (fb_ok) {
        uart_puts("v2: CITY -- WASD cursor, q/e terrain, 1=barricade 2=turret "
                   "3=road 4=house, space=place, x=demolish, enter=siege\n");
    }

    uint64_t last_reported = 0;
    uint64_t last_frame_tick = timer_get_ticks();
    uint64_t frame_count = 0;
    uint64_t last_render_cycles = 0;

    while (1) {
        uint64_t ticks = timer_get_ticks();
        if (ticks - last_reported >= 60) {
            if (state == GAME_BUILD) {
                for (int k = 0; k < sim_speed; k++) {
                    city_sim_tick();
                }
            }
            uart_puts("tick count = ");
            print_dec64(ticks);
            uart_puts(", frames = ");
            print_dec64(frame_count);
            uart_puts(", money = ");
            print_dec64((uint64_t)(city_money > 0 ? city_money : 0));
            if (state == GAME_SIEGE || state == GAME_LOST) {
                uint32_t attackers, defenders;
                siege_phase_counts(&attackers, &defenders);
                uart_puts(", attackers = ");
                print_dec64(attackers);
                uart_puts(", defenders = ");
                print_dec64(defenders);
                uart_puts(", core_hp = ");
                print_dec64((uint64_t)(siege_phase_core_hp() > 0 ? siege_phase_core_hp() : 0));
                uart_puts(", steer_cyc = ");
                print_dec64(siege_phase_last_steer_cycles());
                uart_puts(", combat_cyc = ");
                print_dec64(siege_phase_last_combat_cycles());
            }
            uart_puts(", render_cyc = ");
            print_dec64(last_render_cycles);
            uart_putc('\n');
            last_reported = ticks;
        }

        if (fb_ok) {
            input_action_t action = input_poll();
            if (state == GAME_BUILD) {
                switch (action) {
                case INPUT_UP:
                    if (cur_gy > 0) cur_gy--;
                    break;
                case INPUT_DOWN:
                    if (cur_gy < WORLD_H - 1) cur_gy++;
                    break;
                case INPUT_LEFT:
                    if (cur_gx > 0) cur_gx--;
                    break;
                case INPUT_RIGHT:
                    if (cur_gx < WORLD_W - 1) cur_gx++;
                    break;
                case INPUT_UP_FAST:
                    cur_gy = cur_gy > 5 ? cur_gy - 5 : 0;
                    break;
                case INPUT_DOWN_FAST:
                    cur_gy = cur_gy < WORLD_H - 6 ? cur_gy + 5 : WORLD_H - 1;
                    break;
                case INPUT_LEFT_FAST:
                    cur_gx = cur_gx > 5 ? cur_gx - 5 : 0;
                    break;
                case INPUT_RIGHT_FAST:
                    cur_gx = cur_gx < WORLD_W - 6 ? cur_gx + 5 : WORLD_W - 1;
                    break;
                case INPUT_SPEED:
                    sim_speed = sim_speed >= 4 ? 1 : sim_speed * 2;
                    uart_puts(sim_speed == 1 ? "speed: 1x\n"
                              : sim_speed == 2 ? "speed: 2x\n" : "speed: 4x\n");
                    break;
                case INPUT_RAISE:
                    terrain_edit_tile(cur_gx, cur_gy, +1);
                    break;
                case INPUT_LOWER:
                    terrain_edit_tile(cur_gx, cur_gy, -1);
                    break;
                case INPUT_TOOL_BARRICADE:
                    sel_tool = CITY_BARRICADE;
                    uart_puts("tool: barricade\n");
                    break;
                case INPUT_TOOL_TURRET:
                    sel_tool = CITY_TURRET;
                    uart_puts("tool: turret\n");
                    break;
                case INPUT_TOOL_ROAD:
                    sel_tool = CITY_ROAD;
                    uart_puts("tool: road\n");
                    break;
                case INPUT_TOOL_AVENUE:
                    sel_tool = CITY_AVENUE;
                    uart_puts("tool: avenue\n");
                    break;
                case INPUT_TOOL_ZONE_R:
                    sel_tool = CITY_ZONE_R;
                    uart_puts("tool: zone R\n");
                    break;
                case INPUT_TOOL_ZONE_C:
                    sel_tool = CITY_ZONE_C;
                    uart_puts("tool: zone C\n");
                    break;
                case INPUT_TOOL_ZONE_I:
                    sel_tool = CITY_ZONE_I;
                    uart_puts("tool: zone I\n");
                    break;
                case INPUT_TOOL_WATCHTOWER:
                    sel_tool = CITY_WATCHTOWER;
                    uart_puts("tool: watchtower\n");
                    break;
                case INPUT_TOOL_GRANARY:
                    sel_tool = CITY_GRANARY;
                    uart_puts("tool: granary\n");
                    break;
                case INPUT_TOOL_BARRACKS:
                    sel_tool = CITY_BARRACKS;
                    uart_puts("tool: barracks\n");
                    break;
                case INPUT_TAX_CYCLE:
                    tax_sel = (tax_sel + 1) % 3;
                    break;
                case INPUT_TAX_DOWN:
                    if (city_tax[tax_sel] > 0) city_tax[tax_sel]--;
                    break;
                case INPUT_TAX_UP:
                    if (city_tax[tax_sel] < 20) city_tax[tax_sel]++;
                    break;
                case INPUT_PLACE:
                    uart_puts(city_place(cur_gx, cur_gy, sel_tool)
                                  ? "placed\n"
                                  : "rejected (occupied/sloped/broke)\n");
                    break;
                case INPUT_DEMOLISH:
                    if (city_demolish(cur_gx, cur_gy)) {
                        uart_puts("demolished\n");
                    }
                    break;
                case INPUT_ROTATE:
                    terrain_set_rotation(terrain_get_rotation() + 1);
                    break;
                case INPUT_START:
                    siege_phase_start(wave);
                    state = GAME_SIEGE;
                    result_printed = 0;
                    uart_puts("SIEGE STARTED\n");
                    break;
                default:
                    break;
                }
            } else if (state == GAME_LOST && action == INPUT_START) {
                // Full restart: fresh map, fresh city, fresh wallet.
                terrain_init();
                city_init();
                entity_soa_init();
                state = GAME_BUILD;
                wave = 1;
                uart_puts("NEW GAME\n");
            } else if (state != GAME_BUILD && action == INPUT_ROTATE) {
                terrain_set_rotation(terrain_get_rotation() + 1);
            }

            if (ticks != last_frame_tick) {
                last_frame_tick = ticks;

                for (int k = 0; k < sim_speed && state == GAME_SIEGE; k++) {
                    siege_phase_tick();
                    if (siege_phase_is_lost()) {
                        state = GAME_LOST;
                    } else if (siege_phase_is_won()) {
                        state = GAME_WON;
                    }
                }
                if (!result_printed && (state == GAME_WON || state == GAME_LOST)) {
                    if (state == GAME_WON) {
                        city_money += WIN_BOUNTY + 50 * (wave - 1);
                        wave++;
                        uart_puts("SIEGE WON -- bounty paid, back to building\n");
                        entity_soa_init(); // clear surviving defenders
                        state = GAME_BUILD;
                    } else if (city_food >= 50 + 25 * wave) {
                        // The granaries hold: spend the stockpile, survive.
                        city_food -= 50 + 25 * wave;
                        uart_puts("CORE FELL -- BUT THE GRANARIES HELD. Rebuild.\n");
                        entity_soa_init();
                        state = GAME_BUILD;
                    } else {
                        uart_puts("SIEGE LOST -- core destroyed (enter = new game)\n");
                    }
                    result_printed = 1;
                }

                // Camera centers the cursor tile (view space, rotation-aware).
                int vux, vuy;
                terrain_world_to_view_units(cur_gx * 16 + 8, cur_gy * 16 + 8, &vux, &vuy);
                g_cam_x = (vux - vuy) - FB_WIDTH / 2;
                g_cam_y = (vux + vuy) / 2 -
                          world_height[cur_gy][cur_gx] * ELEV_STEP - FB_HEIGHT / 2;
                g_in_siege = (state != GAME_BUILD);
                if (g_in_siege) {
                    spatial_hash_build(); // fresh buckets for occlusion draw
                }
                uint64_t render_t0 = perf_cycles();
                framebuffer_fill(0x00101018);
                terrain_render(g_cam_x, g_cam_y,
                               state == GAME_BUILD ? cur_gx : -1,
                               state == GAME_BUILD ? cur_gy : -1, tile_overlay);
                render_hud(state, sel_tool, wave, tax_sel);
                hud_minimap(state == GAME_BUILD ? cur_gx : -1,
                            state == GAME_BUILD ? cur_gy : -1, g_in_siege);
                framebuffer_flush();
                last_render_cycles = perf_cycles() - render_t0;
                frame_count++;
            }
        }
    }
}
