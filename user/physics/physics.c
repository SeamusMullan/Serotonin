#include "physics.h"
#include "particle.h"
#include "quadtree.h"
#include "collision.h"
#include "renderer.h"
#include "memory.h"
#include "performance.h"
#include "../kernel.h"
#include <stddef.h>

// Default configuration values
static const physics_config_t DEFAULT_CONFIG = {
    .max_particles = 100,
    .world_width = 1280.0f,
    .world_height = 800.0f,
    .gravity_constant = 6.67430e-11f,
    .theta = 0.5f,
    .restitution = 0.8f,
    .softening = 1.0f,
    .time_step = 0.016f,  // ~60 FPS
    .target_fps = 60,
    .enable_collisions = 1,
    .debug_mode = 0
};

// Internal helper functions
static int physics_validate_config(const physics_config_t *config);
static physics_memory_t* physics_memory_create_internal(const physics_config_t *config);
static void physics_memory_destroy_internal(physics_memory_t *memory);

physics_config_t physics_config_default(void) {
    return DEFAULT_CONFIG;
}

physics_simulation_t* physics_simulation_create(physics_config_t *config) {
    if (!config) {
        return NULL;
    }
    
    // Validate configuration parameters
    if (!physics_validate_config(config)) {
        return NULL;
    }
    
    // Allocate main simulation structure
    physics_simulation_t *sim = (physics_simulation_t*)kernel_malloc(sizeof(physics_simulation_t));
    if (!sim) {
        return NULL;
    }
    
    // Initialize simulation structure
    sim->particles = NULL;
    sim->quadtree = NULL;
    sim->collisions = NULL;
    sim->renderer = NULL;
    sim->memory = NULL;
    sim->profiler = NULL;
    sim->quality = NULL;
    sim->config = *config;
    sim->gravitational_constant = config->gravity_constant;
    sim->theta = config->theta;
    sim->restitution = config->restitution;
    sim->softening = config->softening;
    sim->time_step = config->time_step;
    sim->collision_enabled = config->enable_collisions;
    sim->debug_render = config->debug_mode;
    sim->initialized = 0;
    
    // Create memory management system
    sim->memory = physics_memory_create_internal(config);
    if (!sim->memory) {
        kernel_free(sim);
        return NULL;
    }
    
    // Create particle system
    sim->particles = particle_system_create(config->max_particles);
    if (!sim->particles) {
        physics_memory_destroy_internal(sim->memory);
        kernel_free(sim);
        return NULL;
    }
    
    // Set particle system bounds
    particle_set_bounds(sim->particles, 0.0f, 0.0f, config->world_width, config->world_height);
    
    // Create quadtree for spatial partitioning
    // Allocate enough nodes for worst-case scenario (4 nodes per particle)
    uint32_t max_nodes = config->max_particles * 4;
    sim->quadtree = quadtree_create(config->world_width / 2.0f, config->world_height / 2.0f, 
                                   config->world_width, config->world_height, max_nodes);
    if (!sim->quadtree) {
        particle_system_destroy(sim->particles);
        physics_memory_destroy_internal(sim->memory);
        kernel_free(sim);
        return NULL;
    }
    
    // Create collision system if enabled
    if (config->enable_collisions) {
        // Estimate maximum collisions as 10% of max particle pairs
        uint32_t max_collisions = (config->max_particles * config->max_particles) / 10;
        uint32_t max_pairs = config->max_particles * config->max_particles / 2;
        
        sim->collisions = collision_system_create(max_collisions, max_pairs);
        if (!sim->collisions) {
            quadtree_destroy(sim->quadtree);
            particle_system_destroy(sim->particles);
            physics_memory_destroy_internal(sim->memory);
            kernel_free(sim);
            return NULL;
        }
    }
    
    // Create performance profiler
    sim->profiler = performance_profiler_create((float)config->target_fps);
    if (!sim->profiler) {
        if (sim->collisions) collision_system_destroy(sim->collisions);
        quadtree_destroy(sim->quadtree);
        particle_system_destroy(sim->particles);
        physics_memory_destroy_internal(sim->memory);
        kernel_free(sim);
        return NULL;
    }
    
    // Create adaptive quality system
    sim->quality = adaptive_quality_create((float)config->target_fps);
    if (!sim->quality) {
        performance_profiler_destroy(sim->profiler);
        if (sim->collisions) collision_system_destroy(sim->collisions);
        quadtree_destroy(sim->quadtree);
        particle_system_destroy(sim->particles);
        physics_memory_destroy_internal(sim->memory);
        kernel_free(sim);
        return NULL;
    }
    
    sim->initialized = 1;
    return sim;
}

