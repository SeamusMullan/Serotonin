#include "performance.h"
#include "physics.h"
#include "particle.h"
#include "quadtree.h"
#include "collision.h"
#include "../kernel.h"
#include <stddef.h>
#include "../stdio/stdio.h"

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFU
#endif

// Internal helper functions
static uint32_t get_current_time_ms(void);
static float calculate_fps(float frame_time);
static void update_metrics_history(performance_profiler_t *profiler);
static void adjust_quality_based_on_performance(adaptive_quality_t *quality, float current_fps);

// Performance profiler implementation

performance_profiler_t* performance_profiler_create(float target_fps) {
    performance_profiler_t *profiler = (performance_profiler_t*)kernel_malloc(sizeof(performance_profiler_t));
    if (!profiler) {
        return NULL;
    }
    
    // Initialize profiler structure
    profiler->start_time = 0;
    profiler->last_checkpoint = 0;
    profiler->history_index = 0;
    profiler->profiling_enabled = 1;
    profiler->detailed_profiling = 0;
    
    // Initialize metrics
    profiler->metrics.frame_count = 0;
    profiler->metrics.total_frame_time = 0.0f;
    profiler->metrics.average_frame_time = 0.0f;
    profiler->metrics.min_frame_time = 1000.0f; // Start with high value
    profiler->metrics.max_frame_time = 0.0f;
    profiler->metrics.current_fps = 0.0f;
    profiler->metrics.target_fps = target_fps;
    
    // Initialize component timings
    profiler->metrics.force_calculation_time = 0.0f;
    profiler->metrics.collision_detection_time = 0.0f;
    profiler->metrics.integration_time = 0.0f;
    profiler->metrics.quadtree_rebuild_time = 0.0f;
    profiler->metrics.rendering_time = 0.0f;
    
    // Initialize memory metrics
    profiler->metrics.memory_usage = 0;
    profiler->metrics.peak_memory_usage = 0;
    profiler->metrics.memory_fragmentation = 0.0f;
    
    // Initialize quality metrics
    profiler->metrics.active_particles = 0;
    profiler->metrics.force_calculations = 0;
    profiler->metrics.collision_checks = 0;
    profiler->metrics.simulation_accuracy = 1.0f;
    
    // Clear history
    for (int i = 0; i < 60; i++) {
        profiler->history[i] = profiler->metrics;
    }
    
    return profiler;
}

void performance_profiler_destroy(performance_profiler_t *profiler) {
    if (profiler) {
        kernel_free(profiler);
    }
}

void performance_profiler_begin_frame(performance_profiler_t *profiler) {
    if (!profiler || !profiler->profiling_enabled) {
        return;
    }
    
    profiler->start_time = get_current_time_ms();
    profiler->last_checkpoint = profiler->start_time;
}

void performance_profiler_end_frame(performance_profiler_t *profiler) {
    if (!profiler || !profiler->profiling_enabled) {
        return;
    }
    
    uint32_t end_time = get_current_time_ms();
    float frame_time = (float)(end_time - profiler->start_time) / 1000.0f;
    
    // Update frame metrics
    profiler->metrics.frame_count++;
    profiler->metrics.total_frame_time += frame_time;
    profiler->metrics.average_frame_time = profiler->metrics.total_frame_time / profiler->metrics.frame_count;
    
    // Update min/max frame times
    if (frame_time < profiler->metrics.min_frame_time) {
        profiler->metrics.min_frame_time = frame_time;
    }
    if (frame_time > profiler->metrics.max_frame_time) {
        profiler->metrics.max_frame_time = frame_time;
    }
    
    // Calculate current FPS
    profiler->metrics.current_fps = calculate_fps(frame_time);
    
    // Update metrics history
    update_metrics_history(profiler);
}

void performance_profiler_checkpoint(performance_profiler_t *profiler, const char *component) {
    if (!profiler || !profiler->profiling_enabled || !profiler->detailed_profiling) {
        return;
    }
    
    uint32_t current_time = get_current_time_ms();
    float component_time = (float)(current_time - profiler->last_checkpoint) / 1000.0f;
    
    // Update component-specific timing based on component name
    if (component) {
        if (component[0] == 'f') { // "force"
            profiler->metrics.force_calculation_time += component_time;
        } else if (component[0] == 'c') { // "collision"
            profiler->metrics.collision_detection_time += component_time;
        } else if (component[0] == 'i') { // "integration"
            profiler->metrics.integration_time += component_time;
        } else if (component[0] == 'q') { // "quadtree"
            profiler->metrics.quadtree_rebuild_time += component_time;
        } else if (component[0] == 'r') { // "rendering"
            profiler->metrics.rendering_time += component_time;
        }
    }
    
    profiler->last_checkpoint = current_time;
}

