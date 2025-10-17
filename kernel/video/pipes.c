#include "vbe/vbe.h"
#include "pipes.h"
#include "splash.h" // for fps logic reference only (printf + timer)
#include "../stdio/stdio.h"
#include "../stdlib/stdlib.h"
#include "../schedule/schedule.h"
#include "../kernel.h"
#include "../io/io.h"
#include <stdint.h>

// Use z-layer 1 for pipes fading (layer 0 reserved / excluded)
#define PIPES_Z_LAYER 1

// Alpha fade amount per frame (0..255). Higher = faster fade.
#define PIPES_FADE_DECAY 1

// Simple Bresenham line (same as splash.c draw_line but local and static)
// this function some straight gpt
static void pipes_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    for (;;) {
        if ((unsigned)x0 < SCREEN_WIDTH && (unsigned)y0 < SCREEN_HEIGHT)
            vbe_fast_putpixel((uint32_t)x0, (uint32_t)y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// z-layer variant (draws with alpha; color expected ARGB)
static void pipes_z_draw_line(int x0, int y0, int x1, int y1, uint32_t argb)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        if ((unsigned)x0 < SCREEN_WIDTH && (unsigned)y0 < SCREEN_HEIGHT)
            vbe_z_putpixel(PIPES_Z_LAYER, (uint32_t)x0, (uint32_t)y0, argb);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

typedef struct
{
    int x, y;       // current head
    int dx, dy;     // current direction
    uint32_t color; // pipe color
} Pipe;

// Pick a new direction that keeps us inside bounds and is not an immediate reversal unless required.
static void pick_new_direction(Pipe *p, int seg_len)
{
    static const int dirs[4][2] = {
        {1, 0},  // right
        {-1, 0}, // left
        {0, 1},  // down
        {0, -1}  // up
    };
    int cand_idx[4];
    int cand_count = 0;
    for (int i = 0; i < 4; ++i)
    {
        int ndx = dirs[i][0];
        int ndy = dirs[i][1];
        // avoid immediate reversal if possible
        if (ndx == -p->dx && ndy == -p->dy)
            continue;
        int nx = p->x + ndx * seg_len;
        int ny = p->y + ndy * seg_len;
        if (nx < 1 || nx >= (int)SCREEN_WIDTH - 1 || ny < 1 || ny >= (int)SCREEN_HEIGHT - 1)
            continue;
        cand_idx[cand_count++] = i;
    }
    if (cand_count == 0)
    {
        // we are likely in a corner – allow reversal or anything that fits
        for (int i = 0; i < 4 && cand_count == 0; ++i)
        {
            int ndx = dirs[i][0];
            int ndy = dirs[i][1];
            int nx = p->x + ndx * seg_len;
            int ny = p->y + ndy * seg_len;
            if (nx < 1 || nx >= (int)SCREEN_WIDTH - 1 || ny < 1 || ny >= (int)SCREEN_HEIGHT - 1)
                continue;
            cand_idx[cand_count++] = i;
        }
        if (cand_count == 0)
            return; // nowhere to go; shouldn't happen with sane seg_len
    }
    int pick = cand_idx[rand() % cand_count];
    p->dx = dirs[pick][0];
    p->dy = dirs[pick][1];
}

/**
 * @brief Turn the pipe 90 degrees.
 *
 * @param p Pointer to the pipe structure.
 * @param dir Direction to turn (-1 for left, 1 for right).
 */
// static void turn_90(Pipe *p, int dir)
// {
//     int ndx = dir * p->dy;
//     int ndy = -dir * p->dx;
//     p->dx = ndx;
//     p->dy = ndy;
// }

/**
 * @brief Generate a random color.
 *
 * @return uint32_t The generated color in ARGB format.
 */
static uint32_t rand_color(void)
{
    int r = rand() & 0xFF;
    int g = rand() & 0xFF;
    int b = rand() & 0xFF;

    // make dat shit brite
    r = (r + 128) & 0xFF;
    g = (g + 128) & 0xFF;
    b = (b + 128) & 0xFF;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/**
 * @brief Reset the pipe to its initial state.
 *
 * @param p Pointer to the pipe structure.
 * @param index Index of the pipe (used for initial position).
 */
static void reset_pipe(Pipe *p, int index, int num_pipes)
{
    p->x = (SCREEN_WIDTH * (index + 1)) / (num_pipes + 1);
    p->y = (SCREEN_HEIGHT / 4) + (index / 4) * (SCREEN_HEIGHT / 8);
    int dir = (rand() % 4);
    p->dx = (dir == 0) - (dir == 2); // right(1), left(-1)
    p->dy = (dir == 1) - (dir == 3); // down(1), up(-1)
    p->color = rand_color();
}

/**
 * @brief Advance the pipe by a segment length.
 *
 * @param p Pointer to the pipe structure.
 * @param seg_len Segment length to advance.
 */
static void advance_pipe(Pipe *p, int seg_len, int use_z_layer)
{
    int nx = p->x + p->dx * seg_len;
    int ny = p->y + p->dy * seg_len;

    int out = (nx < 1 || nx >= (int)SCREEN_WIDTH - 1 || ny < 1 || ny >= (int)SCREEN_HEIGHT - 1);
    if (out)
    {
        pick_new_direction(p, seg_len);
        nx = p->x + p->dx * seg_len;
        ny = p->y + p->dy * seg_len;
    }
    else if ((rand() & 0x1F) == 0)
    {
        pick_new_direction(p, seg_len);
        nx = p->x + p->dx * seg_len;
        ny = p->y + p->dy * seg_len;
    }

    if (use_z_layer) {
        uint32_t argb = p->color;
        pipes_z_draw_line(p->x, p->y, nx, ny, argb);
        if (p->dx != 0)
            pipes_z_draw_line(p->x, p->y + 1, nx, ny + 1, argb);
        else if (p->dy != 0)
            pipes_z_draw_line(p->x + 1, p->y, nx + 1, ny, argb);
    } else {
        pipes_draw_line(p->x, p->y, nx, ny, p->color);
        if (p->dx != 0)
            pipes_draw_line(p->x, p->y + 1, nx, ny + 1, p->color);
        else if (p->dy != 0)
            pipes_draw_line(p->x + 1, p->y, nx + 1, ny, p->color);
    }

    p->x = nx;
    p->y = ny;
}

/**
 * @brief Pipes demo function.
 *
 * This function demonstrates the pipe drawing functionality.
 */
void pipes_demo(void)
{
    // Params
    int num_pipes = 64; // increased now that we allocate on the heap
    const int seg_len = 10; // pixels per step
    Pipe *pipes = (Pipe *)kernel_malloc(sizeof(Pipe) * (uint32_t)num_pipes);
    if (!pipes)
        return; // allocation failed; nothing we can do
    srand(12345);
    for (int i = 0; i < num_pipes; ++i)
        reset_pipe(&pipes[i], i, num_pipes);

    // Clear base layer and ensure z-layer is cleared
    vbe_clear_screen(0xFF000000);
    vbe_clear_z_layer(PIPES_Z_LAYER, 0x00000000); // start fully transparent


    uint32_t start_tick = timer_ticks;
    uint32_t dt = 0;

    for (;;)
    {
        dt = timer_ticks - start_tick;

        if (dt > 16){
            start_tick = timer_ticks;
            vbe_clear_screen(0xFF000000);
            // Fade previous content in-place (trail)
            vbe_z_copy_and_fade(PIPES_Z_LAYER, PIPES_Z_LAYER, PIPES_FADE_DECAY);

            // Advance all pipes a step drawing into z-layer
            for (int i = 0; i < num_pipes; ++i)
                advance_pipe(&pipes[i], seg_len, 1);

            vbe_flip();
            kernel_yield();
        }
        kernel_yield();
    }
}
