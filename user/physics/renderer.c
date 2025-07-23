/*
    renderer.c
    VBE-based rendering system for Barnes-Hut physics simulation
*/

#include "renderer.h"
#include "../video/vbe/vbe.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../io/io.h"
#include "../string.h"
#include <stdint.h>
#include <stddef.h>

// Math utility functions
static inline float sqrtf_approx(float x) {
    if (x <= 0.0f) return 0.0f;
    
    // Fast inverse square root approximation (Quake III algorithm)
    float xhalf = 0.5f * x;
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    u.f = u.f * (1.5f - xhalf * u.f * u.f);
    return x * u.f;
}

static inline float absf(float x) {
    return x < 0.0f ? -x : x;
}

static inline int min_int(int a, int b) {
    return a < b ? a : b;
}

static inline int max_int(int a, int b) {
    return a > b ? a : b;
}

/**
 * @brief Create a physics renderer instance
 * 
 * @param framebuffer Pointer to VBE framebuffer
 * @param width Screen width
 * @param height Screen height
 * @param pitch Bytes per scanline
 * @return physics_renderer_t* Pointer to created renderer or NULL on failure
 */
physics_renderer_t* physics_renderer_create(uint32_t *framebuffer, uint32_t width, uint32_t height, uint32_t pitch) {
    physics_renderer_t *renderer = (physics_renderer_t*)kernel_malloc(sizeof(physics_renderer_t));
    if (!renderer) {
        return NULL;
    }
    
    renderer->framebuffer = framebuffer;
    renderer->width = width;
    renderer->height = height;
    renderer->pitch = pitch;
    renderer->background_color = COLOR_BLACK;
    renderer->show_quadtree = 0;
    renderer->show_forces = 0;
    renderer->show_velocities = 0;
    renderer->show_info = 0;
    renderer->scale_factor = 1.0f;
    renderer->offset_x = 0.0f;
    renderer->offset_y = 0.0f;
    
    return renderer;
}

/**
 * @brief Destroy a physics renderer instance
 * 
 * @param renderer Renderer to destroy
 */
void physics_renderer_destroy(physics_renderer_t *renderer) {
    if (renderer) {
        kernel_free(renderer);
    }
}

/**
 * @brief Clear the screen with background color
 * 
 * @param renderer Physics renderer instance
 */
void physics_renderer_clear_screen(physics_renderer_t *renderer) {
    if (!renderer || !renderer->framebuffer) {
        return;
    }
    
    vbe_clear_screen(renderer->background_color);
}

/**
 * @brief Set a pixel in the framebuffer
 * 
 * @param renderer Physics renderer instance
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Pixel color
 */
void physics_renderer_set_pixel(physics_renderer_t *renderer, int x, int y, uint32_t color) {
    if (!renderer || x < 0 || y < 0 || x >= (int)renderer->width || y >= (int)renderer->height) {
        return;
    }
    
    vbe_putpixel((uint32_t)x, (uint32_t)y, color);
}

/**
 * @brief Convert world coordinates to screen coordinates
 * 
 * @param renderer Physics renderer instance
 * @param world_x World X coordinate
 * @param world_y World Y coordinate
 * @param screen_x Output screen X coordinate
 * @param screen_y Output screen Y coordinate
 */
void world_to_screen(const physics_renderer_t *renderer, float world_x, float world_y, int *screen_x, int *screen_y) {
    if (!renderer || !screen_x || !screen_y) {
        return;
    }
    
    *screen_x = (int)((world_x + renderer->offset_x) * renderer->scale_factor);
    *screen_y = (int)((world_y + renderer->offset_y) * renderer->scale_factor);
}

/**
 * @brief Convert screen coordinates to world coordinates
 * 
 * @param renderer Physics renderer instance
 * @param screen_x Screen X coordinate
 * @param screen_y Screen Y coordinate
 * @param world_x Output world X coordinate
 * @param world_y Output world Y coordinate
 */
void screen_to_world(const physics_renderer_t *renderer, int screen_x, int screen_y, float *world_x, float *world_y) {
    if (!renderer || !world_x || !world_y) {
        return;
    }
    
    *world_x = ((float)screen_x / renderer->scale_factor) - renderer->offset_x;
    *world_y = ((float)screen_y / renderer->scale_factor) - renderer->offset_y;
}