void physics_simulation_destroy(physics_simulation_t *sim) {
    if (!sim) {
        return;
    }
    
    // Clean up all subsystems
    if (sim->quality) {
        adaptive_quality_destroy(sim->quality);
    }
    
    if (sim->profiler) {
        performance_profiler_destroy(sim->profiler);
    }
    
    if (sim->collisions) {
        collision_system_destroy(sim->collisions);
    }
    
    if (sim->quadtree) {
        quadtree_destroy(sim->quadtree);
    }
    
    if (sim->particles) {
        particle_system_destroy(sim->particles);
    }
    
    if (sim->memory) {
        physics_memory_destroy_internal(sim->memory);
    }
    
    // Free main structure
    kernel_free(sim);
}

void physics_simulation_step(physics_simulation_t *sim) {
    if (!sim || !sim->initialized) {
        return;
    }
    
    // Begin performance profiling
    if (sim->profiler) {
        performance_profiler_begin_frame(sim->profiler);
    }
    
    // Step 1: Clear accumulated forces from previous frame
    particle_clear_forces(sim->particles);
    
    // Step 2: Rebuild quadtree with current particle positions
    quadtree_rebuild_with_mass(sim->quadtree, sim->particles);
    if (sim->profiler) {
        performance_profiler_checkpoint(sim->profiler, "quadtree");
    }
    
    // Step 3: Calculate gravitational forces using Barnes-Hut algorithm
    quadtree_calculate_forces(sim->quadtree, sim->particles, sim->theta, 
                             sim->gravitational_constant, sim->softening);
    if (sim->profiler) {
        performance_profiler_checkpoint(sim->profiler, "force");
    }
    
    // Step 4: Detect and resolve collisions if enabled
    if (sim->collision_enabled && sim->collisions) {
        // Clear previous collision data
        collision_system_clear(sim->collisions);
        
        // Broad phase collision detection using quadtree
        collision_detect_broad_phase(sim->collisions, sim->quadtree, sim->particles);
        
        // Narrow phase collision detection
        collision_detect_narrow_phase(sim->collisions, sim->particles);
        
        // Resolve all detected collisions
        collision_resolve_all(sim->collisions, sim->particles, sim->restitution);
    }
    if (sim->profiler) {
        performance_profiler_checkpoint(sim->profiler, "collision");
    }
    
    // Step 5: Integrate particle physics (update positions and velocities)
    particle_update_physics(sim->particles, sim->time_step);
    if (sim->profiler) {
        performance_profiler_checkpoint(sim->profiler, "integration");
    }
    
    // Step 6: Update adaptive quality based on performance
    if (sim->quality && sim->profiler) {
        performance_metrics_t metrics;
        performance_get_metrics(sim->profiler, &metrics);
        adaptive_quality_update(sim->quality, &metrics);
        adaptive_quality_apply(sim->quality, sim);
    }
    
    // End performance profiling
    if (sim->profiler) {
        performance_profiler_end_frame(sim->profiler);
    }
}

void physics_simulation_render(physics_simulation_t *sim) {
    if (!sim || !sim->initialized || !sim->renderer) {
        return;
    }
    
    // Clear the screen
    physics_renderer_clear_screen(sim->renderer);
    
    // Render the complete simulation
    physics_renderer_render_simulation(sim->renderer, sim->particles, sim->quadtree);
}

// Internal helper function implementations

static int physics_validate_config(const physics_config_t *config) {
    // Validate particle count
    if (config->max_particles == 0 || config->max_particles > 10000) {
        return 0;
    }
    
    // Validate world dimensions
    if (config->world_width <= 0.0f || config->world_height <= 0.0f) {
        return 0;
    }
    
    // Validate Barnes-Hut theta parameter
    if (config->theta < 0.1f || config->theta > 5.0f) {
        return 0;
    }
    
    // Validate restitution coefficient
    if (config->restitution < 0.0f || config->restitution > 1.0f) {
        return 0;
    }
    
    // Validate time step
    if (config->time_step <= 0.0f || config->time_step > 1.0f) {
        return 0;
    }
    
    // Validate softening parameter
    if (config->softening < 0.0f) {
        return 0;
    }
    
    return 1;
}

static physics_memory_t* physics_memory_create_internal(const physics_config_t *config) {
    // Calculate pool sizes
    uint32_t max_quadtree_nodes = config->max_particles * 4;
    uint32_t max_collisions = config->enable_collisions ? 
        (config->max_particles * config->max_particles) / 10 : 0;
    
    // Use the new advanced memory pool system from memory.c
    return physics_memory_create(config->max_particles, max_quadtree_nodes, max_collisions);
}

static void physics_memory_destroy_internal(physics_memory_t *memory) {
    // Use the new advanced memory pool system from memory.c
    physics_memory_destroy(memory);
}

// Advanced simulation control functions

// Frame rate control structure
typedef struct {
    uint32_t target_fps;
    float target_frame_time;
    uint32_t last_frame_time;
    uint32_t frame_count;
    float actual_fps;
    uint8_t frame_limiting_enabled;
} frame_controller_t;

