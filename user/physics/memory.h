#ifndef PHYSICS_MEMORY_H
#define PHYSICS_MEMORY_H

#include <stdint.h>

// Forward declarations
typedef struct physics_memory physics_memory_t;
typedef struct memory_pool memory_pool_t;
typedef struct memory_stats memory_stats_t;

// Memory pool structure for efficient allocation
struct memory_pool {
    void *pool_start;           // Start of memory pool
    void *pool_end;             // End of memory pool
    uint32_t block_size;        // Size of each block
    uint32_t total_blocks;      // Total number of blocks
    uint32_t used_blocks;       // Number of blocks in use
    uint32_t peak_usage;        // Peak usage for monitoring
    uint8_t *free_list;         // Bitmap of free blocks
    uint32_t next_free_hint;    // Hint for next free block search
    uint8_t initialized;        // Pool initialization flag
};

// Memory statistics for monitoring
struct memory_stats {
    uint32_t total_allocated;   // Total memory allocated
    uint32_t total_used;        // Total memory in use
    uint32_t peak_usage;        // Peak memory usage
    uint32_t allocation_count;  // Number of allocations
    uint32_t deallocation_count; // Number of deallocations
    uint32_t fragmentation_level; // Fragmentation percentage
    float utilization_ratio;    // Memory utilization ratio
};

// Main physics memory management structure
struct physics_memory {
    memory_pool_t particle_pool;    // Pool for particles
    memory_pool_t quadtree_pool;    // Pool for quadtree nodes
    memory_pool_t collision_pool;   // Pool for collision data
    memory_stats_t stats;           // Memory usage statistics
    uint8_t defrag_enabled;         // Enable defragmentation
    uint32_t defrag_threshold;      // Fragmentation threshold for defrag
    uint8_t monitoring_enabled;     // Enable memory monitoring
};

// Memory pool functions
memory_pool_t* memory_pool_create(uint32_t block_size, uint32_t block_count);
void memory_pool_destroy(memory_pool_t *pool);
void* memory_pool_alloc(memory_pool_t *pool);
void memory_pool_free(memory_pool_t *pool, void *ptr);
void memory_pool_clear(memory_pool_t *pool);
uint32_t memory_pool_get_usage(const memory_pool_t *pool);
uint32_t memory_pool_get_fragmentation(const memory_pool_t *pool);
void memory_pool_defragment(memory_pool_t *pool);

// Physics memory management functions
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
void physics_memory_print_stats(const physics_memory_t *memory);
uint32_t physics_memory_get_total_usage(const physics_memory_t *memory);
float physics_memory_get_utilization(const physics_memory_t *memory);

// Memory optimization functions
void physics_memory_defragment_all(physics_memory_t *memory);
void physics_memory_set_defrag_threshold(physics_memory_t *memory, uint32_t threshold);
void physics_memory_enable_monitoring(physics_memory_t *memory, uint8_t enable);
int physics_memory_check_integrity(const physics_memory_t *memory);

// Memory pool recycling functions
void memory_pool_recycle_unused(memory_pool_t *pool);
uint32_t memory_pool_compact(memory_pool_t *pool);
void memory_pool_reset_stats(memory_pool_t *pool);

// Testing functions
int physics_memory_run_tests(void);

#endif // PHYSICS_MEMORY_H