/**
 * @brief Set the view parameters for coordinate transformation
 * 
 * @param renderer Physics renderer instance
 * @param world_width Width of world view
 * @param world_height Height of world view
 * @param center_x World center X coordinate
 * @param center_y World center Y coordinate
 */
void physics_renderer_set_view(physics_renderer_t *renderer, float world_width, float world_height, float center_x, float center_y) {
    if (!renderer || world_width <= 0.0f || world_height <= 0.0f) {
        return;
    }
    
    float scale_x = (float)renderer->width / world_width;
    float scale_y = (float)renderer->height / world_height;
    renderer->scale_factor = scale_x < scale_y ? scale_x : scale_y;
    
    renderer->offset_x = center_x - (world_width * 0.5f);
    renderer->offset_y = center_y - (world_height * 0.5f);
}

/**
 * @brief Get color based on velocity magnitude
 * 
 * @param velocity Velocity magnitude
 * @param max_velocity Maximum velocity for normalization
 * @return uint32_t Color value
 */
uint32_t color_from_velocity(float velocity, float max_velocity) {
    if (max_velocity <= 0.0f) {
        return COLOR_WHITE;
    }
    
    float normalized = velocity / max_velocity;
    if (normalized > 1.0f) normalized = 1.0f;
    
    // Blue (slow) to red (fast) gradient
    uint8_t red = (uint8_t)(normalized * 255.0f);
    uint8_t blue = (uint8_t)((1.0f - normalized) * 255.0f);
    uint8_t green = 0;
    
    return 0xFF000000 | (red << 16) | (green << 8) | blue;
}

/**
 * @brief Get color based on mass
 * 
 * @param mass Particle mass
 * @param max_mass Maximum mass for normalization
 * @return uint32_t Color value
 */
uint32_t color_from_mass(float mass, float max_mass) {
    if (max_mass <= 0.0f) {
        return COLOR_WHITE;
    }
    
    float normalized = mass / max_mass;
    if (normalized > 1.0f) normalized = 1.0f;
    
    // Small masses are yellow, large masses are white
    uint8_t intensity = (uint8_t)(128.0f + normalized * 127.0f);
    return 0xFF000000 | (intensity << 16) | (intensity << 8) | 0;
}

/**
 * @brief Draw a filled circle for particle rendering
 * 
 * @param renderer Physics renderer instance
 * @param x Center X coordinate (screen space)
 * @param y Center Y coordinate (screen space)
 * @param radius Circle radius (screen space)
 * @param color Fill color
 */
void render_particle_filled_circle(physics_renderer_t *renderer, float x, float y, float radius, uint32_t color) {
    if (!renderer || radius <= 0.0f) {
        return;
    }
    
    int center_x = (int)x;
    int center_y = (int)y;
    int r = (int)radius;
    
    // Use simple filled circle algorithm
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                physics_renderer_set_pixel(renderer, center_x + dx, center_y + dy, color);
            }
        }
    }
}

/**
 * @brief Render a single particle
 * 
 * @param renderer Physics renderer instance
 * @param particle Particle to render
 */
void render_particle(physics_renderer_t *renderer, const particle_t *particle) {
    if (!renderer || !particle || !particle->active) {
        return;
    }
    
    int screen_x, screen_y;
    world_to_screen(renderer, particle->x, particle->y, &screen_x, &screen_y);
    
    // Calculate screen radius based on particle mass and scale
    float screen_radius = particle->radius * renderer->scale_factor;
    if (screen_radius < 1.0f) {
        screen_radius = 1.0f; // Minimum visible size
    }
    
    // Use particle's color or generate color based on velocity
    uint32_t color = particle->color;
    if (color == 0) {
        float velocity = sqrtf_approx(particle->vx * particle->vx + particle->vy * particle->vy);
        color = color_from_velocity(velocity, 100.0f); // Assume max velocity of 100
    }
    
    render_particle_filled_circle(renderer, (float)screen_x, (float)screen_y, screen_radius, color);
}

/**
 * @brief Render all particles in a particle system
 * 
 * @param renderer Physics renderer instance
 * @param particles Particle system to render
 */