// Simulation statistics structure
typedef struct {
    uint32_t total_steps;
    uint32_t total_particles;
    uint32_t total_collisions;
    uint32_t quadtree_nodes_used;
    float average_step_time;
    float min_step_time;
    float max_step_time;
} simulation_stats_t;

// Extended simulation functions
int physics_simulation_step_with_timing(physics_simulation_t *sim, float *step_time);
void physics_simulation_set_time_step(physics_simulation_t *sim, float time_step);
void physics_simulation_set_frame_rate(physics_simulation_t *sim, uint32_t target_fps);
int physics_simulation_validate_parameters(physics_simulation_t *sim);
void physics_simulation_get_stats(physics_simulation_t *sim, simulation_stats_t *stats);

// Frame rate control functions
static frame_controller_t* frame_controller_create(uint32_t target_fps);
static void frame_controller_destroy(frame_controller_t *controller);
static void frame_controller_begin_frame(frame_controller_t *controller);
static void frame_controller_end_frame(frame_controller_t *controller);
static uint8_t frame_controller_should_render(frame_controller_t *controller);

// Parameter bounds checking functions
static int validate_gravity_constant(float gravity);
static int validate_theta_parameter(float theta);
static int validate_restitution(float restitution);
static int validate_time_step(float time_step);
static int validate_world_bounds(float width, float height);

int physics_simulation_step_with_timing(physics_simulation_t *sim, float *step_time) {
    if (!sim || !sim->initialized) {
        if (step_time) *step_time = 0.0f;
        return 0;
    }
    
    // Record start time (simplified - in real kernel would use proper timing)
    uint32_t start_time = 0; // Would use kernel timing functions
    
    // Validate simulation parameters before stepping
    if (!physics_simulation_validate_parameters(sim)) {
        if (step_time) *step_time = 0.0f;
        return 0;
    }
    
    // Perform the simulation step
    physics_simulation_step(sim);
    
    // Record end time and calculate step duration
    uint32_t end_time = 0; // Would use kernel timing functions
    if (step_time) {
        *step_time = (float)(end_time - start_time) / 1000.0f; // Convert to seconds
    }
    
    return 1;
}

void physics_simulation_set_time_step(physics_simulation_t *sim, float time_step) {
    if (!sim || !validate_time_step(time_step)) {
        return;
    }
    
    sim->time_step = time_step;
    sim->config.time_step = time_step;
}

void physics_simulation_set_frame_rate(physics_simulation_t *sim, uint32_t target_fps) {
    if (!sim || target_fps == 0 || target_fps > 1000) {
        return;
    }
    
    sim->config.target_fps = target_fps;
    // Update time step to match target frame rate
    sim->time_step = 1.0f / (float)target_fps;
    sim->config.time_step = sim->time_step;
}

int physics_simulation_validate_parameters(physics_simulation_t *sim) {
    if (!sim) {
        return 0;
    }
    
    // Validate all critical parameters
    if (!validate_gravity_constant(sim->gravitational_constant)) {
        return 0;
    }
    
    if (!validate_theta_parameter(sim->theta)) {
        return 0;
    }
    
    if (!validate_restitution(sim->restitution)) {
        return 0;
    }
    
    if (!validate_time_step(sim->time_step)) {
        return 0;
    }
    
    if (!validate_world_bounds(sim->config.world_width, sim->config.world_height)) {
        return 0;
    }
    
    // Validate softening parameter
    if (sim->softening < 0.0f || sim->softening > 100.0f) {
        return 0;
    }
    
    // Validate particle system state
    if (!sim->particles || sim->particles->count > sim->particles->capacity) {
        return 0;
    }
    
    // Validate quadtree state
    if (!sim->quadtree) {
        return 0;
    }
    
    return 1;
}

void physics_simulation_get_stats(physics_simulation_t *sim, simulation_stats_t *stats) {
    if (!sim || !stats) {
        return;
    }
    
    // Initialize stats structure
    stats->total_steps = 0;
    stats->total_particles = 0;
    stats->total_collisions = 0;
    stats->quadtree_nodes_used = 0;
    stats->average_step_time = 0.0f;
    stats->min_step_time = 0.0f;
    stats->max_step_time = 0.0f;
    
    if (sim->particles) {
        stats->total_particles = sim->particles->count;
    }
    
    if (sim->collisions) {
        stats->total_collisions = sim->collisions->count;
    }
    
    if (sim->quadtree) {
        stats->quadtree_nodes_used = sim->quadtree->pool_index;
    }
}

