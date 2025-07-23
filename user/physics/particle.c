#include "particle.h"
#include "../kernel.h"
#include <stddef.h>

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFU
#endif

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

// Create a new particle system with specified maximum capacity
particle_system_t* particle_system_create(uint32_t max_particles) {
    if (max_particles == 0) {
        return NULL;
    }
    
    particle_system_t *system = (particle_system_t*)kernel_malloc(sizeof(particle_system_t));
    if (!system) {
        return NULL;
    }
    
    // Allocate particle array
    system->particles = (particle_t*)kernel_malloc(sizeof(particle_t) * max_particles);
    if (!system->particles) {
        kernel_free(system);
        return NULL;
    }
    
    // Allocate force accumulation arrays
    system->force_x = (float*)kernel_malloc(sizeof(float) * max_particles);
    if (!system->force_x) {
        kernel_free(system->particles);
        kernel_free(system);
        return NULL;
    }
    
    system->force_y = (float*)kernel_malloc(sizeof(float) * max_particles);
    if (!system->force_y) {
        kernel_free(system->force_x);
        kernel_free(system->particles);
        kernel_free(system);
        return NULL;
    }
    
    // Initialize system state
    system->count = 0;
    system->capacity = max_particles;
    system->bounds_x = 0.0f;
    system->bounds_y = 0.0f;
    system->bounds_width = 1280.0f;  // Default to VBE screen width
    system->bounds_height = 800.0f;  // Default to VBE screen height
    
    // Initialize all particles as inactive
    for (uint32_t i = 0; i < max_particles; i++) {
        system->particles[i].active = 0;
        system->force_x[i] = 0.0f;
        system->force_y[i] = 0.0f;
    }
    
    return system;
}

// Destroy particle system and free all allocated memory
void particle_system_destroy(particle_system_t *system) {
    if (!system) {
        return;
    }
    
    if (system->force_y) {
        kernel_free(system->force_y);
    }
    if (system->force_x) {
        kernel_free(system->force_x);
    }
    if (system->particles) {
        kernel_free(system->particles);
    }
    kernel_free(system);
}

// Add a new particle to the system
uint32_t particle_add(particle_system_t *system, float x, float y, float mass, float radius) {
    if (!system || system->count >= system->capacity) {
        return UINT32_MAX; // Invalid index indicates failure
    }
    
    // Find first inactive particle slot
    uint32_t index = UINT32_MAX;
    for (uint32_t i = 0; i < system->capacity; i++) {
        if (!system->particles[i].active) {
            index = i;
            break;
        }
    }
    
    if (index == UINT32_MAX) {
        return UINT32_MAX; // No available slots
    }
    
    // Initialize particle
    particle_t *particle = &system->particles[index];
    particle->x = x;
    particle->y = y;
    particle->vx = 0.0f;
    particle->vy = 0.0f;
    particle->ax = 0.0f;
    particle->ay = 0.0f;
    particle->mass = mass;
    particle->radius = radius;
    particle->color = 0xFFFFFFFF; // White by default
    particle->active = 1;
    
    // Clear forces for this particle
    system->force_x[index] = 0.0f;
    system->force_y[index] = 0.0f;
    
    system->count++;
    
    // Set color based on mass
    particle_set_color_by_mass(particle);
    
    return index;
}

// Remove a particle from the system
void particle_remove(particle_system_t *system, uint32_t index) {
    if (!system || index >= system->capacity) {
        return;
    }
    
    if (system->particles[index].active) {
        system->particles[index].active = 0;
        system->force_x[index] = 0.0f;
        system->force_y[index] = 0.0f;
        system->count--;
    }
}