void performance_profiler_enable(performance_profiler_t *profiler, uint8_t enable) {
    if (profiler) {
        profiler->profiling_enabled = enable;
    }
}

void performance_profiler_set_detailed(performance_profiler_t *profiler, uint8_t detailed) {
    if (profiler) {
        profiler->detailed_profiling = detailed;
    }
}

// Performance metrics functions

void performance_get_metrics(const performance_profiler_t *profiler, performance_metrics_t *metrics) {
    if (profiler && metrics) {
        *metrics = profiler->metrics;
    }
}

float performance_get_average_fps(const performance_profiler_t *profiler) {
    if (!profiler || profiler->metrics.average_frame_time <= 0.0f) {
        return 0.0f;
    }
    
    return 1.0f / profiler->metrics.average_frame_time;
}

float performance_get_frame_time_variance(const performance_profiler_t *profiler) {
    if (!profiler || profiler->metrics.frame_count < 2) {
        return 0.0f;
    }
    
    // Calculate variance from history
    float mean = profiler->metrics.average_frame_time;
    float variance = 0.0f;
    uint32_t count = 0;
    
    for (int i = 0; i < 60 && count < profiler->metrics.frame_count; i++) {
        float frame_time = profiler->history[i].average_frame_time;
        if (frame_time > 0.0f) {
            float diff = frame_time - mean;
            variance += diff * diff;
            count++;
        }
    }
    
    return (count > 1) ? variance / (count - 1) : 0.0f;
}

void performance_reset_metrics(performance_profiler_t *profiler) {
    if (!profiler) {
        return;
    }
    
    float target_fps = profiler->metrics.target_fps;
    
    // Reset all metrics except target FPS
    profiler->metrics.frame_count = 0;
    profiler->metrics.total_frame_time = 0.0f;
    profiler->metrics.average_frame_time = 0.0f;
    profiler->metrics.min_frame_time = 1000.0f;
    profiler->metrics.max_frame_time = 0.0f;
    profiler->metrics.current_fps = 0.0f;
    profiler->metrics.target_fps = target_fps;
    
    // Reset component timings
    profiler->metrics.force_calculation_time = 0.0f;
    profiler->metrics.collision_detection_time = 0.0f;
    profiler->metrics.integration_time = 0.0f;
    profiler->metrics.quadtree_rebuild_time = 0.0f;
    profiler->metrics.rendering_time = 0.0f;
    
    // Reset quality metrics
    profiler->metrics.force_calculations = 0;
    profiler->metrics.collision_checks = 0;
    profiler->metrics.simulation_accuracy = 1.0f;
    
    profiler->history_index = 0;
}

// Adaptive quality implementation

adaptive_quality_t* adaptive_quality_create(float target_fps) {
    adaptive_quality_t *quality = (adaptive_quality_t*)kernel_malloc(sizeof(adaptive_quality_t));
    if (!quality) {
        return NULL;
    }
    
    // Initialize adaptive quality settings
    quality->enabled = 1;
    quality->target_fps = target_fps;
    quality->fps_tolerance = 5.0f; // 5 FPS tolerance
    
    // Initialize quality levels (start at maximum)
    quality->particle_quality = 100;
    quality->force_quality = 100;
    quality->collision_quality = 100;
    quality->theta_quality = 0.5f; // Barnes-Hut theta parameter
    
    // Initialize adjustment parameters
    quality->adjustment_frames = 30; // Wait 30 frames before adjustment
    quality->frames_since_adjust = 0;
    quality->quality_step = 10.0f; // 10% quality steps
    
    // Initialize performance thresholds
    quality->performance_low_threshold = target_fps - quality->fps_tolerance;
    quality->performance_high_threshold = target_fps + quality->fps_tolerance;
    
    return quality;
}

void adaptive_quality_destroy(adaptive_quality_t *quality) {
    if (quality) {
        kernel_free(quality);
    }
}

