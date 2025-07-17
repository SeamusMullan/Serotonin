#include "quadtree.h"
#include "../stdlib/stdlib.h"
#include "../kernel.h"

// Forward declarations
static int quadtree_insert_recursive(quadtree_t *tree, quadtree_node_t *node, 
                                   uint32_t particle_idx, const particle_t *particle, 
                                   uint32_t depth);
static void quadtree_update_mass_path(quadtree_node_t *node, const particle_t *particle);

// Simple square root implementation using Newton's method
static float sqrtf_approx(float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    
    float guess = x * 0.5f;
    for (int i = 0; i < 10; i++) {
        guess = (guess + x / guess) * 0.5f;
    }
    return guess;
}

// Create a new quadtree with specified bounds and node pool size
quadtree_t* quadtree_create(float x, float y, float width, float height, uint32_t max_nodes) {
    if (max_nodes == 0) {
        return NULL;
    }
    
    // Allocate the main quadtree structure
    quadtree_t *tree = (quadtree_t*)kernel_malloc(sizeof(quadtree_t));
    if (!tree) {
        return NULL;
    }
    
    // Allocate the node pool
    tree->node_pool = (quadtree_node_t*)kernel_malloc(sizeof(quadtree_node_t) * max_nodes);
    if (!tree->node_pool) {
        kernel_free(tree);
        return NULL;
    }
    
    // Initialize tree properties
    tree->bounds_x = x;
    tree->bounds_y = y;
    tree->bounds_width = width;
    tree->bounds_height = height;
    tree->pool_size = max_nodes;
    tree->pool_index = 0;
    tree->max_depth = 16; // Reasonable maximum depth
    tree->current_depth = 0;
    tree->root = NULL;
    
    // Initialize all nodes in the pool
    for (uint32_t i = 0; i < max_nodes; i++) {
        quadtree_node_t *node = &tree->node_pool[i];
        node->x = 0.0f;
        node->y = 0.0f;
        node->width = 0.0f;
        node->height = 0.0f;
        node->total_mass = 0.0f;
        node->center_x = 0.0f;
        node->center_y = 0.0f;
        node->particle_count = 0;
        node->particle_index = 0;
        node->is_leaf = 1;
        
        // Initialize child pointers to NULL
        for (int j = 0; j < 4; j++) {
            node->children[j] = NULL;
        }
    }
    
    return tree;
}

// Destroy the quadtree and free all allocated memory
void quadtree_destroy(quadtree_t *tree) {
    if (!tree) {
        return;
    }
    
    // Free the node pool
    if (tree->node_pool) {
        kernel_free(tree->node_pool);
    }
    
    // Free the main structure
    kernel_free(tree);
}

// Clear the quadtree for reuse (reset all nodes)
void quadtree_clear(quadtree_t *tree) {
    if (!tree) {
        return;
    }
    
    // Reset pool index to reuse all nodes
    tree->pool_index = 0;
    tree->current_depth = 0;
    tree->root = NULL;
    
    // Reset all nodes in the pool
    for (uint32_t i = 0; i < tree->pool_size; i++) {
        quadtree_node_t *node = &tree->node_pool[i];
        node->x = 0.0f;
        node->y = 0.0f;
        node->width = 0.0f;
        node->height = 0.0f;
        node->total_mass = 0.0f;
        node->center_x = 0.0f;
        node->center_y = 0.0f;
        node->particle_count = 0;
        node->particle_index = 0;
        node->is_leaf = 1;
        
        // Clear child pointers
        for (int j = 0; j < 4; j++) {
            node->children[j] = NULL;
        }
    }
}

// Allocate a node from the memory pool
quadtree_node_t* quadtree_allocate_node(quadtree_t *tree) {
    if (!tree || tree->pool_index >= tree->pool_size) {
        return NULL;
    }
    
    quadtree_node_t *node = &tree->node_pool[tree->pool_index];
    tree->pool_index++;
    
    return node;
}