void physics_renderer_render_particles(physics_renderer_t *renderer, const particle_system_t *particles) {
    if (!renderer || !particles || !particles->particles) {
        return;
    }
    
    for (uint32_t i = 0; i < particles->count; i++) {
        render_particle(renderer, &particles->particles[i]);
    }
}

/**
 * @brief Draw a circle outline for particle rendering
 * 
 * @param renderer Physics renderer instance
 * @param x Center X coordinate (screen space)
 * @param y Center Y coordinate (screen space)
 * @param radius Circle radius (screen space)
 * @param color Line color
 */
void render_particle_circle(physics_renderer_t *renderer, float x, float y, float radius, uint32_t color) {
    if (!renderer || radius <= 0.0f) {
        return;
    }
    
    int center_x = (int)x;
    int center_y = (int)y;
    int r = (int)radius;
    
    // Bresenham's circle algorithm
    int dx = 0;
    int dy = r;
    int d = 3 - 2 * r;
    
    while (dy >= dx) {
        // Draw 8 octants
        physics_renderer_set_pixel(renderer, center_x + dx, center_y + dy, color);
        physics_renderer_set_pixel(renderer, center_x - dx, center_y + dy, color);
        physics_renderer_set_pixel(renderer, center_x + dx, center_y - dy, color);
        physics_renderer_set_pixel(renderer, center_x - dx, center_y - dy, color);
        physics_renderer_set_pixel(renderer, center_x + dy, center_y + dx, color);
        physics_renderer_set_pixel(renderer, center_x - dy, center_y + dx, color);
        physics_renderer_set_pixel(renderer, center_x + dy, center_y - dx, color);
        physics_renderer_set_pixel(renderer, center_x - dy, center_y - dx, color);
        
        dx++;
        if (d > 0) {
            dy--;
            d = d + 4 * (dx - dy) + 10;
        } else {
            d = d + 4 * dx + 6;
        }
    }
}

/**
 * @brief Draw a line between two points
 * 
 * @param renderer Physics renderer instance
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color Line color
 */