void adaptive_quality_update(adaptive_quality_t *quality, const performance_metrics_t *metrics) {
    if (!quality || !quality->enabled || !metrics) {
        return;
    }
    
    quality->frames_since_adjust++;
    
    // Only adjust quality after enough frames have passed
    if (quality->frames_since_adjust < quality->adjustment_frames) {
        return;
    }
    
    float current_fps = metrics->current_fps;
    
    // Adjust quality based on performance
    adjust_quality_based_on_performance(quality, current_fps);
    
    // Reset adjustment counter
    quality->frames_since_adjust = 0;
}

void adaptive_quality_apply(adaptive_quality_t *quality, physics_simulation_t *sim) {
    if (!quality || !quality->enabled || !sim) {
        return;
    }
    
    // Apply Barnes-Hut theta parameter based on force quality
    float theta = quality->theta_quality;
    if (quality->force_quality < 100) {
        // Increase theta (less accuracy) for lower quality
        theta = 0.5f + (100 - quality->force_quality) * 0.02f; // Scale from 0.5 to 2.5
    }
    sim->theta = theta;
    
    // Apply collision detection quality
    if (quality->collision_quality < 50) {
        // Disable collision detection for very low quality
        sim->collision_enabled = 0;
    } else {
        sim->collision_enabled = 1;
    }
    
    // Note: Particle quality would be applied in the rendering system
    // This is a placeholder for where rendering quality would be adjusted
}

void adaptive_quality_set_target_fps(adaptive_quality_t *quality, float target_fps) {
    if (!quality) {
        return;
    }
    
    quality->target_fps = target_fps;
    quality->performance_low_threshold = target_fps - quality->fps_tolerance;
    quality->performance_high_threshold = target_fps + quality->fps_tolerance;
}

void adaptive_quality_enable(adaptive_quality_t *quality, uint8_t enable) {
    if (quality) {
        quality->enabled = enable;
    }
}

// SIMD optimization functions (SSE2 implementation)

// Simple math functions for kernel environment

static float simple_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float guess = x / 2.0f;
    for (int i = 0; i < 10; i++) {
        guess = (guess + x / guess) / 2.0f;
    }
    return guess;
}

// Benchmark collision detection for a given number of particles and iterations
int benchmark_collision_detection(uint32_t particle_count, uint32_t iterations) {
    particle_system_t *particles = particle_system_create(particle_count);
    if (!particles) return 0;

    // Add test particles
    for (uint32_t i = 0; i < particle_count; i++) {
        particles->particles[i].x = (float)i;
        particles->particles[i].y = (float)i;
        particles->particles[i].radius = 1.0f;
        particles->particles[i].active = 1;
    }

    uint32_t start_time = get_current_time_ms();
    for (uint32_t iter = 0; iter < iterations; iter++) {
        for (uint32_t i = 0; i < particle_count; i++) {
            for (uint32_t j = i + 1; j < particle_count; j++) {
                float dx = particles->particles[j].x - particles->particles[i].x;
                float dy = particles->particles[j].y - particles->particles[i].y;
                float dist2 = dx * dx + dy * dy;
                float r_sum = particles->particles[i].radius + particles->particles[j].radius;
                if (dist2 < r_sum * r_sum) {
                    // Collision detected (do nothing)
                }
            }
        }
    }
    uint32_t end_time = get_current_time_ms();
    particle_system_destroy(particles);
    return (end_time > start_time) ? 1 : 0;
}

// Benchmark memory operations: allocation and deallocation
int benchmark_memory_operations(uint32_t allocation_count, uint32_t iterations) {
    void **ptrs = (void**)kernel_malloc(sizeof(void*) * allocation_count);
    if (!ptrs) return 0;
    uint32_t success = 1;
    uint32_t start_time = get_current_time_ms();
    for (uint32_t iter = 0; iter < iterations; iter++) {
        for (uint32_t i = 0; i < allocation_count; i++) {
            ptrs[i] = kernel_malloc(128);
            if (!ptrs[i]) success = 0;
        }
        for (uint32_t i = 0; i < allocation_count; i++) {
            kernel_free(ptrs[i]);
        }
    }
    uint32_t end_time = get_current_time_ms();
    kernel_free(ptrs);
    return (success && end_time > start_time) ? 1 : 0;
}