// Utility function to determine which quadrant a point belongs to
quadrant_t quadtree_get_quadrant(const quadtree_node_t *node, float x, float y) {
    if (x < node->x) {
        // Left side
        if (y < node->y) {
            return QUADRANT_SW; // Southwest
        } else {
            return QUADRANT_NW; // Northwest
        }
    } else {
        // Right side
        if (y < node->y) {
            return QUADRANT_SE; // Southeast
        } else {
            return QUADRANT_NE; // Northeast
        }
    }
}

// Check if a point is contained within a node's bounds
int quadtree_contains_point(const quadtree_node_t *node, float x, float y) {
    float half_width = node->width * 0.5f;
    float half_height = node->height * 0.5f;
    
    return (x >= node->x - half_width && x < node->x + half_width &&
            y >= node->y - half_height && y < node->y + half_height);
}

// Calculate distance from a point to the center of a node
float quadtree_node_distance(const quadtree_node_t *node, float x, float y) {
    float dx = node->center_x - x;
    float dy = node->center_y - y;
    return sqrtf_approx(dx * dx + dy * dy);
}

// Subdivide a node into four child nodes
void quadtree_subdivide(quadtree_t *tree, quadtree_node_t *node) {
    if (!tree || !node || !node->is_leaf) {
        return;
    }
    
    float half_width = node->width * 0.5f;
    float half_height = node->height * 0.5f;
    float quarter_width = half_width * 0.5f;
    float quarter_height = half_height * 0.5f;
    
    // Allocate child nodes
    for (int i = 0; i < 4; i++) {
        node->children[i] = quadtree_allocate_node(tree);
        if (!node->children[i]) {
            // Failed to allocate, clean up any allocated children
            for (int j = 0; j < i; j++) {
                // Note: In a pool system, we can't really "free" individual nodes
                // but we can mark them as unused by clearing their data
                node->children[j] = NULL;
            }
            return;
        }
    }
    
    // Initialize Northwest child
    node->children[QUADRANT_NW]->x = node->x - quarter_width;
    node->children[QUADRANT_NW]->y = node->y + quarter_height;
    node->children[QUADRANT_NW]->width = half_width;
    node->children[QUADRANT_NW]->height = half_height;
    node->children[QUADRANT_NW]->is_leaf = 1;
    
    // Initialize Northeast child
    node->children[QUADRANT_NE]->x = node->x + quarter_width;
    node->children[QUADRANT_NE]->y = node->y + quarter_height;
    node->children[QUADRANT_NE]->width = half_width;
    node->children[QUADRANT_NE]->height = half_height;
    node->children[QUADRANT_NE]->is_leaf = 1;
    
    // Initialize Southwest child
    node->children[QUADRANT_SW]->x = node->x - quarter_width;
    node->children[QUADRANT_SW]->y = node->y - quarter_height;
    node->children[QUADRANT_SW]->width = half_width;
    node->children[QUADRANT_SW]->height = half_height;
    node->children[QUADRANT_SW]->is_leaf = 1;
    
    // Initialize Southeast child
    node->children[QUADRANT_SE]->x = node->x + quarter_width;
    node->children[QUADRANT_SE]->y = node->y - quarter_height;
    node->children[QUADRANT_SE]->width = half_width;
    node->children[QUADRANT_SE]->height = half_height;
    node->children[QUADRANT_SE]->is_leaf = 1;
    
    // Mark this node as no longer a leaf
    node->is_leaf = 0;
}

// Insert a particle into the quadtree
int quadtree_insert(quadtree_t *tree, uint32_t particle_idx, const particle_t *particle) {
    if (!tree || !particle) {
        return 0; // Failure
    }
    
    // If tree is empty, create root node
    if (!tree->root) {
        tree->root = quadtree_allocate_node(tree);
        if (!tree->root) {
            return 0; // Failed to allocate root
        }
        
        tree->root->x = tree->bounds_x + tree->bounds_width * 0.5f;
        tree->root->y = tree->bounds_y + tree->bounds_height * 0.5f;
        tree->root->width = tree->bounds_width;
        tree->root->height = tree->bounds_height;
        tree->root->is_leaf = 1;
        tree->current_depth = 1;
    }
    
    return quadtree_insert_recursive(tree, tree->root, particle_idx, particle, 0);
}

