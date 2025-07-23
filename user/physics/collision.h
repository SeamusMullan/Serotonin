#ifndef COLLISION_H
#define COLLISION_H

#include <stdint.h>
#include "particle.h"
#include "quadtree.h"

// Forward declarations
typedef struct collision collision_t;
typedef struct collision_system collision_system_t;

// Individual collision data structure
struct collision {
    uint32_t particle_a;          // Index of first particle
    uint32_t particle_b;          // Index of second particle
    float overlap;                // Amount of overlap between particles
    float normal_x, normal_y;     // Collision normal vector (normalized)
    float relative_velocity;      // Relative velocity along normal
    uint8_t resolved;             // Flag indicating if collision was resolved
};

// Collision system management structure
struct collision_system {
    collision_t *collisions;      // Array of detected collisions
    uint32_t count;               // Current number of collisions
    uint32_t capacity;            // Maximum collision capacity
    uint32_t *broad_phase_pairs;  // Potential collision pairs from broad phase
    uint32_t pair_count;          // Number of potential pairs
    uint32_t pair_capacity;       // Maximum pair capacity
};

// Function declarations
collision_system_t* collision_system_create(uint32_t max_collisions, uint32_t max_pairs);
void collision_system_destroy(collision_system_t *system);
void collision_system_clear(collision_system_t *system);

// Collision detection functions
void collision_detect_broad_phase(collision_system_t *system, const quadtree_t *tree, const particle_system_t *particles);
void collision_detect_narrow_phase(collision_system_t *system, const particle_system_t *particles);
int collision_detect_particles(const particle_t *a, const particle_t *b, collision_t *collision, uint32_t idx_a, uint32_t idx_b);

// Collision response functions
void collision_resolve_all(collision_system_t *system, particle_system_t *particles, float restitution);
void collision_resolve_single(const collision_t *collision, particle_system_t *particles, float restitution);
void collision_separate_particles(particle_system_t *particles, uint32_t idx_a, uint32_t idx_b, float overlap, float normal_x, float normal_y);

// Utility functions
float collision_calculate_overlap(const particle_t *a, const particle_t *b);
void collision_calculate_normal(const particle_t *a, const particle_t *b, float *normal_x, float *normal_y);
float collision_calculate_relative_velocity(const particle_t *a, const particle_t *b, float normal_x, float normal_y);

#endif // COLLISION_H