// Complete simulation step with all components integrated
void physics_simulation_step_complete(physics_simulation_t *sim) {
    if (!sim || !sim->initialized) {
        return;
    }
    
    // Validate parameters before each step
    if (!physics_simulation_validate_parameters(sim)) {
        return;
    }
    
    // Step 1: Clear accumulated forces from previous frame
    particle_clear_forces(sim->particles);
    
    // Step 2: Clear and rebuild quadtree with current particle positions
    quadtree_clear(sim->quadtree);
    quadtree_rebuild_with_mass(sim->quadtree, sim->particles);
    
    // Step 3: Calculate gravitational forces using Barnes-Hut algorithm
    quadtree_calculate_forces(sim->quadtree, sim->particles, sim->theta, 
                             sim->gravitational_constant, sim->softening);
    
    // Step 4: Handle collision detection and response if enabled
    if (sim->collision_enabled && sim->collisions) {
        // Clear previous collision data
        collision_system_clear(sim->collisions);
        
        // Broad phase: Use quadtree to find potential collision pairs
        collision_detect_broad_phase(sim->collisions, sim->quadtree, sim->particles);
        
        // Narrow phase: Precise collision detection
        collision_detect_narrow_phase(sim->collisions, sim->particles);
        
        // Resolve all detected collisions with momentum conservation
        collision_resolve_all(sim->collisions, sim->particles, sim->restitution);
    }
    
    // Step 5: Integrate particle physics (positions and velocities)
    particle_update_physics(sim->particles, sim->time_step);
    
    // Step 6: Apply boundary conditions (keep particles in world bounds)
    for (uint32_t i = 0; i < sim->particles->count; i++) {
        particle_t *p = &sim->particles->particles[i];
        if (!p->active) continue;
        
        // Wrap or bounce particles at world boundaries
        if (p->x < 0.0f) {
            p->x = 0.0f;
            p->vx = -p->vx * sim->restitution;
        } else if (p->x > sim->config.world_width) {
            p->x = sim->config.world_width;
            p->vx = -p->vx * sim->restitution;
        }
        
        if (p->y < 0.0f) {
            p->y = 0.0f;
            p->vy = -p->vy * sim->restitution;
        } else if (p->y > sim->config.world_height) {
            p->y = sim->config.world_height;
            p->vy = -p->vy * sim->restitution;
        }
    }
}

// Parameter validation helper functions
static int validate_gravity_constant(float gravity) {
    // Allow a reasonable range for gravitational constant
    return (gravity >= 0.0f && gravity <= 1.0f);
}

static int validate_theta_parameter(float theta) {
    // Barnes-Hut theta should be between 0.1 and 5.0
    return (theta >= 0.1f && theta <= 5.0f);
}

static int validate_restitution(float restitution) {
    // Restitution coefficient should be between 0 and 1
    return (restitution >= 0.0f && restitution <= 1.0f);
}

static int validate_time_step(float time_step) {
    // Time step should be positive and reasonable
    return (time_step > 0.0f && time_step <= 1.0f);
}

static int validate_world_bounds(float width, float height) {
    // World dimensions should be positive and reasonable
    return (width > 0.0f && height > 0.0f && width <= 10000.0f && height <= 10000.0f);
}

// Configuration update functions
void physics_simulation_update_config(physics_simulation_t *sim, const physics_config_t *new_config) {
    if (!sim || !new_config || !physics_validate_config(new_config)) {
        return;
    }
    
    // Update configuration
    sim->config = *new_config;
    sim->gravitational_constant = new_config->gravity_constant;
    sim->theta = new_config->theta;
    sim->restitution = new_config->restitution;
    sim->softening = new_config->softening;
    sim->time_step = new_config->time_step;
    sim->collision_enabled = new_config->enable_collisions;
    sim->debug_render = new_config->debug_mode;
    
    // Update particle system bounds if they changed
    if (sim->particles) {
        particle_set_bounds(sim->particles, 0.0f, 0.0f, 
                           new_config->world_width, new_config->world_height);
    }
}

// Simulation state management
void physics_simulation_pause(physics_simulation_t *sim) {
    if (sim) {
        sim->initialized = 0;
    }
}

void physics_simulation_resume(physics_simulation_t *sim) {
    if (sim && physics_simulation_validate_parameters(sim)) {
        sim->initialized = 1;
    }
}

void physics_simulation_reset(physics_simulation_t *sim) {
    if (!sim) {
        return;
    }
    
    // Clear all particles
    if (sim->particles) {
        sim->particles->count = 0;
        particle_clear_forces(sim->particles);
    }
    
    // Clear quadtree
    if (sim->quadtree) {
        quadtree_clear(sim->quadtree);
    }
    
    // Clear collisions
    if (sim->collisions) {
        collision_system_clear(sim->collisions);
    }
}

// Utility function to check if simulation is ready
int physics_simulation_is_ready(const physics_simulation_t *sim) {
    return (sim && sim->initialized && sim->particles && sim->quadtree);
}

// Performance monitoring wrapper functions

performance_profiler_t* physics_performance_create_profiler(float target_fps) {
    return performance_profiler_create(target_fps);
}

void physics_performance_destroy_profiler(performance_profiler_t *profiler) {
    performance_profiler_destroy(profiler);
}

adaptive_quality_t* physics_performance_create_quality(float target_fps) {
    return adaptive_quality_create(target_fps);
}

void physics_performance_destroy_quality(adaptive_quality_t *quality) {
    adaptive_quality_destroy(quality);
}