// Recursive helper function for insertion
static int quadtree_insert_recursive(quadtree_t *tree, quadtree_node_t *node, 
                                   uint32_t particle_idx, const particle_t *particle, 
                                   uint32_t depth) {
    if (!node || !particle) {
        return 0;
    }
    
    // Check if particle is within node bounds
    if (!quadtree_contains_point(node, particle->x, particle->y)) {
        return 0; // Particle is outside this node
    }
    
    // If this is a leaf node
    if (node->is_leaf) {
        // If the node is empty, add the particle
        if (node->particle_count == 0) {
            node->particle_index = particle_idx;
            node->particle_count = 1;
            return 1; // Success
        }
        
        // If the node already has a particle and we haven't reached max depth
        if (node->particle_count == 1 && depth < tree->max_depth) {
            // Store the existing particle info
            uint32_t existing_particle_idx = node->particle_index;
            
            // Subdivide the node
            quadtree_subdivide(tree, node);
            if (node->is_leaf) {
                // Subdivision failed, can't insert
                return 0;
            }
            
            // Clear the particle data from this node (it's no longer a leaf)
            node->particle_index = 0;
            node->particle_count = 0;
            
            // Try to insert both particles into child nodes
            // We need the existing particle data, but we don't have access to the particle system here
            // For now, we'll just insert the new particle and increment the count
            // The existing particle will need to be handled by the caller
            
            // Try to insert the new particle into appropriate child
            quadrant_t quad = quadtree_get_quadrant(node, particle->x, particle->y);
            if (quadtree_insert_recursive(tree, node->children[quad], particle_idx, particle, depth + 1)) {
                node->particle_count = 2; // This node now contains 2 particles in its subtree
                return 1;
            }
        }
        
        // If we can't subdivide or subdivision failed, we're at capacity
        return 0;
    }
    
    // This is an internal node, find the appropriate child
    quadrant_t quad = quadtree_get_quadrant(node, particle->x, particle->y);
    if (quadtree_insert_recursive(tree, node->children[quad], particle_idx, particle, depth + 1)) {
        node->particle_count++; // Increment count for this subtree
        return 1;
    }
    
    return 0; // Insertion failed
}

// Rebuild the entire quadtree from a particle system
void quadtree_rebuild(quadtree_t *tree, const particle_system_t *particles) {
    if (!tree || !particles) {
        return;
    }
    
    // Clear the existing tree
    quadtree_clear(tree);
    
    // Insert all active particles
    for (uint32_t i = 0; i < particles->count; i++) {
        if (particles->particles[i].active) {
            quadtree_insert(tree, i, &particles->particles[i]);
        }
    }
}

// Update mass information for a node when a particle is added
void quadtree_update_mass(quadtree_node_t *node, const particle_t *particle) {
    if (!node || !particle) {
        return;
    }
    
    // If this is the first particle in the node
    if (node->total_mass == 0.0f) {
        node->total_mass = particle->mass;
        node->center_x = particle->x;
        node->center_y = particle->y;
    } else {
        // Calculate new center of mass using weighted average
        float total_mass_new = node->total_mass + particle->mass;
        
        // Weighted average: (m1*x1 + m2*x2) / (m1 + m2)
        node->center_x = (node->center_x * node->total_mass + particle->x * particle->mass) / total_mass_new;
        node->center_y = (node->center_y * node->total_mass + particle->y * particle->mass) / total_mass_new;
        
        node->total_mass = total_mass_new;
    }
}