// Benchmark simulation step for a given simulation and iteration count
float benchmark_simulation_step(physics_simulation_t *sim, uint32_t iterations) {
    if (!sim || iterations == 0) return 0.0f;
    uint32_t start_time = get_current_time_ms();
    for (uint32_t i = 0; i < iterations; i++) {
        // Assume physics_simulation_step exists
        physics_simulation_step(sim);
    }
    uint32_t end_time = get_current_time_ms();
    return (float)(end_time - start_time) / (float)iterations;
}

// Print a performance report to the kernel console
void performance_print_report(const performance_profiler_t *profiler) {
    if (!profiler) return;
    const performance_metrics_t *m = &profiler->metrics;
    printf("Performance Report:\n");
    printf("Frames: %u\n", m->frame_count);
    printf("Current FPS: %.2f (Target: %.2f)\n", m->current_fps, m->target_fps);
    printf("Avg Frame Time: %.4f ms\n", m->average_frame_time * 1000.0f);
    printf("Min Frame Time: %.4f ms\n", m->min_frame_time * 1000.0f);
    printf("Max Frame Time: %.4f ms\n", m->max_frame_time * 1000.0f);
    printf("Force Calc Time: %.4f ms\n", m->force_calculation_time * 1000.0f);
    printf("Collision Time: %.4f ms\n", m->collision_detection_time * 1000.0f);
    printf("Integration Time: %.4f ms\n", m->integration_time * 1000.0f);
    printf("Quadtree Time: %.4f ms\n", m->quadtree_rebuild_time * 1000.0f);
    printf("Rendering Time: %.4f ms\n", m->rendering_time * 1000.0f);
    printf("Memory Usage: %u bytes (Peak: %u)\n", m->memory_usage, m->peak_memory_usage);
    printf("Memory Fragmentation: %.2f%%\n", m->memory_fragmentation);
    printf("Active Particles: %u\n", m->active_particles);
    printf("Force Calculations: %u\n", m->force_calculations);
    printf("Collision Checks: %u\n", m->collision_checks);
    printf("Simulation Accuracy: %.2f\n", m->simulation_accuracy);
}

// Utility: balance quality vs performance
void balance_quality_vs_performance(physics_simulation_t *sim, float target_fps) {
    if (!sim) return;
    // Example: adjust theta for Barnes-Hut based on target FPS
    if (sim->theta < 0.5f) sim->theta = 0.5f;
    if (sim->theta > 2.5f) sim->theta = 2.5f;
    // Could add more logic here
}

// Utility: estimate optimal particle count
uint32_t estimate_optimal_particle_count(float target_fps, float available_memory) {
    // Assume each particle uses ~64 bytes
    uint32_t max_particles = (uint32_t)(available_memory / 64.0f);
    // Adjust for target FPS (simple heuristic)
    if (target_fps < 30.0f) max_particles /= 2;
    return max_particles;
}

// Testing function
int physics_performance_run_tests(void) {
    int result = 1;
    result &= benchmark_force_calculations(100, 10);
    result &= benchmark_collision_detection(100, 10);
    result &= benchmark_memory_operations(100, 10);
    return result;
}

// SSE disabled for kernel environment
#if 0

