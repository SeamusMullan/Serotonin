#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdint.h>
#include "particle.h"
#include "quadtree.h"
#include "memory.h"
#include "collision.h"
#include "renderer.h"
#include "performance.h"

// Forward declarations for remaining types
typedef struct physics_simulation physics_simulation_t;
typedef struct physics_config physics_config_t;

// Configuration structure for simulation parameters
struct physics_config {
    uint32_t max_particles;       // Maximum particle count
    float world_width;            // Simulation bounds
    float world_height;
    float gravity_constant;       // Gravitational constant
    float theta;                  // Barnes-Hut theta parameter (0.5-2.0)
    float restitution;            // Collision restitution coefficient
    float softening;              // Force softening parameter
    float time_step;              // Integration step size
    uint32_t target_fps;          // Target frame rate
    uint8_t enable_collisions;    // Enable collision detection
    uint8_t debug_mode;           // Debug visualization mode
};

// Memory management structure for efficient allocation
// struct physics_memory {
//     memory_pool_t particle_pool;    // Pool for particles
//     memory_pool_t quadtree_pool;    // Pool for quadtree nodes
//     memory_pool_t collision_pool;   // Pool for collision data
//     memory_stats_t stats;           // Memory usage statistics
//     uint8_t defrag_enabled;         // Enable defragmentation
//     uint32_t defrag_threshold;      // Fragmentation threshold for defrag
//     uint8_t monitoring_enabled;     // Enable memory monitoring
// };

// Main physics simulation state
struct physics_simulation {
    particle_system_t *particles;
    quadtree_t *quadtree;
    collision_system_t *collisions;
    physics_renderer_t *renderer;
    physics_memory_t *memory;
    performance_profiler_t *profiler;
    adaptive_quality_t *quality;
    physics_config_t config;
    float gravitational_constant;
    float theta;                  // Barnes-Hut approximation parameter
    float restitution;            // Collision restitution coefficient
    float softening;              // Force softening parameter
    float time_step;              // Integration time step
    uint8_t collision_enabled;
    uint8_t debug_render;
    uint8_t initialized;
};

// Configuration system functions
physics_config_t* physics_config_create(void);
void physics_config_destroy(physics_config_t *config);
int physics_config_validate(const physics_config_t *config);
physics_config_t physics_config_preset_default(void);
physics_config_t physics_config_preset_high_performance(void);
physics_config_t physics_config_preset_orbital_system(void);
physics_config_t physics_config_preset_collision_demo(void);
physics_config_t physics_config_preset_stress_test(void);
int physics_config_serialize(const physics_config_t *config, char *buffer, uint32_t buffer_size);
int physics_config_deserialize(physics_config_t *config, const char *buffer);

// Function declarations
physics_simulation_t* physics_simulation_create(physics_config_t *config);
void physics_simulation_destroy(physics_simulation_t *sim);
void physics_simulation_step(physics_simulation_t *sim);
void physics_simulation_render(physics_simulation_t *sim);
physics_config_t physics_config_default(void);

// Advanced simulation control functions
void physics_simulation_step_complete(physics_simulation_t *sim);
int physics_simulation_step_with_timing(physics_simulation_t *sim, float *step_time);
void physics_simulation_set_time_step(physics_simulation_t *sim, float time_step);
void physics_simulation_set_frame_rate(physics_simulation_t *sim, uint32_t target_fps);
int physics_simulation_validate_parameters(physics_simulation_t *sim);
void physics_simulation_update_config(physics_simulation_t *sim, const physics_config_t *new_config);

// Simulation state management
void physics_simulation_pause(physics_simulation_t *sim);
void physics_simulation_resume(physics_simulation_t *sim);
void physics_simulation_reset(physics_simulation_t *sim);
int physics_simulation_is_ready(const physics_simulation_t *sim);

// Memory management functions (implemented in memory.c)
physics_memory_t* physics_memory_create(uint32_t max_particles, uint32_t max_quadtree_nodes, uint32_t max_collisions);
void physics_memory_destroy(physics_memory_t *memory);
void* physics_memory_alloc_particle(physics_memory_t *memory);
void* physics_memory_alloc_quadtree_node(physics_memory_t *memory);
void* physics_memory_alloc_collision(physics_memory_t *memory);
void physics_memory_free_particle(physics_memory_t *memory, void *ptr);
void physics_memory_free_quadtree_node(physics_memory_t *memory, void *ptr);
void physics_memory_free_collision(physics_memory_t *memory, void *ptr);

// Memory monitoring and reporting functions
void physics_memory_update_stats(physics_memory_t *memory);
void physics_memory_get_stats(const physics_memory_t *memory, memory_stats_t *stats);
uint32_t physics_memory_get_total_usage(const physics_memory_t *memory);
float physics_memory_get_utilization(const physics_memory_t *memory);

// Memory optimization functions
void physics_memory_defragment_all(physics_memory_t *memory);
void physics_memory_set_defrag_threshold(physics_memory_t *memory, uint32_t threshold);
void physics_memory_enable_monitoring(physics_memory_t *memory, uint8_t enable);
int physics_memory_check_integrity(const physics_memory_t *memory);

// Performance monitoring functions (implemented in performance.c)
performance_profiler_t* physics_performance_create_profiler(float target_fps);
void physics_performance_destroy_profiler(performance_profiler_t *profiler);
adaptive_quality_t* physics_performance_create_quality(float target_fps);
void physics_performance_destroy_quality(adaptive_quality_t *quality);
void physics_performance_begin_frame(physics_simulation_t *sim);
void physics_performance_end_frame(physics_simulation_t *sim);
void physics_performance_get_metrics(const physics_simulation_t *sim, performance_metrics_t *metrics);
void physics_performance_optimize_simulation(physics_simulation_t *sim);
int physics_performance_run_benchmarks(void);

// Integration testing
int physics_run_integration_tests(void);

// Particle initialization patterns
int initialize_random_particles(physics_simulation_t *sim, uint32_t count);
int initialize_orbital_system(physics_simulation_t *sim, uint32_t planet_count);
int initialize_collision_demo(physics_simulation_t *sim, uint32_t count);
int validate_particle_pattern(const physics_simulation_t *sim);
int check_orbital_stability(const physics_simulation_t *sim);

// Configuration system testing
int test_physics_config_system(void);
int test_physics_config_presets(void);
int run_physics_config_tests(void);

// Particle initialization testing
int test_particle_initialization_patterns(void);
int test_particle_validation_functions(void);
int test_preset_initialization_combinations(void);
int run_physics_initialization_tests(void);

#endif // PHYSICS_H