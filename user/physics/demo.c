#include "demo.h"
#include "kernel_integration.h"
#include "physics.h"
#include "../kernel.h"
#include "../video/vbe/vbe.h"
#include "../stdio/stdio.h"
#include <stddef.h>

// Kernel environment - no standard library includes

// Global demo control instance
demo_control_t *physics_demo_control = NULL;

// Demo statistics
static demo_stats_t demo_statistics;

// Random number generator state
static uint32_t demo_rng_seed = 12345;

// Scenario names and descriptions
static const char* scenario_names[] = {
    "Random Particles",
    "Orbital System", 
    "Collision Demo",
    "Gravity Well",
    "Binary System",
    "Particle Fountain",
    "Galaxy Spiral",
    "Particle Cluster",
    "Chain Reaction",
    "Solar System"
};

static const char* scenario_descriptions[] = {
    "Randomly distributed particles with gravitational interactions",
    "Planetary system with stable orbital mechanics",
    "High-energy particle collisions with momentum conservation",
    "Massive central body attracting surrounding particles",
    "Two massive bodies orbiting each other with debris",
    "Continuous stream of particles from a central source",
    "Spiral galaxy formation with rotating arms",
    "Dense particle cluster with internal dynamics",
    "Cascading collision chain reactions",
    "Realistic solar system with planets and moons"
};

// Forward declarations for helper functions
static uint32_t demo_simple_rand(void);
static void demo_render_text_line(const char *text, uint32_t x, uint32_t y, uint32_t color);
static void demo_render_stats_overlay(void);
static void demo_update_fps_counter(void);
static uint32_t get_current_time_ms(void);

// Simple math functions for kernel environment
static float simple_cosf(float x) {
    // Simple cosine approximation
    while (x > 3.14159f) x -= 6.28318f;
    while (x < -3.14159f) x += 6.28318f;
    return 1.0f - (x*x)/2.0f + (x*x*x*x)/24.0f;
}

static float simple_sinf(float x) {
    // Simple sine approximation
    while (x > 3.14159f) x -= 6.28318f;
    while (x < -3.14159f) x += 6.28318f;
    return x - (x*x*x)/6.0f + (x*x*x*x*x)/120.0f;
}

static float simple_sqrtf(float x) {
    // Simple square root approximation using Newton's method
    if (x <= 0.0f) return 0.0f;
    float guess = x / 2.0f;
    for (int i = 0; i < 10; i++) {
        guess = (guess + x / guess) / 2.0f;
    }
    return guess;
}

// Basic demo functions
int physics_demo_init(void) {
    if (physics_demo_control != NULL) {
        return 1;
    }
    
    physics_kernel_error_t kernel_result = physics_kernel_init();
    if (kernel_result != PHYSICS_KERNEL_SUCCESS) {
        return 0;
    }
    
    physics_demo_control = (demo_control_t*)kernel_malloc(sizeof(demo_control_t));
    if (!physics_demo_control) {
        physics_kernel_shutdown();
        return 0;
    }
    
    physics_demo_control->current_scenario = DEMO_SCENARIO_RANDOM_PARTICLES;
    physics_demo_control->demo_running = 0;
    physics_demo_control->demo_paused = 0;
    physics_demo_control->show_help = 0;
    physics_demo_control->show_debug_info = 1;
    physics_demo_control->auto_cycle_scenarios = 0;
    physics_demo_control->scenario_timer = 0;
    physics_demo_control->auto_cycle_interval = 300;
    physics_demo_control->gravity_multiplier = 1.0f;
    physics_demo_control->time_scale = 1.0f;
    physics_demo_control->particle_count_override = 0;
    physics_demo_control->collision_enabled_override = 1;
    
    physics_demo_reset_stats();
    return 1;
}

void physics_demo_shutdown(void) {
    if (!physics_demo_control) {
        return;
    }
    
    physics_demo_stop();
    physics_kernel_shutdown();
    kernel_free(physics_demo_control);
    physics_demo_control = NULL;
}