void physics_performance_begin_frame(physics_simulation_t *sim) {
    if (sim && sim->profiler) {
        performance_profiler_begin_frame(sim->profiler);
    }
}

void physics_performance_end_frame(physics_simulation_t *sim) {
    if (sim && sim->profiler) {
        performance_profiler_end_frame(sim->profiler);
    }
}

void physics_performance_get_metrics(const physics_simulation_t *sim, performance_metrics_t *metrics) {
    if (sim && sim->profiler && metrics) {
        performance_get_metrics(sim->profiler, metrics);
    }
}

void physics_performance_optimize_simulation(physics_simulation_t *sim) {
    if (!sim || !sim->profiler) {
        return;
    }
    
    performance_metrics_t metrics;
    performance_get_metrics(sim->profiler, &metrics);
    optimize_simulation_parameters(sim, &metrics);
}

int physics_performance_run_benchmarks(void) {
    // Run comprehensive performance benchmarks
    int results = 1;
    
    // Benchmark force calculations
    results &= benchmark_force_calculations(100, 10);
    
    // Benchmark collision detection
    results &= benchmark_collision_detection(50, 10);
    
    // Benchmark memory operations
    results &= benchmark_memory_operations(1000, 10);
    
    return results;
}

// Configuration System Implementation

physics_config_t* physics_config_create(void) {
    physics_config_t *config = (physics_config_t*)kernel_malloc(sizeof(physics_config_t));
    if (!config) {
        return NULL;
    }
    
    // Initialize with default values
    *config = physics_config_preset_default();
    
    return config;
}

void physics_config_destroy(physics_config_t *config) {
    if (config) {
        kernel_free(config);
    }
}

int physics_config_validate(const physics_config_t *config) {
    if (!config) {
        return 0;
    }
    
    // Validate particle count (1 to 10000)
    if (config->max_particles == 0 || config->max_particles > 10000) {
        return 0;
    }
    
    // Validate world dimensions (must be positive and reasonable)
    if (config->world_width <= 0.0f || config->world_height <= 0.0f ||
        config->world_width > 10000.0f || config->world_height > 10000.0f) {
        return 0;
    }
    
    // Validate gravitational constant (0.0 to 1.0)
    if (config->gravity_constant < 0.0f || config->gravity_constant > 1.0f) {
        return 0;
    }
    
    // Validate Barnes-Hut theta parameter (0.1 to 5.0)
    if (config->theta < 0.1f || config->theta > 5.0f) {
        return 0;
    }
    
    // Validate restitution coefficient (0.0 to 1.0)
    if (config->restitution < 0.0f || config->restitution > 1.0f) {
        return 0;
    }
    
    // Validate softening parameter (must be non-negative)
    if (config->softening < 0.0f || config->softening > 100.0f) {
        return 0;
    }
    
    // Validate time step (must be positive and reasonable)
    if (config->time_step <= 0.0f || config->time_step > 1.0f) {
        return 0;
    }
    
    // Validate target FPS (1 to 1000)
    if (config->target_fps == 0 || config->target_fps > 1000) {
        return 0;
    }
    
    return 1;
}

