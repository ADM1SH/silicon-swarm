#include "engine/entity_soa.h"
#include "game/input.h"
#include "kernel/alloc.h"
#include "kernel/exceptions.h"
#include "kernel/framebuffer.h"
#include "kernel/gic.h"
#include "kernel/mmu.h"
#include "kernel/timer.h"
#include "kernel/uart.h"

#define CURSOR_SIZE 8
#define CURSOR_STEP 8
#define CURSOR_COLOR 0x00FFFFFFu
#define ENTITY_COLOR 0x0060C0FFu
#define BG_COLOR 0x00101018u
#define NUM_DUMMY_ENTITIES 10000

static uint32_t gradient_color(int x, int y) {
    uint32_t r = (uint32_t)(x * 255 / (FB_WIDTH - 1));
    uint32_t g = (uint32_t)(y * 255 / (FB_HEIGHT - 1));
    uint32_t b = 0x40;
    return (r << 16) | (g << 8) | b;
}

static void draw_cursor(int x, int y, uint32_t color) {
    for (int dy = 0; dy < CURSOR_SIZE; dy++) {
        for (int dx = 0; dx < CURSOR_SIZE; dx++) {
            framebuffer_set_pixel(x + dx, y + dy, color);
        }
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

// Deterministic pseudo-random spread (LCG, same constants glibc's rand()
// uses) -- no libc, and reproducible runs matter more than true randomness
// for a dummy movement demo.
static uint32_t g_rng_state = 12345;
static uint32_t next_rand(void) {
    g_rng_state = g_rng_state * 1103515245u + 12345u;
    return (g_rng_state >> 16) & 0x7fff;
}

static void spawn_dummy_entities(void) {
    for (int i = 0; i < NUM_DUMMY_ENTITIES; i++) {
        int32_t x = (int32_t)(next_rand() % FB_WIDTH) << 16;
        int32_t y = (int32_t)(next_rand() % FB_HEIGHT) << 16;
        int32_t vx_px = (int32_t)(next_rand() % 4) - 2; // -2..1 px/tick
        if (vx_px == 0) vx_px = 1;
        int32_t vy_px = (int32_t)(next_rand() % 4) - 2;
        if (vy_px == 0) vy_px = -1;
        entity_spawn(x, y, vx_px << 16, vy_px << 16, 1, 0);
    }
}

// Position update (engine/entity_soa.c) plus a wall-bounce policy -- bounds
// are a game-level concern, not the generic SoA module's job.
static void entity_tick(void) {
    entity_update_all();
    for (uint32_t i = 0; i < entity_count; i++) {
        if (!entity_alive[i]) {
            continue;
        }
        int32_t px = entity_x[i] >> 16;
        if (px < 0) {
            entity_x[i] = 0;
            entity_vx[i] = -entity_vx[i];
        } else if (px >= FB_WIDTH) {
            entity_x[i] = (int32_t)(FB_WIDTH - 1) << 16;
            entity_vx[i] = -entity_vx[i];
        }
        int32_t py = entity_y[i] >> 16;
        if (py < 0) {
            entity_y[i] = 0;
            entity_vy[i] = -entity_vy[i];
        } else if (py >= FB_HEIGHT) {
            entity_y[i] = (int32_t)(FB_HEIGHT - 1) << 16;
            entity_vy[i] = -entity_vy[i];
        }
    }
}

// One full frame: background, every alive entity, then the cursor on top.
// A full clear + redraw each tick (rather than erase-by-old-position) is
// the only sane option once thousands of entities can overlap the same
// pixel.
static void render_frame(int cursor_x, int cursor_y) {
    framebuffer_fill(BG_COLOR);
    for (uint32_t i = 0; i < entity_count; i++) {
        if (!entity_alive[i]) {
            continue;
        }
        framebuffer_set_pixel(entity_x[i] >> 16, entity_y[i] >> 16, ENTITY_COLOR);
    }
    draw_cursor(cursor_x, cursor_y, CURSOR_COLOR);
    framebuffer_flush();
}

void kmain(void) {
    uart_init();
    uart_puts("SILICON SWARM BOOT OK\n");

    exceptions_init();

    mmu_init();
    uart_puts("MMU + caches enabled\n");

    gic_init();
    timer_init(60);
    irq_set_handler(timer_irq_handler);
    __asm__ volatile("msr daifclr, #2"); // unmask IRQ
    uart_puts("GIC + timer enabled (60Hz)\n");

    alloc_init();
    entity_soa_init();

    int fb_ok = framebuffer_init();
    int cursor_x = FB_WIDTH / 2 - CURSOR_SIZE / 2;
    int cursor_y = FB_HEIGHT / 2 - CURSOR_SIZE / 2;
    if (fb_ok) {
        framebuffer_fill(0x001040A0); // solid fill
        framebuffer_flush();

        uint64_t wait_until = timer_get_ticks() + 60; // ~1s, so the solid
        while (timer_get_ticks() < wait_until) {}       // fill is separately observable

        for (int y = 0; y < FB_HEIGHT; y++) {
            for (int x = 0; x < FB_WIDTH; x++) {
                framebuffer_set_pixel(x, y, gradient_color(x, y));
            }
        }
        framebuffer_flush();
        uart_puts("test pattern drawn\n");

        spawn_dummy_entities();
        uart_puts("Phase 7: spawned ");
        print_dec64(entity_count);
        uart_puts(" dummy entities\n");
        uart_puts("Phase 6: WASD to move the cursor\n");
    }

    uint64_t last_reported = 0;
    uint64_t last_frame_tick = timer_get_ticks();
    uint64_t frame_count = 0;
    while (1) {
        uint64_t ticks = timer_get_ticks();
        if (ticks - last_reported >= 60) {
            // frame_count vs. ticks is the actual "stable 60Hz" evidence --
            // the tick counter itself is IRQ-driven and keeps incrementing
            // regardless of whether the render loop below is keeping up,
            // so a growing gap between them (not just the tick count alone)
            // is what would reveal dropped frames.
            uart_puts("tick count = ");
            print_dec64(ticks);
            uart_puts(", frames = ");
            print_dec64(frame_count);
            uart_putc('\n');
            last_reported = ticks;
        }

        if (fb_ok) {
            input_action_t action = input_poll();
            if (action != INPUT_NONE) {
                int nx = cursor_x, ny = cursor_y;
                switch (action) {
                case INPUT_UP:
                    ny -= CURSOR_STEP;
                    break;
                case INPUT_DOWN:
                    ny += CURSOR_STEP;
                    break;
                case INPUT_LEFT:
                    nx -= CURSOR_STEP;
                    break;
                case INPUT_RIGHT:
                    nx += CURSOR_STEP;
                    break;
                default:
                    break;
                }
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx > FB_WIDTH - CURSOR_SIZE) nx = FB_WIDTH - CURSOR_SIZE;
                if (ny > FB_HEIGHT - CURSOR_SIZE) ny = FB_HEIGHT - CURSOR_SIZE;
                if (nx != cursor_x || ny != cursor_y) {
                    cursor_x = nx;
                    cursor_y = ny;
                    uart_puts("cursor -> (");
                    print_dec64((uint64_t)cursor_x);
                    uart_puts(", ");
                    print_dec64((uint64_t)cursor_y);
                    uart_puts(")\n");
                }
            }

            // One simulation + render step per new tick -- this is what
            // "stable 60Hz" means: if entity_tick()+render_frame() ever
            // took longer than a tick's budget, ticks would advance faster
            // than frames render and this gate would fall behind visibly.
            if (ticks != last_frame_tick) {
                last_frame_tick = ticks;
                entity_tick();
                render_frame(cursor_x, cursor_y);
                frame_count++;
            }
        }
    }
}