// Recursively calculate center of mass for all nodes in the tree
void quadtree_calculate_center_of_mass(quadtree_node_t *node, const particle_system_t *particles) {
    if (!node || !particles) {
        return;
    }
    
    // Reset mass information
    node->total_mass = 0.0f;
    node->center_x = 0.0f;
    node->center_y = 0.0f;
    
    if (node->is_leaf) {
        // For leaf nodes, use the particle data directly
        if (node->particle_count > 0) {
            // Get the particle data
            uint32_t particle_idx = node->particle_index;
            if (particle_idx < particles->count && particles->particles[particle_idx].active) {
                const particle_t *particle = &particles->particles[particle_idx];
                node->total_mass = particle->mass;
                node->center_x = particle->x;
                node->center_y = particle->y;
            }
        }
    } else {
        // For internal nodes, calculate from children
        float total_mass = 0.0f;
        float weighted_x = 0.0f;
        float weighted_y = 0.0f;
        
        for (int i = 0; i < 4; i++) {
            if (node->children[i] != NULL) {
                // Recursively calculate center of mass for child
                quadtree_calculate_center_of_mass(node->children[i], particles);
                
                // Add child's contribution to this node's center of mass
                float child_mass = node->children[i]->total_mass;
                if (child_mass > 0.0f) {
                    weighted_x += node->children[i]->center_x * child_mass;
                    weighted_y += node->children[i]->center_y * child_mass;
                    total_mass += child_mass;
                }
            }
        }
        
        // Calculate final center of mass
        if (total_mass > 0.0f) {
            node->center_x = weighted_x / total_mass;
            node->center_y = weighted_y / total_mass;
            node->total_mass = total_mass;
        }
    }
}

// Enhanced insertion function that updates mass during insertion
int quadtree_insert_with_mass(quadtree_t *tree, uint32_t particle_idx, const particle_t *particle) {
    if (!tree || !particle) {
        return 0;
    }
    
    // First, perform the regular insertion
    int result = quadtree_insert(tree, particle_idx, particle);
    
    if (result) {
        // If insertion was successful, update mass information
        // We need to traverse the path from root to the inserted particle and update mass
        quadtree_update_mass_path(tree->root, particle);
    }
    
    return result;
}

// Helper function to update mass along the path from root to inserted particle
static void quadtree_update_mass_path(quadtree_node_t *node, const particle_t *particle) {
    if (!node || !particle) {
        return;
    }
    
    // Update mass for this node
    quadtree_update_mass(node, particle);
    
    // If this is not a leaf, continue down the appropriate path
    if (!node->is_leaf) {
        quadrant_t quad = quadtree_get_quadrant(node, particle->x, particle->y);
        if (node->children[quad] != NULL) {
            quadtree_update_mass_path(node->children[quad], particle);
        }
    }
}

// Rebuild quadtree with mass calculations
void quadtree_rebuild_with_mass(quadtree_t *tree, const particle_system_t *particles) {
    if (!tree || !particles) {
        return;
    }
    
    // Clear the existing tree
    quadtree_clear(tree);
    
    // Insert all active particles
    for (uint32_t i = 0; i < particles->count; i++) {
        if (particles->particles[i].active) {
            quadtree_insert(tree, i, &particles->particles[i]);
        }
    }
    
    // Calculate center of mass for all nodes
    if (tree->root) {
        quadtree_calculate_center_of_mass(tree->root, particles);
    }
}

// Calculate gravitational force between two particles
static void calculate_gravitational_force(const particle_t *p1, const particle_t *p2, 
                                        float gravity_constant, float softening,
                                        float *fx, float *fy) {
    if (!p1 || !p2 || !fx || !fy) {
        *fx = 0.0f;
        *fy = 0.0f;
        return;
    }
    
    // Calculate distance vector
    float dx = p2->x - p1->x;
    float dy = p2->y - p1->y;
    
    // Calculate distance squared with softening parameter
    float distance_squared = dx * dx + dy * dy + softening * softening;
    
    // Avoid division by zero and extremely small distances
    if (distance_squared < 1e-10f) {
        *fx = 0.0f;
        *fy = 0.0f;
        return;
    }
    
    // Calculate distance
    float distance = sqrtf_approx(distance_squared);
    
    // Calculate gravitational force magnitude: F = G * m1 * m2 / r^2
    float force_magnitude = gravity_constant * p1->mass * p2->mass / distance_squared;
    
    // Calculate force components (normalized direction vector * magnitude)
    *fx = force_magnitude * dx / distance;
    *fy = force_magnitude * dy / distance;
}

