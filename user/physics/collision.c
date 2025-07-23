#include "collision.h"
#include "../kernel.h"
#include <stddef.h>

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFU
#endif

// Static function declarations
static void collision_traverse_quadtree(collision_system_t *system, const quadtree_node_t *node, const particle_system_t *particles);
static void collision_find_nearby_particles(collision_system_t *system, const quadtree_node_t *node, 
                                          const particle_system_t *particles, uint32_t particle_idx);

// Math utility functions
static float sqrt_approx(float x) {
    if (x <= 0.0f) return 0.0f;
    
    // Fast inverse square root approximation (Quake III algorithm)
    float xhalf = 0.5f * x;
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    u.f = u.f * (1.5f - xhalf * u.f * u.f);
    return x * u.f;
}

// Create a new collision system with specified capacities
collision_system_t* collision_system_create(uint32_t max_collisions, uint32_t max_pairs) {
    if (max_collisions == 0 || max_pairs == 0) {
        return NULL;
    }
    
    collision_system_t *system = (collision_system_t*)kernel_malloc(sizeof(collision_system_t));
    if (!system) {
        return NULL;
    }
    
    // Allocate collision array
    system->collisions = (collision_t*)kernel_malloc(sizeof(collision_t) * max_collisions);
    if (!system->collisions) {
        kernel_free(system);
        return NULL;
    }
    
    // Allocate broad phase pairs array
    system->broad_phase_pairs = (uint32_t*)kernel_malloc(sizeof(uint32_t) * max_pairs * 2);
    if (!system->broad_phase_pairs) {
        kernel_free(system->collisions);
        kernel_free(system);
        return NULL;
    }
    
    // Initialize system state
    system->count = 0;
    system->capacity = max_collisions;
    system->pair_count = 0;
    system->pair_capacity = max_pairs;
    
    return system;
}

// Destroy collision system and free all allocated memory
void collision_system_destroy(collision_system_t *system) {
    if (!system) {
        return;
    }
    
    if (system->broad_phase_pairs) {
        kernel_free(system->broad_phase_pairs);
    }
    if (system->collisions) {
        kernel_free(system->collisions);
    }
    kernel_free(system);
}

// Clear all collisions and pairs from the system
void collision_system_clear(collision_system_t *system) {
    if (!system) {
        return;
    }
    
    system->count = 0;
    system->pair_count = 0;
    
    // Reset resolved flags for all collisions
    for (uint32_t i = 0; i < system->capacity; i++) {
        system->collisions[i].resolved = 0;
    }
}

// Broad phase collision detection using quadtree
void collision_detect_broad_phase(collision_system_t *system, const quadtree_t *tree, const particle_system_t *particles) {
    if (!system || !tree || !particles) {
        return;
    }
    
    system->pair_count = 0;
    
    // Simple O(N²) broad phase for now - can be optimized with proper quadtree traversal
    for (uint32_t i = 0; i < particles->capacity && system->pair_count < system->pair_capacity; i++) {
        if (!particles->particles[i].active) {
            continue;
        }
        
        for (uint32_t j = i + 1; j < particles->capacity && system->pair_count < system->pair_capacity; j++) {
            if (!particles->particles[j].active) {
                continue;
            }
            
            const particle_t *a = &particles->particles[i];
            const particle_t *b = &particles->particles[j];
            
            // Quick distance check for broad phase
            float dx = a->x - b->x;
            float dy = a->y - b->y;
            float distance_sq = dx * dx + dy * dy;
            float radius_sum = a->radius + b->radius;
            
            // If particles are close enough to potentially collide (with margin)
            if (distance_sq <= radius_sum * radius_sum * 1.44f) { // 20% margin (1.2²)
                system->broad_phase_pairs[system->pair_count * 2] = i;
                system->broad_phase_pairs[system->pair_count * 2 + 1] = j;
                system->pair_count++;
            }
        }
    }
}

// Helper function to recursively traverse quadtree for broad phase detection (unused for now)
static void collision_traverse_quadtree(collision_system_t *system, const quadtree_node_t *node, const particle_system_t *particles) {
    // This function is reserved for future quadtree-based broad phase optimization
    (void)system;
    (void)node;
    (void)particles;
}

// Find nearby particles for broad phase detection (unused for now)
static void collision_find_nearby_particles(collision_system_t *system, const quadtree_node_t *node, 
                                          const particle_system_t *particles, uint32_t particle_idx) {
    // This function is reserved for future quadtree-based broad phase optimization
    (void)system;
    (void)node;
    (void)particles;
    (void)particle_idx;
}

// Narrow phase collision detection
void collision_detect_narrow_phase(collision_system_t *system, const particle_system_t *particles) {
    if (!system || !particles) {
        return;
    }
    
    system->count = 0;
    
    // Check each broad phase pair for actual collision
    for (uint32_t i = 0; i < system->pair_count && system->count < system->capacity; i++) {
        uint32_t idx_a = system->broad_phase_pairs[i * 2];
        uint32_t idx_b = system->broad_phase_pairs[i * 2 + 1];
        
        if (idx_a >= particles->capacity || idx_b >= particles->capacity) {
            continue;
        }
        
        const particle_t *particle_a = &particles->particles[idx_a];
        const particle_t *particle_b = &particles->particles[idx_b];
        
        if (!particle_a->active || !particle_b->active) {
            continue;
        }
        
        // Perform precise collision detection
        collision_t *collision = &system->collisions[system->count];
        if (collision_detect_particles(particle_a, particle_b, collision, idx_a, idx_b)) {
            system->count++;
        }
    }
}

