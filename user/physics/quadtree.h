#ifndef QUADTREE_H
#define QUADTREE_H

#include <stdint.h>
#include "particle.h"

#define QUADTREE_MAX_PARTICLES_PER_NODE 1

// Forward declarations
typedef struct quadtree_node quadtree_node_t;
typedef struct quadtree quadtree_t;

// Quadtree node structure for spatial partitioning
struct quadtree_node {
    float x, y;                   // Center position of the node
    float width, height;          // Node dimensions
    float total_mass;             // Total mass in this subtree
    float center_x, center_y;     // Center of mass coordinates
    uint32_t particle_count;      // Number of particles in this subtree
    uint32_t particle_index;      // Particle index (for leaf nodes)
    struct quadtree_node *children[4]; // Child nodes: [NW, NE, SW, SE]
    uint8_t is_leaf;              // 1 if leaf node, 0 if internal node
};

// Quadtree management structure
struct quadtree {
    quadtree_node_t *root;        // Root node of the tree
    quadtree_node_t *node_pool;   // Pre-allocated node pool
    uint32_t pool_size;           // Size of the node pool
    uint32_t pool_index;          // Current pool allocation index
    float bounds_x, bounds_y;     // Tree boundary position
    float bounds_width, bounds_height; // Tree boundary dimensions
    uint32_t max_depth;           // Maximum tree depth
    uint32_t current_depth;       // Current maximum depth in use
};

// Quadrant enumeration for child node indexing
typedef enum {
    QUADRANT_NW = 0,              // Northwest
    QUADRANT_NE = 1,              // Northeast
    QUADRANT_SW = 2,              // Southwest
    QUADRANT_SE = 3               // Southeast
} quadrant_t;

// Function declarations
quadtree_t* quadtree_create(float x, float y, float width, float height, uint32_t max_nodes);
void quadtree_destroy(quadtree_t *tree);
void quadtree_clear(quadtree_t *tree);
int quadtree_insert(quadtree_t *tree, uint32_t particle_idx, const particle_t *particle);
int quadtree_insert_with_mass(quadtree_t *tree, uint32_t particle_idx, const particle_t *particle);
void quadtree_rebuild(quadtree_t *tree, const particle_system_t *particles);
void quadtree_rebuild_with_mass(quadtree_t *tree, const particle_system_t *particles);
void quadtree_calculate_forces(quadtree_t *tree, particle_system_t *particles, float theta, float gravity_constant, float softening);

// Node management functions
quadtree_node_t* quadtree_allocate_node(quadtree_t *tree);
void quadtree_subdivide(quadtree_t *tree, quadtree_node_t *node);
void quadtree_update_mass(quadtree_node_t *node, const particle_t *particle);
void quadtree_calculate_center_of_mass(quadtree_node_t *node, const particle_system_t *particles);

// Utility functions
quadrant_t quadtree_get_quadrant(const quadtree_node_t *node, float x, float y);
int quadtree_contains_point(const quadtree_node_t *node, float x, float y);
float quadtree_node_distance(const quadtree_node_t *node, float x, float y);

#endif // QUADTREE_H