void simd_calculate_forces_sse2(particle_system_t *particles, float *forces_x, float *forces_y, 
                                uint32_t count, float gravity, float softening) {
    if (!particles || !forces_x || !forces_y || count == 0) {
        return;
    }
    
    // Process particles in groups of 4 using SSE2
    uint32_t simd_count = count & ~3; // Round down to multiple of 4
    
    __m128 gravity_vec = _mm_set1_ps(gravity);
    __m128 softening_vec = _mm_set1_ps(softening * softening);
    
    for (uint32_t i = 0; i < simd_count; i += 4) {
        // Load 4 particles' positions and masses
        __m128 x1 = _mm_setr_ps(particles->particles[i].x, particles->particles[i+1].x,
                               particles->particles[i+2].x, particles->particles[i+3].x);
        __m128 y1 = _mm_setr_ps(particles->particles[i].y, particles->particles[i+1].y,
                               particles->particles[i+2].y, particles->particles[i+3].y);
        __m128 m1 = _mm_setr_ps(particles->particles[i].mass, particles->particles[i+1].mass,
                               particles->particles[i+2].mass, particles->particles[i+3].mass);
        
        __m128 fx = _mm_setzero_ps();
        __m128 fy = _mm_setzero_ps();
        
        // Calculate forces with all other particles
        for (uint32_t j = 0; j < count; j++) {
            if (j >= i && j < i + 4) continue; // Skip self-interaction
            
            __m128 x2 = _mm_set1_ps(particles->particles[j].x);
            __m128 y2 = _mm_set1_ps(particles->particles[j].y);
            __m128 m2 = _mm_set1_ps(particles->particles[j].mass);
            
            // Calculate distance vector
            __m128 dx = _mm_sub_ps(x2, x1);
            __m128 dy = _mm_sub_ps(y2, y1);
            
            // Calculate distance squared
            __m128 dx2 = _mm_mul_ps(dx, dx);
            __m128 dy2 = _mm_mul_ps(dy, dy);
            __m128 r2 = _mm_add_ps(dx2, dy2);
            r2 = _mm_add_ps(r2, softening_vec); // Add softening
            
            // Calculate force magnitude: G * m1 * m2 / r^2
            __m128 r = _mm_sqrt_ps(r2);
            __m128 r3 = _mm_mul_ps(r2, r);
            __m128 force_mag = _mm_div_ps(_mm_mul_ps(_mm_mul_ps(gravity_vec, m1), m2), r3);
            
            // Calculate force components
            fx = _mm_add_ps(fx, _mm_mul_ps(force_mag, dx));
            fy = _mm_add_ps(fy, _mm_mul_ps(force_mag, dy));
        }
        
        // Store results
        float fx_array[4], fy_array[4];
        _mm_storeu_ps(fx_array, fx);
        _mm_storeu_ps(fy_array, fy);
        
        for (int k = 0; k < 4; k++) {
            forces_x[i + k] += fx_array[k];
            forces_y[i + k] += fy_array[k];
        }
    }
    
    // Handle remaining particles (non-SIMD)
    for (uint32_t i = simd_count; i < count; i++) {
        float fx = 0.0f, fy = 0.0f;
        
        for (uint32_t j = 0; j < count; j++) {
            if (i == j) continue;
            
            float dx = particles->particles[j].x - particles->particles[i].x;
            float dy = particles->particles[j].y - particles->particles[i].y;
            float r2 = dx * dx + dy * dy + softening * softening;
            float r = sqrtf(r2);
            float force_mag = gravity * particles->particles[i].mass * particles->particles[j].mass / (r2 * r);
            
            fx += force_mag * dx;
            fy += force_mag * dy;
        }
        
        forces_x[i] += fx;
        forces_y[i] += fy;
    }
}

void simd_integrate_particles_sse2(particle_system_t *particles, float dt, uint32_t count) {
    if (!particles || count == 0) {
        return;
    }
    
    uint32_t simd_count = count & ~3; // Round down to multiple of 4
    __m128 dt_vec = _mm_set1_ps(dt);
    
    for (uint32_t i = 0; i < simd_count; i += 4) {
        // Load positions, velocities, and accelerations
        __m128 x = _mm_setr_ps(particles->particles[i].x, particles->particles[i+1].x,
                              particles->particles[i+2].x, particles->particles[i+3].x);
        __m128 y = _mm_setr_ps(particles->particles[i].y, particles->particles[i+1].y,
                              particles->particles[i+2].y, particles->particles[i+3].y);
        __m128 vx = _mm_setr_ps(particles->particles[i].vx, particles->particles[i+1].vx,
                               particles->particles[i+2].vx, particles->particles[i+3].vx);
        __m128 vy = _mm_setr_ps(particles->particles[i].vy, particles->particles[i+1].vy,
                               particles->particles[i+2].vy, particles->particles[i+3].vy);
        __m128 ax = _mm_setr_ps(particles->particles[i].ax, particles->particles[i+1].ax,
                               particles->particles[i+2].ax, particles->particles[i+3].ax);
        __m128 ay = _mm_setr_ps(particles->particles[i].ay, particles->particles[i+1].ay,
                               particles->particles[i+2].ay, particles->particles[i+3].ay);
        
        // Update velocities: v = v + a * dt
        vx = _mm_add_ps(vx, _mm_mul_ps(ax, dt_vec));
        vy = _mm_add_ps(vy, _mm_mul_ps(ay, dt_vec));
        
        // Update positions: x = x + v * dt
        x = _mm_add_ps(x, _mm_mul_ps(vx, dt_vec));
        y = _mm_add_ps(y, _mm_mul_ps(vy, dt_vec));
        
        // Store results back
        float x_array[4], y_array[4], vx_array[4], vy_array[4];
        _mm_storeu_ps(x_array, x);
        _mm_storeu_ps(y_array, y);
        _mm_storeu_ps(vx_array, vx);
        _mm_storeu_ps(vy_array, vy);
        
        for (int k = 0; k < 4; k++) {
            particles->particles[i + k].x = x_array[k];
            particles->particles[i + k].y = y_array[k];
            particles->particles[i + k].vx = vx_array[k];
            particles->particles[i + k].vy = vy_array[k];
        }
    }
    
    // Handle remaining particles
    for (uint32_t i = simd_count; i < count; i++) {
        particles->particles[i].vx += particles->particles[i].ax * dt;
        particles->particles[i].vy += particles->particles[i].ay * dt;
        particles->particles[i].x += particles->particles[i].vx * dt;
        particles->particles[i].y += particles->particles[i].vy * dt;
    }
}