void render_line(physics_renderer_t *renderer, float x1, float y1, float x2, float y2, uint32_t color) {
    if (!renderer) {
        return;
    }
    
    int ix1 = (int)x1;
    int iy1 = (int)y1;
    int ix2 = (int)x2;
    int iy2 = (int)y2;
    
    // Bresenham's line algorithm
    int dx = absf((float)(ix2 - ix1));
    int dy = absf((float)(iy2 - iy1));
    int sx = ix1 < ix2 ? 1 : -1;
    int sy = iy1 < iy2 ? 1 : -1;
    int err = dx - dy;
    
    int x = ix1;
    int y = iy1;
    
    while (1) {
        physics_renderer_set_pixel(renderer, x, y, color);
        
        if (x == ix2 && y == iy2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

/**
 * @brief Draw a rectangle outline
 * 
 * @param renderer Physics renderer instance
 * @param x Top-left X coordinate
 * @param y Top-left Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color Line color
 */
void render_rectangle(physics_renderer_t *renderer, float x, float y, float width, float height, uint32_t color) {
    if (!renderer || width <= 0.0f || height <= 0.0f) {
        return;
    }
    
    float x2 = x + width;
    float y2 = y + height;
    
    // Draw four sides
    render_line(renderer, x, y, x2, y, color);      // Top
    render_line(renderer, x2, y, x2, y2, color);    // Right
    render_line(renderer, x2, y2, x, y2, color);    // Bottom
    render_line(renderer, x, y2, x, y, color);      // Left
}

/**
 * @brief Interpolate between two colors
 * 
 * @param color1 First color
 * @param color2 Second color
 * @param t Interpolation factor (0.0 to 1.0)
 * @return uint32_t Interpolated color
 */
uint32_t color_lerp(uint32_t color1, uint32_t color2, float t) {
    if (t <= 0.0f) return color1;
    if (t >= 1.0f) return color2;
    
    uint8_t a1 = (color1 >> 24) & 0xFF;
    uint8_t r1 = (color1 >> 16) & 0xFF;
    uint8_t g1 = (color1 >> 8) & 0xFF;
    uint8_t b1 = color1 & 0xFF;
    
    uint8_t a2 = (color2 >> 24) & 0xFF;
    uint8_t r2 = (color2 >> 16) & 0xFF;
    uint8_t g2 = (color2 >> 8) & 0xFF;
    uint8_t b2 = color2 & 0xFF;
    
    uint8_t a = (uint8_t)(a1 + t * (a2 - a1));
    uint8_t r = (uint8_t)(r1 + t * (r2 - r1));
    uint8_t g = (uint8_t)(g1 + t * (g2 - g1));
    uint8_t b = (uint8_t)(b1 + t * (b2 - b1));
    
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/**
 * @brief Get a pixel color from the framebuffer
 * 
 * @param renderer Physics renderer instance
 * @param x X coordinate
 * @param y Y coordinate
 * @return uint32_t Pixel color
 */
uint32_t physics_renderer_get_pixel(physics_renderer_t *renderer, int x, int y) {
    if (!renderer || !renderer->framebuffer || x < 0 || y < 0 || 
        x >= (int)renderer->width || y >= (int)renderer->height) {
        return 0;
    }
    
    uint32_t offset = y * (renderer->pitch / sizeof(uint32_t)) + x;
    return renderer->framebuffer[offset];
}

/**
 * @brief Render the complete simulation (particles and optional debug info)
 * 
 * @param renderer Physics renderer instance
 * @param particles Particle system to render
 * @param tree Quadtree for optional debug rendering
 */
void physics_renderer_render_simulation(physics_renderer_t *renderer, const particle_system_t *particles, const quadtree_t *tree) {
    if (!renderer) {
        return;
    }
    
    // Clear screen
    physics_renderer_clear_screen(renderer);
    
    // Render particles
    if (particles) {
        physics_renderer_render_particles(renderer, particles);
    }
    
    // Render quadtree if debug mode is enabled
    if (renderer->show_quadtree && tree) {
        physics_renderer_render_quadtree(renderer, tree);
    }
}

/**
 * @brief Render quadtree structure for debugging
 * 
 * @param renderer Physics renderer instance
 * @param tree Quadtree to render
 */
void physics_renderer_render_quadtree(physics_renderer_t *renderer, const quadtree_t *tree) {
    if (!renderer || !tree || !tree->root) {
        return;
    }
    
    render_quadtree_node(renderer, tree->root);
}

/**
 * @brief Count quadtree nodes recursively
 * 
 * @param node Quadtree node to count
 * @return uint32_t Total number of nodes in subtree
 */
static uint32_t count_quadtree_nodes(const quadtree_node_t *node) {
    if (!node) {
        return 0;
    }
    
    uint32_t count = 1; // Count this node
    
    // Count children if not a leaf
    if (!node->is_leaf) {
        for (int i = 0; i < 4; i++) {
            count += count_quadtree_nodes(node->children[i]);
        }
    }
    
    return count;
}

/**
 * @brief Render a single quadtree node
 * 
 * @param renderer Physics renderer instance
 * @param node Quadtree node to render
 */
void render_quadtree_node(physics_renderer_t *renderer, const quadtree_node_t *node) {
    if (!renderer || !node) {
        return;
    }
    
    int screen_x, screen_y;
    world_to_screen(renderer, node->x - node->width * 0.5f, node->y - node->height * 0.5f, &screen_x, &screen_y);
    
    float screen_width = node->width * renderer->scale_factor;
    float screen_height = node->height * renderer->scale_factor;
    
    // Only draw if the node is visible on screen
    if (screen_x + screen_width >= 0 && screen_x < (int)renderer->width &&
        screen_y + screen_height >= 0 && screen_y < (int)renderer->height) {
        
        // Draw node boundary with different colors for leaf vs internal nodes
        uint32_t color;
        if (node->is_leaf) {
            // Green for leaf nodes, intensity based on particle count
            uint8_t intensity = node->particle_count > 0 ? 255 : 128;
            color = 0xFF000000 | (intensity << 8); // Green channel
        } else {
            // Gray for internal nodes, darker for deeper levels
            color = COLOR_GRAY;
        }
        
        render_rectangle(renderer, (float)screen_x, (float)screen_y, screen_width, screen_height, color);
        
        // Draw center of mass as a small dot if the node has mass
        if (node->total_mass > 0.0f) {
            int com_x, com_y;
            world_to_screen(renderer, node->center_x, node->center_y, &com_x, &com_y);
            
            // Draw center of mass as a small cross
            uint32_t com_color = COLOR_YELLOW;
            physics_renderer_set_pixel(renderer, com_x, com_y, com_color);
            physics_renderer_set_pixel(renderer, com_x-1, com_y, com_color);
            physics_renderer_set_pixel(renderer, com_x+1, com_y, com_color);
            physics_renderer_set_pixel(renderer, com_x, com_y-1, com_color);
            physics_renderer_set_pixel(renderer, com_x, com_y+1, com_color);
        }
    }
    
    // Recursively render children
    if (!node->is_leaf) {
        for (int i = 0; i < 4; i++) {
            if (node->children[i]) {
                render_quadtree_node(renderer, node->children[i]);
            }
        }
    }
}

/**
 * @brief Render force vector for debugging
 * 
 * @param renderer Physics renderer instance
 * @param particle Particle to render force for
 * @param force_x Force X component
 * @param force_y Force Y component
 */
void render_force_vector(physics_renderer_t *renderer, const particle_t *particle, float force_x, float force_y) {
    if (!renderer || !particle || !particle->active) {
        return;
    }
    
    int start_x, start_y;
    world_to_screen(renderer, particle->x, particle->y, &start_x, &start_y);
    
    // Scale force for visualization
    float force_scale = 1000.0f; // Adjust this value as needed
    float end_world_x = particle->x + force_x * force_scale;
    float end_world_y = particle->y + force_y * force_scale;
    
    int end_x, end_y;
    world_to_screen(renderer, end_world_x, end_world_y, &end_x, &end_y);
    
    // Draw force vector as red line
    render_line(renderer, (float)start_x, (float)start_y, (float)end_x, (float)end_y, COLOR_RED);
}

/**
 * @brief Render velocity vector for debugging
 * 
 * @param renderer Physics renderer instance
 * @param particle Particle to render velocity for
 */
void render_velocity_vector(physics_renderer_t *renderer, const particle_t *particle) {
    if (!renderer || !particle || !particle->active) {
        return;
    }
    
    int start_x, start_y;
    world_to_screen(renderer, particle->x, particle->y, &start_x, &start_y);
    
    // Scale velocity for visualization
    float velocity_scale = 10.0f; // Adjust this value as needed
    float end_world_x = particle->x + particle->vx * velocity_scale;
    float end_world_y = particle->y + particle->vy * velocity_scale;
    
    int end_x, end_y;
    world_to_screen(renderer, end_world_x, end_world_y, &end_x, &end_y);
    
    // Draw velocity vector as blue line
    render_line(renderer, (float)start_x, (float)start_y, (float)end_x, (float)end_y, COLOR_BLUE);
}

/**
 * @brief Render simulation information and statistics
 * 
 * @param renderer Physics renderer instance
 * @param stats Rendering statistics
 */
void render_simulation_info(physics_renderer_t *renderer, const render_stats_t *stats) {
    if (!renderer || !stats) {
        return;
    }
    
    // Enhanced simulation info display with better visual indicators and text
    char buffer[64];
    
    // Draw background panel for info display
    vbe_fillrect(5, 5, 250, 120, 0x80000000); // Semi-transparent black background
    
    // Draw FPS indicator - green for good FPS, yellow for moderate, red for low FPS
    uint32_t fps_color;
    if (stats->fps >= 45.0f) {
        fps_color = COLOR_GREEN;
    } else if (stats->fps >= 25.0f) {
        fps_color = COLOR_YELLOW;
    } else {
        fps_color = COLOR_RED;
    }
    vbe_fillrect(10, 10, 60, 8, fps_color);
    
    // Display FPS text
    utoa((uint32_t)stats->fps, buffer);
    vbe_puts("FPS: ", 75, 10, COLOR_WHITE);
    vbe_puts(buffer, 110, 10, fps_color);
    
    // Draw particle count indicator with scaling
    uint32_t particle_width = stats->particles_rendered / 5;
    if (particle_width > 200) particle_width = 200; // Cap the width
    uint32_t particle_color = stats->particles_rendered > 500 ? COLOR_YELLOW : COLOR_WHITE;
    vbe_fillrect(10, 25, particle_width, 6, particle_color);
    
    // Display particle count text
    utoa(stats->particles_rendered, buffer);
    vbe_puts("Particles: ", 10, 35, COLOR_WHITE);
    vbe_puts(buffer, 90, 35, particle_color);
    
    // Draw frame time indicator (target: 16ms for 60fps, 33ms for 30fps)
    uint32_t time_width = stats->frame_time_ms > 50 ? 50 : stats->frame_time_ms;
    uint32_t time_color;
    if (stats->frame_time_ms <= 16) {
        time_color = COLOR_GREEN;
    } else if (stats->frame_time_ms <= 33) {
        time_color = COLOR_YELLOW;
    } else {
        time_color = COLOR_RED;
    }
    vbe_fillrect(10, 50, time_width * 2, 6, time_color); // Scale up for visibility
    
    // Display frame time text
    utoa(stats->frame_time_ms, buffer);
    vbe_puts("Frame Time: ", 10, 60, COLOR_WHITE);
    vbe_puts(buffer, 100, 60, time_color);
    vbe_puts("ms", 130, 60, COLOR_WHITE);
    
    // Draw quadtree nodes indicator if available
    if (stats->quadtree_nodes_rendered > 0) {
        uint32_t nodes_width = stats->quadtree_nodes_rendered / 2;
        if (nodes_width > 100) nodes_width = 100;
        vbe_fillrect(10, 75, nodes_width, 4, COLOR_CYAN);
        
        // Display quadtree nodes text
        utoa(stats->quadtree_nodes_rendered, buffer);
        vbe_puts("QTree Nodes: ", 10, 85, COLOR_WHITE);
        vbe_puts(buffer, 110, 85, COLOR_CYAN);
    }
    
    // Display total frame count
    utoa(stats->frame_count, buffer);
    vbe_puts("Frames: ", 10, 100, COLOR_WHITE);
    vbe_puts(buffer, 70, 100, COLOR_GRAY);
}

/**
 * @brief Begin frame rendering and start timing
 * 
 * @param renderer Physics renderer instance
 * @param stats Rendering statistics to update
 */
void physics_renderer_begin_frame(physics_renderer_t *renderer, render_stats_t *stats) {
    if (!renderer || !stats) {
        return;
    }
    
    // Record frame start time
    stats->last_frame_time = timer_ticks;
    
    // Initialize frame counters
    stats->particles_rendered = 0;
    stats->quadtree_nodes_rendered = 0;
}

/**
 * @brief End frame rendering and calculate statistics
 * 
 * @param renderer Physics renderer instance
 * @param stats Rendering statistics to update
 */
void physics_renderer_end_frame(physics_renderer_t *renderer, render_stats_t *stats) {
    if (!renderer || !stats) {
        return;
    }
    
    // Calculate frame time
    uint64_t current_time = timer_ticks;
    stats->frame_time_ms = (uint32_t)(current_time - stats->last_frame_time);
    
    // Update frame count for FPS calculation
    stats->frame_count++;
    
    // Calculate FPS every second (assuming timer_ticks is in milliseconds)
    if (stats->fps_timer_start == 0) {
        stats->fps_timer_start = current_time;
    }
    
    uint64_t elapsed_time = current_time - stats->fps_timer_start;
    if (elapsed_time >= 1000) { // 1 second
        stats->fps = (float)stats->frame_count * 1000.0f / (float)elapsed_time;
        stats->frame_count = 0;
        stats->fps_timer_start = current_time;
    }
    
    // Render debug info if enabled
    if (renderer->show_info) {
        render_simulation_info(renderer, stats);
    }
}

/**
 * @brief Present the rendered frame to the screen using VBE double buffering
 * 
 * @param renderer Physics renderer instance
 */
void physics_renderer_present_frame(physics_renderer_t *renderer) {
    if (!renderer) {
        return;
    }
    
    // Use VBE's double buffering system to present the frame
    vbe_flip();
}

/**
 * @brief Check if frame rate should be limited based on target FPS
 * 
 * @param renderer Physics renderer instance
 * @param target_fps Target frames per second
 * @return int 1 if frame should be delayed, 0 otherwise
 */
int physics_renderer_should_limit_framerate(physics_renderer_t *renderer, uint32_t target_fps) {
    if (!renderer || target_fps == 0) {
        return 0;
    }
    
    static uint64_t last_frame_time = 0;
    uint64_t current_time = timer_ticks;
    
    // Calculate target frame time in milliseconds
    uint32_t target_frame_time = 1000 / target_fps;
    
    // Check if enough time has passed since last frame
    if (last_frame_time == 0) {
        last_frame_time = current_time;
        return 0;
    }
    
    uint64_t elapsed = current_time - last_frame_time;
    if (elapsed < target_frame_time) {
        // Need to wait more
        return 1;
    }
    
    last_frame_time = current_time;
    return 0;
}

/**
 * @brief Wait for frame synchronization to maintain target FPS
 * 
 * @param renderer Physics renderer instance
 * @param target_fps Target frames per second
 */
void physics_renderer_wait_for_frame_sync(physics_renderer_t *renderer, uint32_t target_fps) {
    if (!renderer || target_fps == 0) {
        return;
    }
    
    // Simple busy wait for frame synchronization
    while (physics_renderer_should_limit_framerate(renderer, target_fps)) {
        // Small delay to prevent excessive CPU usage
        asm volatile ("pause");
    }
}

/**
 * @brief Initialize render statistics structure
 * 
 * @param stats Render statistics to initialize
 */
void physics_renderer_init_stats(render_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    stats->particles_rendered = 0;
    stats->quadtree_nodes_rendered = 0;
    stats->frame_time_ms = 0;
    stats->fps = 0.0f;
    stats->last_frame_time = 0;
    stats->frame_count = 0;
    stats->fps_timer_start = 0;
}

/**
 * @brief Enhanced render simulation with complete frame management
 * 
 * @param renderer Physics renderer instance
 * @param particles Particle system to render
 * @param tree Quadtree for optional debug rendering
 * @param stats Rendering statistics
 * @param target_fps Target frame rate (0 for unlimited)
 */
void physics_renderer_render_simulation_complete(physics_renderer_t *renderer, 
                                                const particle_system_t *particles, 
                                                const quadtree_t *tree,
                                                render_stats_t *stats,
                                                uint32_t target_fps) {
    if (!renderer) {
        return;
    }
    
    // Check frame rate limiting - if we should skip this frame, return early
    if (target_fps > 0 && physics_renderer_should_limit_framerate(renderer, target_fps)) {
        return; // Skip this frame to maintain target FPS
    }
    
    // Begin frame timing
    physics_renderer_begin_frame(renderer, stats);
    
    // Clear screen with background color
    physics_renderer_clear_screen(renderer);
    
    // Render particles first (background layer)
    if (particles) {
        physics_renderer_render_particles(renderer, particles);
        if (stats) {
            stats->particles_rendered = particles->count;
        }
    }
    
    // Render quadtree if debug mode is enabled (overlay layer)
    if (renderer->show_quadtree && tree) {
        physics_renderer_render_quadtree(renderer, tree);
        // Count quadtree nodes for statistics
        if (stats) {
            stats->quadtree_nodes_rendered = count_quadtree_nodes(tree->root);
        }
    }
    
    // Render debug vectors if enabled (top overlay layer)
    if (particles && (renderer->show_forces || renderer->show_velocities)) {
        for (uint32_t i = 0; i < particles->count; i++) {
            const particle_t *particle = &particles->particles[i];
            if (!particle->active) continue;
            
            if (renderer->show_velocities) {
                render_velocity_vector(renderer, particle);
            }
            
            // Force vectors would need force data passed in
            // This is a placeholder for when force data is available
        }
    }
    
    // End frame timing and render stats (top UI layer)
    physics_renderer_end_frame(renderer, stats);
    
    // Present the frame using VBE double buffering
    physics_renderer_present_frame(renderer);
    
    // Optional: Wait for frame synchronization if strict timing is needed
    if (target_fps > 0) {
        physics_renderer_wait_for_frame_sync(renderer, target_fps);
    }
}