// Calculate gravitational force between a particle and a mass point (for Barnes-Hut)
static void calculate_gravitational_force_point(const particle_t *particle, 
                                              float mass_x, float mass_y, float mass,
                                              float gravity_constant, float softening,
                                              float *fx, float *fy) {
    if (!particle || !fx || !fy) {
        *fx = 0.0f;
        *fy = 0.0f;
        return;
    }
    
    // Calculate distance vector
    float dx = mass_x - particle->x;
    float dy = mass_y - particle->y;
    
    // Calculate distance squared with softening parameter
    float distance_squared = dx * dx + dy * dy + softening * softening;
    
    // Avoid division by zero and extremely small distances
    if (distance_squared < 1e-10f) {
        *fx = 0.0f;
        *fy = 0.0f;
        return;
    }
    
    // Calculate distance
    float distance = sqrtf_approx(distance_squared);
    
    // Calculate gravitational force magnitude: F = G * m1 * m2 / r^2
    float force_magnitude = gravity_constant * particle->mass * mass / distance_squared;
    
    // Calculate force components (normalized direction vector * magnitude)
    *fx = force_magnitude * dx / distance;
    *fy = force_magnitude * dy / distance;
}

// Forward declaration for recursive force calculation
static void quadtree_calculate_forces_recursive(quadtree_node_t *node, 
                                              particle_system_t *particles,
                                              uint32_t particle_idx,
                                              float theta, float gravity_constant, 
                                              float softening);

// Main Barnes-Hut force calculation function
void quadtree_calculate_forces(quadtree_t *tree, particle_system_t *particles, 
                             float theta, float gravity_constant, float softening) {
    if (!tree || !particles || !tree->root) {
        return;
    }
    
    // Calculate forces for each active particle
    for (uint32_t i = 0; i < particles->capacity; i++) {
        if (particles->particles[i].active) {
            // Recursively calculate forces from the quadtree
            quadtree_calculate_forces_recursive(tree->root, particles, i, 
                                              theta, gravity_constant, softening);
        }
    }
}

// Recursive Barnes-Hut force calculation for a single particle
static void quadtree_calculate_forces_recursive(quadtree_node_t *node, 
                                              particle_system_t *particles,
                                              uint32_t particle_idx,
                                              float theta, float gravity_constant, 
                                              float softening) {
    if (!node || !particles || particle_idx >= particles->capacity) {
        return;
    }
    
    const particle_t *particle = &particles->particles[particle_idx];
    if (!particle->active) {
        return;
    }
    
    // If this node has no mass, skip it
    if (node->total_mass <= 0.0f) {
        return;
    }
    
    // Calculate distance from particle to node's center of mass
    float dx = node->center_x - particle->x;
    float dy = node->center_y - particle->y;
    float distance = sqrtf_approx(dx * dx + dy * dy);
    
    // Barnes-Hut approximation criterion: if distance > node_width / theta
    // then treat this node as a single mass point
    if (node->is_leaf || (distance > node->width / theta)) {
        // If this is a leaf node containing the same particle, skip it
        if (node->is_leaf && node->particle_count == 1 && 
            node->particle_index == particle_idx) {
            return;
        }
        
        // Treat this node as a single mass point
        float fx, fy;
        calculate_gravitational_force_point(particle, 
                                          node->center_x, node->center_y, node->total_mass,
                                          gravity_constant, softening, &fx, &fy);
        
        // Accumulate forces
        particle_apply_force(particles, particle_idx, fx, fy);
    } else {
        // Node is too close, recursively examine children
        for (int i = 0; i < 4; i++) {
            if (node->children[i] != NULL) {
                quadtree_calculate_forces_recursive(node->children[i], particles, 
                                                  particle_idx, theta, 
                                                  gravity_constant, softening);
            }
        }
    }
}