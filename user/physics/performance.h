#ifndef PHYSICS_PERFORMANCE_H
#define PHYSICS_PERFORMANCE_H

#include <stdint.h>

// Forward declarations
typedef struct physics_simulation physics_simulation_t;
typedef struct particle_system particle_system_t;
typedef struct quadtree quadtree_t;
typedef struct performance_profiler performance_profiler_t;
typedef struct performance_metrics performance_metrics_t;
typedef struct adaptive_quality adaptive_quality_t;

// Performance metrics structure
struct performance_metrics {
    uint32_t frame_count;           // Total frames processed
    float total_frame_time;         // Total time spent on frames
    float average_frame_time;       // Average frame time
    float min_frame_time;           // Minimum frame time
    float max_frame_time;           // Maximum frame time
    float current_fps;              // Current frames per second
    float target_fps;               // Target frames per second
    
    // Component timing
    float force_calculation_time;   // Time spent calculating forces
    float collision_detection_time; // Time spent on collision detection
    float integration_time;         // Time spent on physics integration
    float quadtree_rebuild_time;    // Time spent rebuilding quadtree
    float rendering_time;           // Time spent on rendering
    
    // Memory metrics
    uint32_t memory_usage;          // Current memory usage
    uint32_t peak_memory_usage;     // Peak memory usage
    float memory_fragmentation;     // Memory fragmentation percentage
    
    // Quality metrics
    uint32_t active_particles;      // Number of active particles
    uint32_t force_calculations;    // Number of force calculations per frame
    uint32_t collision_checks;      // Number of collision checks per frame
    float simulation_accuracy;      // Simulation accuracy metric
};

// Performance profiler for timing measurements
struct performance_profiler {
    uint32_t start_time;            // Frame start time
    uint32_t last_checkpoint;       // Last timing checkpoint
    performance_metrics_t metrics;  // Current metrics
    performance_metrics_t history[60]; // Last 60 frames of metrics
    uint32_t history_index;         // Current history index
    uint8_t profiling_enabled;      // Enable/disable profiling
    uint8_t detailed_profiling;     // Enable detailed component profiling
};

// Adaptive quality settings
struct adaptive_quality {
    uint8_t enabled;                // Enable adaptive quality
    float target_fps;               // Target FPS to maintain
    float fps_tolerance;            // FPS tolerance before adjustment
    
    // Quality levels (0-100)
    uint32_t particle_quality;      // Particle rendering quality
    uint32_t force_quality;         // Force calculation quality
    uint32_t collision_quality;     // Collision detection quality
    float theta_quality;            // Barnes-Hut theta parameter
    
    // Adjustment parameters
    uint32_t adjustment_frames;     // Frames to wait before adjustment
    uint32_t frames_since_adjust;   // Frames since last adjustment
    float quality_step;             // Quality adjustment step size
    
    // Performance thresholds
    float performance_low_threshold;  // Threshold for reducing quality
    float performance_high_threshold; // Threshold for increasing quality
};

// Performance profiler functions
performance_profiler_t* performance_profiler_create(float target_fps);
void performance_profiler_destroy(performance_profiler_t *profiler);
void performance_profiler_begin_frame(performance_profiler_t *profiler);
void performance_profiler_end_frame(performance_profiler_t *profiler);
void performance_profiler_checkpoint(performance_profiler_t *profiler, const char *component);
void performance_profiler_enable(performance_profiler_t *profiler, uint8_t enable);
void performance_profiler_set_detailed(performance_profiler_t *profiler, uint8_t detailed);

// Performance metrics functions
void performance_get_metrics(const performance_profiler_t *profiler, performance_metrics_t *metrics);
float performance_get_average_fps(const performance_profiler_t *profiler);
float performance_get_frame_time_variance(const performance_profiler_t *profiler);
void performance_reset_metrics(performance_profiler_t *profiler);
void performance_print_report(const performance_profiler_t *profiler);

// Adaptive quality functions
adaptive_quality_t* adaptive_quality_create(float target_fps);
void adaptive_quality_destroy(adaptive_quality_t *quality);
void adaptive_quality_update(adaptive_quality_t *quality, const performance_metrics_t *metrics);
void adaptive_quality_apply(adaptive_quality_t *quality, physics_simulation_t *sim);
void adaptive_quality_set_target_fps(adaptive_quality_t *quality, float target_fps);
void adaptive_quality_enable(adaptive_quality_t *quality, uint8_t enable);

// SIMD optimization functions (when available)
#ifdef __SSE2__
void simd_calculate_forces_sse2(particle_system_t *particles, float *forces_x, float *forces_y, 
                                uint32_t count, float gravity, float softening);
void simd_integrate_particles_sse2(particle_system_t *particles, float dt, uint32_t count);
void simd_vector_operations_sse2(float *a, float *b, float *result, uint32_t count);
#endif

// Cache-friendly data layout optimizations
void optimize_particle_layout(particle_system_t *particles);
void optimize_quadtree_layout(quadtree_t *quadtree);
void prefetch_memory_regions(void *ptr, uint32_t size);

// Performance benchmarking functions
int benchmark_force_calculations(uint32_t particle_count, uint32_t iterations);
int benchmark_collision_detection(uint32_t particle_count, uint32_t iterations);
int benchmark_memory_operations(uint32_t allocation_count, uint32_t iterations);
float benchmark_simulation_step(physics_simulation_t *sim, uint32_t iterations);

// Performance optimization utilities
void optimize_simulation_parameters(physics_simulation_t *sim, const performance_metrics_t *metrics);
void balance_quality_vs_performance(physics_simulation_t *sim, float target_fps);
uint32_t estimate_optimal_particle_count(float target_fps, float available_memory);
float calculate_performance_score(const performance_metrics_t *metrics);

// Testing functions
int physics_performance_run_tests(void);

#endif // PHYSICS_PERFORMANCE_H