// Update particle physics using Verlet integration
void particle_update_physics(particle_system_t *system, float dt) {
    if (!system) {
        return;
    }
    
    const float dt_squared = dt * dt;
    
    for (uint32_t i = 0; i < system->capacity; i++) {
        particle_t *p = &system->particles[i];
        if (!p->active) {
            continue;
        }
        
        // Calculate acceleration from accumulated forces
        float ax = system->force_x[i] / p->mass;
        float ay = system->force_y[i] / p->mass;
        
        // Verlet integration: x(t+dt) = x(t) + v(t)*dt + 0.5*a(t)*dt^2
        float new_x = p->x + p->vx * dt + 0.5f * ax * dt_squared;
        float new_y = p->y + p->vy * dt + 0.5f * ay * dt_squared;
        
        // Update velocity: v(t+dt) = v(t) + a(t)*dt
        p->vx += ax * dt;
        p->vy += ay * dt;
        
        // Apply boundary conditions (wrap around)
        if (new_x < system->bounds_x) {
            new_x = system->bounds_x + system->bounds_width;
        } else if (new_x > system->bounds_x + system->bounds_width) {
            new_x = system->bounds_x;
        }
        
        if (new_y < system->bounds_y) {
            new_y = system->bounds_y + system->bounds_height;
        } else if (new_y > system->bounds_y + system->bounds_height) {
            new_y = system->bounds_y;
        }
        
        // Update position
        p->x = new_x;
        p->y = new_y;
        
        // Store acceleration for next frame
        p->ax = ax;
        p->ay = ay;
    }
}

// Clear all accumulated forces
void particle_clear_forces(particle_system_t *system) {
    if (!system) {
        return;
    }
    
    for (uint32_t i = 0; i < system->capacity; i++) {
        system->force_x[i] = 0.0f;
        system->force_y[i] = 0.0f;
    }
}

// Apply force to a specific particle
void particle_apply_force(particle_system_t *system, uint32_t index, float fx, float fy) {
    if (!system || index >= system->capacity || !system->particles[index].active) {
        return;
    }
    
    system->force_x[index] += fx;
    system->force_y[index] += fy;
}

// Set simulation boundaries
void particle_set_bounds(particle_system_t *system, float x, float y, float width, float height) {
    if (!system) {
        return;
    }
    
    system->bounds_x = x;
    system->bounds_y = y;
    system->bounds_width = width;
    system->bounds_height = height;
}

// Calculate squared distance between two particles (faster than distance)
float particle_distance_squared(const particle_t *a, const particle_t *b) {
    if (!a || !b) {
        return 0.0f;
    }
    
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    return dx * dx + dy * dy;
}

// Calculate distance between two particles
float particle_distance(const particle_t *a, const particle_t *b) {
    return sqrt_approx(particle_distance_squared(a, b));
}

// Set particle color based on mass (heavier = redder, lighter = bluer)
void particle_set_color_by_mass(particle_t *particle) {
    if (!particle) {
        return;
    }
    
    // Normalize mass to 0-1 range (assuming mass range 0.1 to 10.0)
    float normalized_mass = (particle->mass - 0.1f) / 9.9f;
    if (normalized_mass < 0.0f) normalized_mass = 0.0f;
    if (normalized_mass > 1.0f) normalized_mass = 1.0f;
    
    // Interpolate between blue (low mass) and red (high mass)
    uint8_t red = (uint8_t)(255 * normalized_mass);
    uint8_t green = (uint8_t)(128 * (1.0f - normalized_mass * normalized_mass));
    uint8_t blue = (uint8_t)(255 * (1.0f - normalized_mass));
    
    particle->color = 0xFF000000 | (red << 16) | (green << 8) | blue;
}

// Set particle color based on velocity (faster = brighter)
void particle_set_color_by_velocity(particle_t *particle) {
    if (!particle) {
        return;
    }
    
    // Calculate velocity magnitude
    float velocity = sqrt_approx(particle->vx * particle->vx + particle->vy * particle->vy);
    
    // Normalize velocity to 0-1 range (assuming max velocity ~100)
    float normalized_velocity = velocity / 100.0f;
    if (normalized_velocity > 1.0f) normalized_velocity = 1.0f;
    
    // Create color based on velocity (HSV-like approach)
    uint8_t intensity = (uint8_t)(255 * normalized_velocity);
    uint8_t red = intensity;
    uint8_t green = (uint8_t)(intensity * 0.7f);
    uint8_t blue = (uint8_t)(intensity * 0.3f);
    
    particle->color = 0xFF000000 | (red << 16) | (green << 8) | blue;
}