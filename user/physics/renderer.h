#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>
#include "particle.h"
#include "quadtree.h"

// Physics renderer structure for VBE-based visualization
typedef struct physics_renderer {
    uint32_t *framebuffer;        // Pointer to VBE framebuffer
    uint32_t width, height;       // Screen dimensions
    uint32_t pitch;               // Bytes per scanline
    uint32_t background_color;    // Background clear color
    uint8_t show_quadtree;        // Debug: show quadtree structure
    uint8_t show_forces;          // Debug: show force vectors
    uint8_t show_velocities;      // Debug: show velocity vectors
    uint8_t show_info;            // Debug: show simulation info
    float scale_factor;           // Scaling factor for world to screen coordinates
    float offset_x, offset_y;     // Screen offset for centering
} physics_renderer_t;

// Rendering statistics structure
typedef struct {
    uint32_t particles_rendered;
    uint32_t quadtree_nodes_rendered;
    uint32_t frame_time_ms;
    float fps;
    uint64_t last_frame_time;
    uint32_t frame_count;
    uint64_t fps_timer_start;
} render_stats_t;

// Function declarations
physics_renderer_t* physics_renderer_create(uint32_t *framebuffer, uint32_t width, uint32_t height, uint32_t pitch);
void physics_renderer_destroy(physics_renderer_t *renderer);
void physics_renderer_clear_screen(physics_renderer_t *renderer);

// Main rendering functions
void physics_renderer_render_simulation(physics_renderer_t *renderer, const particle_system_t *particles, const quadtree_t *tree);
void physics_renderer_render_simulation_complete(physics_renderer_t *renderer, const particle_system_t *particles, const quadtree_t *tree, render_stats_t *stats, uint32_t target_fps);
void physics_renderer_render_particles(physics_renderer_t *renderer, const particle_system_t *particles);
void physics_renderer_render_quadtree(physics_renderer_t *renderer, const quadtree_t *tree);

// Individual rendering functions
void render_particle(physics_renderer_t *renderer, const particle_t *particle);
void render_particle_circle(physics_renderer_t *renderer, float x, float y, float radius, uint32_t color);
void render_particle_filled_circle(physics_renderer_t *renderer, float x, float y, float radius, uint32_t color);
void render_quadtree_node(physics_renderer_t *renderer, const quadtree_node_t *node);
void render_line(physics_renderer_t *renderer, float x1, float y1, float x2, float y2, uint32_t color);
void render_rectangle(physics_renderer_t *renderer, float x, float y, float width, float height, uint32_t color);

// Debug visualization functions
void render_force_vector(physics_renderer_t *renderer, const particle_t *particle, float force_x, float force_y);
void render_velocity_vector(physics_renderer_t *renderer, const particle_t *particle);
void render_simulation_info(physics_renderer_t *renderer, const render_stats_t *stats);

// Coordinate transformation functions
void world_to_screen(const physics_renderer_t *renderer, float world_x, float world_y, int *screen_x, int *screen_y);
void screen_to_world(const physics_renderer_t *renderer, int screen_x, int screen_y, float *world_x, float *world_y);
void physics_renderer_set_view(physics_renderer_t *renderer, float world_width, float world_height, float center_x, float center_y);

// Frame management functions
void physics_renderer_begin_frame(physics_renderer_t *renderer, render_stats_t *stats);
void physics_renderer_end_frame(physics_renderer_t *renderer, render_stats_t *stats);
void physics_renderer_present_frame(physics_renderer_t *renderer);
int physics_renderer_should_limit_framerate(physics_renderer_t *renderer, uint32_t target_fps);
void physics_renderer_wait_for_frame_sync(physics_renderer_t *renderer, uint32_t target_fps);
void physics_renderer_init_stats(render_stats_t *stats);

// Utility functions
void physics_renderer_set_pixel(physics_renderer_t *renderer, int x, int y, uint32_t color);
uint32_t physics_renderer_get_pixel(physics_renderer_t *renderer, int x, int y);
uint32_t color_lerp(uint32_t color1, uint32_t color2, float t);
uint32_t color_from_velocity(float velocity, float max_velocity);
uint32_t color_from_mass(float mass, float max_mass);

// Color constants
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_BLACK     0xFF000000
#define COLOR_RED       0xFFFF0000
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFF0000FF
#define COLOR_YELLOW    0xFFFFFF00
#define COLOR_CYAN      0xFF00FFFF
#define COLOR_MAGENTA   0xFFFF00FF
#define COLOR_GRAY      0xFF808080
#define COLOR_DARK_GRAY 0xFF404040

#endif // RENDERER_H