int physics_demo_start(demo_scenario_t scenario) {
    if (!physics_demo_control) {
        return 0;
    }
    
    if (!physics_demo_load_scenario(scenario)) {
        return 0;
    }
    
    physics_kernel_error_t result = physics_kernel_start_simulation();
    if (result != PHYSICS_KERNEL_SUCCESS) {
        return 0;
    }
    
    physics_demo_control->demo_running = 1;
    physics_demo_control->demo_paused = 0;
    physics_demo_control->scenario_timer = 0;
    
    physics_demo_reset_stats();
    return 1;
}

void physics_demo_stop(void) {
    if (physics_demo_control) {
        physics_demo_control->demo_running = 0;
        physics_demo_control->demo_paused = 0;
        physics_kernel_pause_simulation();
    }
}

int physics_demo_load_scenario(demo_scenario_t scenario) {
    if (!physics_demo_control || scenario >= DEMO_SCENARIO_COUNT) {
        return 0;
    }
    
    physics_config_t config = physics_config_preset_default();
    
    physics_kernel_error_t result = physics_kernel_create_simulation(&config);
    if (result != PHYSICS_KERNEL_SUCCESS) {
        return 0;
    }
    
    int setup_result = 0;
    switch (scenario) {
        case DEMO_SCENARIO_RANDOM_PARTICLES:
            setup_result = physics_demo_setup_random_particles(config.max_particles);
            break;
        case DEMO_SCENARIO_GALAXY_SPIRAL:
            setup_result = physics_demo_setup_galaxy_spiral(config.max_particles);
            break;
        case DEMO_SCENARIO_PARTICLE_CLUSTER:
            setup_result = physics_demo_setup_particle_cluster(config.max_particles);
            break;
        case DEMO_SCENARIO_CHAIN_REACTION:
            setup_result = physics_demo_setup_chain_reaction(config.max_particles);
            break;
        case DEMO_SCENARIO_SOLAR_SYSTEM:
            setup_result = physics_demo_setup_solar_system();
            break;
        default:
            setup_result = physics_demo_setup_random_particles(config.max_particles);
            break;
    }
    
    if (setup_result) {
        physics_demo_control->current_scenario = scenario;
        return 1;
    }
    
    return 0;
}

// Scenario setup implementations
int physics_demo_setup_random_particles(uint32_t count) {
    physics_kernel_error_t result = physics_kernel_init_random_particles(count);
    return (result == PHYSICS_KERNEL_SUCCESS) ? 1 : 0;
}

int physics_demo_setup_galaxy_spiral(uint32_t particle_count) {
    float center_x = 640.0f;
    float center_y = 400.0f;
    
    for (uint32_t i = 0; i < particle_count; i++) {
        float angle = physics_demo_get_random_float(0.0f, 6.28f);
        float r = physics_demo_get_random_float(50.0f, 300.0f);
        
        float x = center_x + r * simple_cosf(angle);
        float y = center_y + r * simple_sinf(angle);
        
        float orbital_speed = simple_sqrtf(1000.0f / r) * 0.5f;
        float vx = -orbital_speed * simple_sinf(angle);
        float vy = orbital_speed * simple_cosf(angle);
        
        float mass = physics_demo_get_random_float(1.0f, 5.0f);
        physics_kernel_add_particle(x, y, mass, vx, vy);
    }
    
    return 1;
}

int physics_demo_setup_particle_cluster(uint32_t particle_count) {
    float center_x = 640.0f;
    float center_y = 400.0f;
    
    for (uint32_t i = 0; i < particle_count; i++) {
        float angle = physics_demo_get_random_float(0.0f, 6.28f);
        float r = physics_demo_get_random_float(10.0f, 100.0f);
        
        float x = center_x + r * simple_cosf(angle);
        float y = center_y + r * simple_sinf(angle);
        
        float mass = physics_demo_get_random_float(1.0f, 10.0f);
        physics_kernel_add_particle(x, y, mass, 0.0f, 0.0f);
    }
    
    return 1;
}

int physics_demo_setup_chain_reaction(uint32_t particle_count) {
    for (uint32_t i = 0; i < particle_count; i++) {
        float x = physics_demo_get_random_float(100.0f, 1180.0f);
        float y = physics_demo_get_random_float(100.0f, 700.0f);
        float mass = physics_demo_get_random_float(5.0f, 15.0f);
        
        physics_kernel_add_particle(x, y, mass, 0.0f, 0.0f);
    }
    
    return 1;
}