// Detect collision between two particles (circle-circle intersection)
int collision_detect_particles(const particle_t *a, const particle_t *b, collision_t *collision, uint32_t idx_a, uint32_t idx_b) {
    if (!a || !b || !collision) {
        return 0;
    }
    
    // Calculate distance between particle centers
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float distance_sq = dx * dx + dy * dy;
    float radius_sum = a->radius + b->radius;
    
    // Check if particles are overlapping
    if (distance_sq >= radius_sum * radius_sum) {
        return 0; // No collision
    }
    
    // Calculate collision details
    float distance = sqrt_approx(distance_sq);
    collision->particle_a = idx_a;
    collision->particle_b = idx_b;
    collision->overlap = radius_sum - distance;
    
    // Calculate collision normal (normalized direction from b to a)
    if (distance > 0.0001f) {
        collision->normal_x = dx / distance;
        collision->normal_y = dy / distance;
    } else {
        // Handle case where particles are at exactly the same position
        collision->normal_x = 1.0f;
        collision->normal_y = 0.0f;
    }
    
    // Calculate relative velocity along collision normal
    float rel_vx = a->vx - b->vx;
    float rel_vy = a->vy - b->vy;
    collision->relative_velocity = rel_vx * collision->normal_x + rel_vy * collision->normal_y;
    
    collision->resolved = 0;
    
    return 1; // Collision detected
}

// Calculate overlap between two particles
float collision_calculate_overlap(const particle_t *a, const particle_t *b) {
    if (!a || !b) {
        return 0.0f;
    }
    
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float distance = sqrt_approx(dx * dx + dy * dy);
    float radius_sum = a->radius + b->radius;
    
    return (distance < radius_sum) ? (radius_sum - distance) : 0.0f;
}

// Calculate collision normal vector (normalized direction from b to a)
void collision_calculate_normal(const particle_t *a, const particle_t *b, float *normal_x, float *normal_y) {
    if (!a || !b || !normal_x || !normal_y) {
        return;
    }
    
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float distance = sqrt_approx(dx * dx + dy * dy);
    
    if (distance > 0.0001f) {
        *normal_x = dx / distance;
        *normal_y = dy / distance;
    } else {
        // Default normal if particles are at same position
        *normal_x = 1.0f;
        *normal_y = 0.0f;
    }
}

// Calculate relative velocity along collision normal
float collision_calculate_relative_velocity(const particle_t *a, const particle_t *b, float normal_x, float normal_y) {
    if (!a || !b) {
        return 0.0f;
    }
    
    float rel_vx = a->vx - b->vx;
    float rel_vy = a->vy - b->vy;
    
    return rel_vx * normal_x + rel_vy * normal_y;
}

// Resolve all detected collisions
void collision_resolve_all(collision_system_t *system, particle_system_t *particles, float restitution) {
    if (!system || !particles) {
        return;
    }
    
    for (uint32_t i = 0; i < system->count; i++) {
        collision_t *collision = &system->collisions[i];
        if (!collision->resolved) {
            collision_resolve_single(collision, particles, restitution);
            collision->resolved = 1;
        }
    }
}

// Resolve a single collision with momentum conservation
void collision_resolve_single(const collision_t *collision, particle_system_t *particles, float restitution) {
    if (!collision || !particles) {
        return;
    }
    
    uint32_t idx_a = collision->particle_a;
    uint32_t idx_b = collision->particle_b;
    
    if (idx_a >= particles->capacity || idx_b >= particles->capacity) {
        return;
    }
    
    particle_t *a = &particles->particles[idx_a];
    particle_t *b = &particles->particles[idx_b];
    
    if (!a->active || !b->active) {
        return;
    }
    
    // Separate particles first to prevent interpenetration
    collision_separate_particles(particles, idx_a, idx_b, collision->overlap, 
                                collision->normal_x, collision->normal_y);
    
    // Only resolve if particles are approaching each other
    if (collision->relative_velocity > 0.0f) {
        return; // Particles are separating
    }
    
    // Calculate impulse magnitude for elastic collision
    float total_mass = a->mass + b->mass;
    if (total_mass <= 0.0f) {
        return;
    }
    
    float impulse_magnitude = -(1.0f + restitution) * collision->relative_velocity / total_mass;
    
    // Apply impulse to both particles
    float impulse_x = impulse_magnitude * collision->normal_x;
    float impulse_y = impulse_magnitude * collision->normal_y;
    
    // Update velocities based on momentum conservation
    a->vx += impulse_x * b->mass / a->mass;
    a->vy += impulse_y * b->mass / a->mass;
    b->vx -= impulse_x * a->mass / b->mass;
    b->vy -= impulse_y * a->mass / b->mass;
}

// Separate overlapping particles to prevent interpenetration
void collision_separate_particles(particle_system_t *particles, uint32_t idx_a, uint32_t idx_b, 
                                 float overlap, float normal_x, float normal_y) {
    if (!particles || idx_a >= particles->capacity || idx_b >= particles->capacity) {
        return;
    }
    
    particle_t *a = &particles->particles[idx_a];
    particle_t *b = &particles->particles[idx_b];
    
    if (!a->active || !b->active || overlap <= 0.0f) {
        return;
    }
    
    // Calculate separation based on mass ratio
    float total_mass = a->mass + b->mass;
    if (total_mass <= 0.0f) {
        return;
    }
    
    float separation_a = overlap * (b->mass / total_mass);
    float separation_b = overlap * (a->mass / total_mass);
    
    // Move particles apart along collision normal
    a->x += separation_a * normal_x;
    a->y += separation_a * normal_y;
    b->x -= separation_b * normal_x;
    b->y -= separation_b * normal_y;
}