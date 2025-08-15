#include "vbe/vbe.h"
#include "pipes.h"
#include "splash.h" // for fps logic reference only (printf + timer)
#include "../stdio/stdio.h"
#include "../stdlib/stdlib.h"
#include "../schedule/schedule.h"
#include <stdint.h>

// Simple Bresenham line (same as splash.c draw_line but local and static)
// this function some straight gpt
static void pipes_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    for (;;)
    {
        if ((unsigned)x0 < SCREEN_WIDTH && (unsigned)y0 < SCREEN_HEIGHT)
            vbe_fast_putpixel((uint32_t)x0, (uint32_t)y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

typedef struct
{
    int x, y;       // current head
    int dx, dy;     // current direction
    uint32_t color; // pipe color
} Pipe;

/**
 * @brief Turn the pipe 90 degrees.
 *
 * @param p Pointer to the pipe structure.
 * @param dir Direction to turn (-1 for left, 1 for right).
 */
static void turn_90(Pipe *p, int dir)
{
    int ndx = dir * p->dy;
    int ndy = -dir * p->dx;
    p->dx = ndx;
    p->dy = ndy;
}

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
static void reset_pipe(Pipe *p, int index)
{
    p->x = (SCREEN_WIDTH / 4) + (index % 4) * (SCREEN_WIDTH / 8);
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
static void advance_pipe(Pipe *p, int seg_len)
{
    int nx = p->x + p->dx * seg_len;
    int ny = p->y + p->dy * seg_len;

    // bounce/turn at edges
    if (nx < 1 || nx >= (int)SCREEN_WIDTH - 1)
    {
        // choose new vertical direction
        p->dx = 0;
        p->dy = (rand() & 1) ? 1 : -1;
        p->color = rand_color();
        nx = p->x + p->dy * 0; // recalc below using new dir
        ny = p->y + p->dy * seg_len;
    }
    else if (ny < 1 || ny >= (int)SCREEN_HEIGHT - 1)
    {
        // choose new horizontal direction
        p->dy = 0;
        p->dx = (rand() & 1) ? 1 : -1;
        p->color = rand_color();
        nx = p->x + p->dx * seg_len;
        ny = p->y + p->dx * 0; // same
    }

    pipes_draw_line(p->x, p->y, nx, ny, p->color);
    // thicken a bit by drawing a parallel pixel when possible
    if (p->dx != 0)
    {
        pipes_draw_line(p->x, p->y + 1, nx, ny + 1, p->color);
    }
    else if (p->dy != 0)
    {
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
    const int num_pipes = 2;
    const int seg_len = 10; // pixels per step

    Pipe pipes[16];
    srand(12345);
    for (int i = 0; i < num_pipes; ++i)
        reset_pipe(&pipes[i], i);

    // FPS state (same logic as cube_demo)
    uint64_t last_fps_tick = 0;
    uint32_t frame_count = 0;
    uint32_t total_frame_count = 0;
    uint32_t current_fps = 0;

    // Clear once, then draw on top like a screensaver
    // vbe_clear_screen(0xFF000000);

    for (;;)
    {
        if (last_fps_tick == 0)
            last_fps_tick = timer_ticks;
        frame_count++;
        total_frame_count++;

        if (timer_ticks - last_fps_tick >= MILLISECONDS_TO_TICKS(1000))
        {
            current_fps = frame_count;
            frame_count = 0;
            last_fps_tick = timer_ticks;
        }

        // Advance all pipes a step
        for (int i = 0; i < num_pipes; ++i)
            advance_pipe(&pipes[i], seg_len);

        // Draw FPS text in top-left
        vbe_set_cursor(0, 0);
        printf("Serotonin Kernel Pipes Demo: %d frames, %d fps", total_frame_count, current_fps);

        vbe_flip_all();
        kernel_yield();
    }
}