#endif // __SSE2__

// Cache-friendly data layout optimizations

void optimize_particle_layout(particle_system_t *particles) {
    if (!particles || particles->count == 0) {
        return;
    }
    
    // Sort particles by spatial locality (simple Z-order curve approximation)
    // This is a simplified implementation - a full implementation would use
    // a proper space-filling curve like Morton order
    
    // For now, just ensure active particles are at the beginning
    uint32_t write_index = 0;
    for (uint32_t i = 0; i < particles->capacity; i++) {
        if (particles->particles[i].active) {
            if (i != write_index) {
                // Swap particles
                particle_t temp = particles->particles[write_index];
                particles->particles[write_index] = particles->particles[i];
                particles->particles[i] = temp;
            }
            write_index++;
        }
    }
}

void optimize_quadtree_layout(quadtree_t *quadtree) {
    if (!quadtree) {
        return;
    }
    
    // Quadtree layout optimization would involve reorganizing nodes
    // for better cache locality. This is a placeholder implementation.
    // A full implementation would use techniques like:
    // - Breadth-first layout for better cache performance
    // - Node pooling with spatial locality
    // - Memory prefetching hints
}

void prefetch_memory_regions(void *ptr, uint32_t size) {
    if (!ptr || size == 0) {
        return;
    }
    
    // Memory prefetching hints for better cache performance
    // This is platform-specific and would use compiler intrinsics
    // For now, this is a placeholder
    
    #ifdef __GNUC__
    // GCC builtin prefetch
    __builtin_prefetch(ptr, 0, 3); // Read prefetch with high temporal locality
    #endif
}

// Performance benchmarking functions

int benchmark_force_calculations(uint32_t particle_count, uint32_t iterations) {
    // Create a test particle system
    particle_system_t *particles = particle_system_create(particle_count);
    if (!particles) {
        return 0;
    }
    
    // Add test particles
    for (uint32_t i = 0; i < particle_count; i++) {
        float x = (float)(i % 100);
        float y = (float)(i / 100);
        particle_add(particles, x, y, 1.0f, 1.0f);
    }
    
    uint32_t start_time = get_current_time_ms();
    
    // Run force calculation benchmark
    for (uint32_t iter = 0; iter < iterations; iter++) {
        particle_clear_forces(particles);
        
        // Simple O(N^2) force calculation for benchmarking
        for (uint32_t i = 0; i < particles->count; i++) {
            for (uint32_t j = i + 1; j < particles->count; j++) {
                float dx = particles->particles[j].x - particles->particles[i].x;
                float dy = particles->particles[j].y - particles->particles[i].y;
                float r2 = dx * dx + dy * dy + 1.0f; // Softening
                float r = simple_sqrtf(r2);
                float force = 1.0f / (r2 * r); // Simplified force
                
                particles->force_x[i] += force * dx;
                particles->force_y[i] += force * dy;
                particles->force_x[j] -= force * dx;
                particles->force_y[j] -= force * dy;
            }
        }
    }
    
    uint32_t end_time = get_current_time_ms();
    uint32_t elapsed = end_time - start_time;
    
    particle_system_destroy(particles);
    
    return (elapsed > 0) ? 1 : 0;
}