int physics_demo_setup_solar_system(void) {
    // Add sun at center
    physics_kernel_add_particle(640.0f, 400.0f, 1000.0f, 0.0f, 0.0f);
    
    // Add planets
    float distances[] = {100.0f, 150.0f, 200.0f, 300.0f};
    float masses[] = {10.0f, 15.0f, 12.0f, 8.0f};
    
    for (int i = 0; i < 4; i++) {
        float x = 640.0f + distances[i];
        float y = 400.0f;
        float orbital_speed = simple_sqrtf(1000.0f / distances[i]) * 0.8f;
        
        physics_kernel_add_particle(x, y, masses[i], 0.0f, orbital_speed);
    }
    
    return 1;
}

// Utility functions
float physics_demo_get_random_float(float min, float max) {
    demo_rng_seed = demo_rng_seed * 1103515245 + 12345;
    float normalized = (float)(demo_rng_seed & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    return min + normalized * (max - min);
}

uint32_t physics_demo_get_random_uint(uint32_t min, uint32_t max) {
    demo_rng_seed = demo_rng_seed * 1103515245 + 12345;
    return min + (demo_rng_seed % (max - min));
}

uint32_t physics_demo_get_color_for_mass(float mass) {
    uint8_t intensity = (uint8_t)(mass * 25.0f);
    if (intensity > 255) intensity = 255;
    return 0xFF000000 | (intensity << 16) | (intensity << 8) | intensity;
}

uint32_t physics_demo_get_color_for_velocity(float vx, float vy) {
    float speed = simple_sqrtf(vx*vx + vy*vy);
    uint8_t intensity = (uint8_t)(speed * 2.0f);
    if (intensity > 255) intensity = 255;
    return 0xFF000000 | (intensity << 16);
}

void physics_demo_reset_stats(void) {
    demo_statistics.frames_rendered = 0;
    demo_statistics.total_particles = 0;
    demo_statistics.active_particles = 0;
    demo_statistics.collisions_detected = 0;
    demo_statistics.quadtree_nodes_used = 0;
    demo_statistics.average_fps = 0.0f;
    demo_statistics.current_fps = 0.0f;
    demo_statistics.memory_usage = 0;
    demo_statistics.simulation_time = 0.0f;
}

// Helper functions
static uint32_t demo_simple_rand(void) {
    demo_rng_seed = demo_rng_seed * 1103515245 + 12345;
    return demo_rng_seed;
}

static void demo_render_text_line(const char *text, uint32_t x, uint32_t y, uint32_t color) {
    // Simple text rendering placeholder
    (void)text; (void)color; // Suppress unused warnings
    uint32_t len = 10; // Approximate text length
    vbe_fillrect(x, y + 8, len * 8, 2, color);
}

static void demo_render_stats_overlay(void) {
    // Simple stats overlay
    vbe_fillrect(10, 60, 200, 100, 0x80000000);
}

static void demo_update_fps_counter(void) {
    static uint32_t frame_count = 0;
    static uint32_t last_time = 0;
    
    frame_count++;
    uint32_t current_time = get_current_time_ms();
    
    if (current_time - last_time >= 1000) {
        demo_statistics.current_fps = (float)frame_count * 1000.0f / (current_time - last_time);
        frame_count = 0;
        last_time = current_time;
    }
}

static uint32_t get_current_time_ms(void) {
    static uint32_t fake_time = 0;
    return ++fake_time;
}

// Missing demo functions that are called from kernel
void physics_demo_update(void) {
    // Simple placeholder update function
    if (physics_demo_control && physics_demo_control->demo_running) {
        // Update demo state
        physics_demo_control->scenario_timer++;
    }
}

void physics_demo_render(void) {
    // Simple placeholder render function
    if (physics_demo_control && physics_demo_control->demo_running) {
        // Render demo
        vbe_fillrect(10, 10, 200, 50, 0xFF0000FF); // Blue rectangle as placeholder
    }
}