physics_config_t physics_config_preset_default(void) {
    physics_config_t config = {
        .max_particles = 100,
        .world_width = 1280.0f,
        .world_height = 800.0f,
        .gravity_constant = 6.67430e-5f,  // Scaled for simulation
        .theta = 0.5f,
        .restitution = 0.8f,
        .softening = 1.0f,
        .time_step = 0.016f,  // ~60 FPS
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    return config;
}

physics_config_t physics_config_preset_high_performance(void) {
    physics_config_t config = {
        .max_particles = 50,
        .world_width = 1280.0f,
        .world_height = 800.0f,
        .gravity_constant = 6.67430e-5f,
        .theta = 1.0f,  // Less accurate but faster
        .restitution = 0.9f,
        .softening = 2.0f,
        .time_step = 0.020f,  // 50 FPS for better performance
        .target_fps = 50,
        .enable_collisions = 0,  // Disable collisions for speed
        .debug_mode = 0
    };
    return config;
}

physics_config_t physics_config_preset_orbital_system(void) {
    physics_config_t config = {
        .max_particles = 20,
        .world_width = 1280.0f,
        .world_height = 800.0f,
        .gravity_constant = 1.0e-3f,  // Strong gravity for orbital mechanics
        .theta = 0.3f,  // High accuracy for stable orbits
        .restitution = 0.1f,  // Low bounce for realistic collisions
        .softening = 5.0f,  // Prevent singularities
        .time_step = 0.008f,  // Small time step for stability
        .target_fps = 120,
        .enable_collisions = 1,
        .debug_mode = 1  // Show quadtree for visualization
    };
    return config;
}

physics_config_t physics_config_preset_collision_demo(void) {
    physics_config_t config = {
        .max_particles = 200,
        .world_width = 1280.0f,
        .world_height = 800.0f,
        .gravity_constant = 1.0e-6f,  // Minimal gravity
        .theta = 0.8f,
        .restitution = 0.95f,  // High bounce for interesting collisions
        .softening = 0.5f,
        .time_step = 0.016f,
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    return config;
}

physics_config_t physics_config_preset_stress_test(void) {
    physics_config_t config = {
        .max_particles = 1000,
        .world_width = 2000.0f,  // Larger world
        .world_height = 1200.0f,
        .gravity_constant = 1.0e-4f,
        .theta = 1.5f,  // Lower accuracy for performance
        .restitution = 0.7f,
        .softening = 3.0f,
        .time_step = 0.025f,  // Larger time step
        .target_fps = 30,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    return config;
}

// Helper functions for simple string operations
static int simple_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static uint32_t simple_atoi(const char *str) {
    uint32_t result = 0;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result;
}

static float simple_atof(const char *str) {
    float result = 0.0f;
    float fraction = 0.0f;
    int divisor = 1;
    int sign = 1;
    
    // Handle negative sign
    if (*str == '-') {
        sign = -1;
        str++;
    }
    
    // Parse integer part
    while (*str >= '0' && *str <= '9') {
        result = result * 10.0f + (*str - '0');
        str++;
    }
    
    // Parse decimal part
    if (*str == '.') {
        str++;
        while (*str >= '0' && *str <= '9') {
            fraction = fraction * 10.0f + (*str - '0');
            divisor *= 10;
            str++;
        }
    }
    
    return sign * (result + fraction / divisor);
}

static void simple_copy_string(char *dest, const char *src, uint32_t max_len) {
    uint32_t i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

int physics_config_serialize(const physics_config_t *config, char *buffer, uint32_t buffer_size) {
    if (!config || !buffer || buffer_size < 512) {
        return 0;
    }
    
    // Simple binary serialization - just copy the struct
    // Add a magic header for validation
    uint32_t magic = 0x50485953; // "PHYS"
    uint32_t version = 1;
    uint32_t offset = 0;
    
    // Check if we have enough space
    if (buffer_size < sizeof(magic) + sizeof(version) + sizeof(physics_config_t)) {
        return 0;
    }
    
    // Write magic number
    *((uint32_t*)(buffer + offset)) = magic;
    offset += sizeof(uint32_t);
    
    // Write version
    *((uint32_t*)(buffer + offset)) = version;
    offset += sizeof(uint32_t);
    
    // Write configuration data
    *((physics_config_t*)(buffer + offset)) = *config;
    offset += sizeof(physics_config_t);
    
    return offset;
}

int physics_config_deserialize(physics_config_t *config, const char *buffer) {
    if (!config || !buffer) {
        return 0;
    }
    
    uint32_t offset = 0;
    
    // Read and validate magic number
    uint32_t magic = *((uint32_t*)(buffer + offset));
    offset += sizeof(uint32_t);
    
    if (magic != 0x50485953) { // "PHYS"
        return 0;
    }
    
    // Read version
    uint32_t version = *((uint32_t*)(buffer + offset));
    offset += sizeof(uint32_t);
    
    if (version != 1) {
        return 0;
    }
    
    // Read configuration data
    *config = *((physics_config_t*)(buffer + offset));
    
    // Validate the deserialized configuration
    return physics_config_validate(config);
}

// Particle Initialization Patterns Implementation

// Simple random number generator for particle initialization
static uint32_t rng_seed = 12345;

static uint32_t simple_rand(void) {
    rng_seed = rng_seed * 1103515245 + 12345;
    return rng_seed;
}

static float simple_rand_float(void) {
    return (float)(simple_rand() % 10000) / 10000.0f;
}

static float simple_rand_range(float min, float max) {
    return min + simple_rand_float() * (max - min);
}

// Mathematical constants and helper functions
#define PI 3.14159265359f
#define TWO_PI (2.0f * PI)

static float simple_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    
    // Newton's method for square root
    float guess = x / 2.0f;
    for (int i = 0; i < 10; i++) {
        guess = (guess + x / guess) / 2.0f;
    }
    return guess;
}

static float simple_sin(float x) {
    // Simple sine approximation using Taylor series
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    
    float x2 = x * x;
    return x * (1.0f - x2/6.0f + x2*x2/120.0f - x2*x2*x2/5040.0f);
}

static float simple_cos(float x) {
    // Simple cosine approximation
    return simple_sin(x + PI/2.0f);
}

int initialize_random_particles(physics_simulation_t *sim, uint32_t count) {
    if (!sim || !sim->particles || count == 0) {
        return 0;
    }
    
    // Limit count to available capacity
    if (count > sim->particles->capacity) {
        count = sim->particles->capacity;
    }
    
    // Clear existing particles
    sim->particles->count = 0;
    
    // Generate random particles within world bounds
    float margin = 50.0f; // Keep particles away from edges
    float min_x = margin;
    float max_x = sim->config.world_width - margin;
    float min_y = margin;
    float max_y = sim->config.world_height - margin;
    
    for (uint32_t i = 0; i < count; i++) {
        // Random position
        float x = simple_rand_range(min_x, max_x);
        float y = simple_rand_range(min_y, max_y);
        
        // Random mass (1.0 to 10.0)
        float mass = simple_rand_range(1.0f, 10.0f);
        
        // Radius based on mass
        float radius = 2.0f + mass * 0.5f;
        
        // Add particle to system
        uint32_t particle_idx = particle_add(sim->particles, x, y, mass, radius);
        if (particle_idx < sim->particles->capacity) {
            particle_t *p = &sim->particles->particles[particle_idx];
            
            // Random initial velocity (small values)
            p->vx = simple_rand_range(-20.0f, 20.0f);
            p->vy = simple_rand_range(-20.0f, 20.0f);
            
            // Set color based on mass
            particle_set_color_by_mass(p);
        }
    }
    
    return 1;
}

int initialize_orbital_system(physics_simulation_t *sim, uint32_t planet_count) {
    if (!sim || !sim->particles || planet_count == 0) {
        return 0;
    }
    
    // Limit planet count
    if (planet_count > sim->particles->capacity - 1) {
        planet_count = sim->particles->capacity - 1;
    }
    
    // Clear existing particles
    sim->particles->count = 0;
    
    // Create central star at center of world
    float center_x = sim->config.world_width / 2.0f;
    float center_y = sim->config.world_height / 2.0f;
    float star_mass = 100.0f; // Heavy central body
    float star_radius = 15.0f;
    
    uint32_t star_idx = particle_add(sim->particles, center_x, center_y, star_mass, star_radius);
    if (star_idx < sim->particles->capacity) {
        particle_t *star = &sim->particles->particles[star_idx];
        star->vx = 0.0f;
        star->vy = 0.0f;
        star->color = 0xFFFFFF00; // Yellow star
    }
    
    // Create orbiting planets
    float min_orbit_radius = 80.0f;
    float max_orbit_radius = simple_sqrt(sim->config.world_width * sim->config.world_width + 
                                        sim->config.world_height * sim->config.world_height) / 3.0f;
    
    for (uint32_t i = 0; i < planet_count; i++) {
        // Calculate orbital radius (evenly spaced)
        float orbit_radius = min_orbit_radius + 
                           (max_orbit_radius - min_orbit_radius) * (float)i / (float)(planet_count - 1);
        
        // Random angle for planet position
        float angle = simple_rand_range(0.0f, TWO_PI);
        
        // Calculate planet position
        float planet_x = center_x + orbit_radius * simple_cos(angle);
        float planet_y = center_y + orbit_radius * simple_sin(angle);
        
        // Planet mass (smaller than star)
        float planet_mass = simple_rand_range(1.0f, 5.0f);
        float planet_radius = 3.0f + planet_mass * 0.5f;
        
        // Add planet
        uint32_t planet_idx = particle_add(sim->particles, planet_x, planet_y, planet_mass, planet_radius);
        if (planet_idx < sim->particles->capacity) {
            particle_t *planet = &sim->particles->particles[planet_idx];
            
            // Calculate orbital velocity for stable orbit
            // v = sqrt(G * M / r) where G is gravity constant, M is central mass, r is radius
            float orbital_speed = simple_sqrt(sim->config.gravity_constant * star_mass / orbit_radius);
            
            // Set velocity perpendicular to radius vector
            planet->vx = -orbital_speed * simple_sin(angle);
            planet->vy = orbital_speed * simple_cos(angle);
            
            // Set color based on distance (closer = redder, farther = bluer)
            float color_factor = (orbit_radius - min_orbit_radius) / (max_orbit_radius - min_orbit_radius);
            uint8_t red = (uint8_t)(255 * (1.0f - color_factor));
            uint8_t blue = (uint8_t)(255 * color_factor);
            planet->color = 0xFF000000 | (red << 16) | (128 << 8) | blue;
        }
    }
    
    return 1;
}

int initialize_collision_demo(physics_simulation_t *sim, uint32_t count) {
    if (!sim || !sim->particles || count == 0) {
        return 0;
    }
    
    // Limit count to available capacity
    if (count > sim->particles->capacity) {
        count = sim->particles->capacity;
    }
    
    // Clear existing particles
    sim->particles->count = 0;
    
    // Create collision demonstration with two groups of particles
    uint32_t group1_count = count / 2;
    uint32_t group2_count = count - group1_count;
    
    float center_x = sim->config.world_width / 2.0f;
    float center_y = sim->config.world_height / 2.0f;
    float separation = 200.0f;
    
    // Group 1: Left side, moving right
    for (uint32_t i = 0; i < group1_count; i++) {
        float x = center_x - separation + simple_rand_range(-50.0f, 50.0f);
        float y = center_y + simple_rand_range(-100.0f, 100.0f);
        float mass = simple_rand_range(2.0f, 6.0f);
        float radius = 3.0f + mass * 0.3f;
        
        uint32_t particle_idx = particle_add(sim->particles, x, y, mass, radius);
        if (particle_idx < sim->particles->capacity) {
            particle_t *p = &sim->particles->particles[particle_idx];
            p->vx = simple_rand_range(30.0f, 60.0f); // Moving right
            p->vy = simple_rand_range(-10.0f, 10.0f);
            p->color = 0xFF0000FF; // Blue group
        }
    }
    
    // Group 2: Right side, moving left
    for (uint32_t i = 0; i < group2_count; i++) {
        float x = center_x + separation + simple_rand_range(-50.0f, 50.0f);
        float y = center_y + simple_rand_range(-100.0f, 100.0f);
        float mass = simple_rand_range(2.0f, 6.0f);
        float radius = 3.0f + mass * 0.3f;
        
        uint32_t particle_idx = particle_add(sim->particles, x, y, mass, radius);
        if (particle_idx < sim->particles->capacity) {
            particle_t *p = &sim->particles->particles[particle_idx];
            p->vx = simple_rand_range(-60.0f, -30.0f); // Moving left
            p->vy = simple_rand_range(-10.0f, 10.0f);
            p->color = 0xFFFF0000; // Red group
        }
    }
    
    return 1;
}

int validate_particle_pattern(const physics_simulation_t *sim) {
    if (!sim || !sim->particles) {
        return 0;
    }
    
    // Check that particles are within world bounds
    for (uint32_t i = 0; i < sim->particles->count; i++) {
        const particle_t *p = &sim->particles->particles[i];
        if (!p->active) continue;
        
        if (p->x < 0.0f || p->x > sim->config.world_width ||
            p->y < 0.0f || p->y > sim->config.world_height) {
            return 0; // Particle out of bounds
        }
        
        // Check for reasonable mass and radius
        if (p->mass <= 0.0f || p->mass > 1000.0f ||
            p->radius <= 0.0f || p->radius > 100.0f) {
            return 0; // Invalid particle properties
        }
    }
    
    // Check for overlapping particles (basic validation)
    for (uint32_t i = 0; i < sim->particles->count; i++) {
        const particle_t *p1 = &sim->particles->particles[i];
        if (!p1->active) continue;
        
        for (uint32_t j = i + 1; j < sim->particles->count; j++) {
            const particle_t *p2 = &sim->particles->particles[j];
            if (!p2->active) continue;
            
            float dx = p1->x - p2->x;
            float dy = p1->y - p2->y;
            float distance = simple_sqrt(dx * dx + dy * dy);
            float min_distance = p1->radius + p2->radius;
            
            // Allow some small overlap for initialization
            if (distance < min_distance * 0.5f) {
                return 0; // Particles too close
            }
        }
    }
    
    return 1;
}

int check_orbital_stability(const physics_simulation_t *sim) {
    if (!sim || !sim->particles || sim->particles->count < 2) {
        return 0;
    }
    
    // Find the central body (heaviest particle)
    uint32_t central_idx = 0;
    float max_mass = 0.0f;
    
    for (uint32_t i = 0; i < sim->particles->count; i++) {
        const particle_t *p = &sim->particles->particles[i];
        if (p->active && p->mass > max_mass) {
            max_mass = p->mass;
            central_idx = i;
        }
    }
    
    const particle_t *central = &sim->particles->particles[central_idx];
    
    // Check orbital parameters for other particles
    for (uint32_t i = 0; i < sim->particles->count; i++) {
        if (i == central_idx) continue;
        
        const particle_t *planet = &sim->particles->particles[i];
        if (!planet->active) continue;
        
        // Calculate distance to central body
        float dx = planet->x - central->x;
        float dy = planet->y - central->y;
        float distance = simple_sqrt(dx * dx + dy * dy);
        
        if (distance < central->radius + planet->radius) {
            return 0; // Collision with central body
        }
        
        // Calculate orbital velocity
        float vx_rel = planet->vx - central->vx;
        float vy_rel = planet->vy - central->vy;
        float speed = simple_sqrt(vx_rel * vx_rel + vy_rel * vy_rel);
        
        // Calculate expected orbital speed for circular orbit
        float expected_speed = simple_sqrt(sim->config.gravity_constant * central->mass / distance);
        
        // Check if speed is reasonable for orbital motion (within 50% of expected)
        if (speed < expected_speed * 0.5f || speed > expected_speed * 1.5f) {
            return 0; // Unstable orbit
        }
        
        // Check if velocity is roughly perpendicular to radius vector
        float radial_dot = (dx * vx_rel + dy * vy_rel) / distance;
        float tangential_speed = simple_sqrt(speed * speed - radial_dot * radial_dot);
        
        if (tangential_speed < speed * 0.7f) {
            return 0; // Too much radial velocity component
        }
    }
    
    return 1;
}