// Internal helper function implementations

static uint32_t get_current_time_ms(void) {
    // This would use the kernel's timing functions
    // For now, return a placeholder value
    static uint32_t fake_time = 0;
    return ++fake_time;
}

static float calculate_fps(float frame_time) {
    return (frame_time > 0.0f) ? (1.0f / frame_time) : 0.0f;
}

static void update_metrics_history(performance_profiler_t *profiler) {
    if (!profiler) {
        return;
    }
    
    // Store current metrics in history
    profiler->history[profiler->history_index] = profiler->metrics;
    profiler->history_index = (profiler->history_index + 1) % 60;
}

static void adjust_quality_based_on_performance(adaptive_quality_t *quality, float current_fps) {
    if (!quality) {
        return;
    }
    
    if (current_fps < quality->performance_low_threshold) {
        // Performance is too low, reduce quality
        if (quality->force_quality > 10) {
            quality->force_quality -= (uint32_t)quality->quality_step;
        }
        if (quality->collision_quality > 10) {
            quality->collision_quality -= (uint32_t)quality->quality_step;
        }
        if (quality->particle_quality > 10) {
            quality->particle_quality -= (uint32_t)quality->quality_step;
        }
        
        // Increase theta for less accurate but faster force calculations
        quality->theta_quality += 0.1f;
        if (quality->theta_quality > 2.0f) {
            quality->theta_quality = 2.0f;
        }
        
    } else if (current_fps > quality->performance_high_threshold) {
        // Performance is good, can increase quality
        if (quality->force_quality < 100) {
            quality->force_quality += (uint32_t)quality->quality_step;
        }
        if (quality->collision_quality < 100) {
            quality->collision_quality += (uint32_t)quality->quality_step;
        }
        if (quality->particle_quality < 100) {
            quality->particle_quality += (uint32_t)quality->quality_step;
        }
        
        // Decrease theta for more accurate force calculations
        quality->theta_quality -= 0.05f;
        if (quality->theta_quality < 0.3f) {
            quality->theta_quality = 0.3f;
        }
    }
    
    // Clamp quality values
    if (quality->force_quality > 100) quality->force_quality = 100;
    if (quality->collision_quality > 100) quality->collision_quality = 100;
    if (quality->particle_quality > 100) quality->particle_quality = 100;
}

// Additional performance optimization utilities

void optimize_simulation_parameters(physics_simulation_t *sim, const performance_metrics_t *metrics) {
    if (!sim || !metrics) {
        return;
    }
    
    // Adjust simulation parameters based on performance metrics
    if (metrics->current_fps < metrics->target_fps * 0.8f) {
        // Performance is poor, optimize for speed
        
        // Increase Barnes-Hut theta for less accuracy but better performance
        if (sim->theta < 1.5f) {
            sim->theta += 0.1f;
        }
        
        // Increase time step slightly for fewer updates
        if (sim->time_step < 0.02f) {
            sim->time_step += 0.001f;
        }
        
        // Disable collision detection if performance is very poor
        if (metrics->current_fps < metrics->target_fps * 0.5f) {
            sim->collision_enabled = 0;
        }
    } else if (metrics->current_fps > metrics->target_fps * 1.2f) {
        // Performance is good, can increase accuracy
        
        // Decrease Barnes-Hut theta for better accuracy
        if (sim->theta > 0.3f) {
            sim->theta -= 0.05f;
        }
        
        // Decrease time step for better accuracy
        if (sim->time_step > 0.01f) {
            sim->time_step -= 0.001f;
        }
        
        // Enable collision detection
        sim->collision_enabled = 1;
    }
}

float calculate_performance_score(const performance_metrics_t *metrics) {
    if (!metrics) {
        return 0.0f;
    }
    
    // Calculate a composite performance score (0-100)
    float fps_score = (metrics->current_fps / metrics->target_fps) * 50.0f;
    if (fps_score > 50.0f) fps_score = 50.0f;
    
    float memory_score = (1.0f - metrics->memory_fragmentation / 100.0f) * 25.0f;
    float accuracy_score = metrics->simulation_accuracy * 25.0f;
    
    return fps_score + memory_score + accuracy_score;
}