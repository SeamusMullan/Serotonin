#ifndef PARTICLE_H
#define PARTICLE_H

#include <stdint.h>

// Individual particle data structure
typedef struct particle {
    float x, y;                   // Position coordinates
    float vx, vy;                 // Velocity components
    float ax, ay;                 // Acceleration components
    float mass;                   // Particle mass
    float radius;                 // Collision radius
    uint32_t color;               // Rendering color (ARGB format)
    uint8_t active;               // Active flag (1 = active, 0 = inactive)
} particle_t;

// Particle system management structure
typedef struct particle_system {
    particle_t *particles;        // Array of particles
    uint32_t count;               // Current number of active particles
    uint32_t capacity;            // Maximum particle capacity
    float *force_x;               // Force accumulation array (X component)
    float *force_y;               // Force accumulation array (Y component)
    float bounds_x, bounds_y;     // Simulation boundary position
    float bounds_width, bounds_height; // Simulation boundary dimensions
} particle_system_t;

// Function declarations
particle_system_t* particle_system_create(uint32_t max_particles);
void particle_system_destroy(particle_system_t *system);
uint32_t particle_add(particle_system_t *system, float x, float y, float mass, float radius);
void particle_remove(particle_system_t *system, uint32_t index);
void particle_update_physics(particle_system_t *system, float dt);
void particle_clear_forces(particle_system_t *system);
void particle_apply_force(particle_system_t *system, uint32_t index, float fx, float fy);
void particle_set_bounds(particle_system_t *system, float x, float y, float width, float height);

// Utility functions
float particle_distance_squared(const particle_t *a, const particle_t *b);
float particle_distance(const particle_t *a, const particle_t *b);
void particle_set_color_by_mass(particle_t *particle);
void particle_set_color_by_velocity(particle_t *particle);

#endif // PARTICLE_H