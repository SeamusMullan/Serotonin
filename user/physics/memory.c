#include "memory.h"
#include "particle.h"
#include "quadtree.h"
#include "collision.h"
#include "../kernel.h"
#include <stddef.h>

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFU
#endif

// Internal helper functions
static uint32_t find_next_free_block(memory_pool_t *pool, uint32_t start_hint);
static void update_pool_stats(memory_pool_t *pool);
static uint32_t calculate_fragmentation(const memory_pool_t *pool);
static void compact_pool_memory(memory_pool_t *pool);

// Memory pool implementation

memory_pool_t* memory_pool_create(uint32_t block_size, uint32_t block_count) {
    if (block_size == 0 || block_count == 0) {
        return NULL;
    }
    
    memory_pool_t *pool = (memory_pool_t*)kernel_malloc(sizeof(memory_pool_t));
    if (!pool) {
        return NULL;
    }
    
    // Align block size to 8-byte boundary for better performance
    block_size = (block_size + 7) & ~7;
    
    // Allocate the main memory pool
    uint32_t total_size = block_size * block_count;
    pool->pool_start = kernel_malloc(total_size);
    if (!pool->pool_start) {
        kernel_free(pool);
        return NULL;
    }
    
    // Allocate free list bitmap (1 bit per block)
    uint32_t bitmap_size = (block_count + 7) / 8;
    pool->free_list = (uint8_t*)kernel_malloc(bitmap_size);
    if (!pool->free_list) {
        kernel_free(pool->pool_start);
        kernel_free(pool);
        return NULL;
    }
    
    // Initialize pool structure
    pool->pool_end = (uint8_t*)pool->pool_start + total_size;
    pool->block_size = block_size;
    pool->total_blocks = block_count;
    pool->used_blocks = 0;
    pool->peak_usage = 0;
    pool->next_free_hint = 0;
    pool->initialized = 1;
    
    // Initialize free list (all blocks free initially)
    for (uint32_t i = 0; i < bitmap_size; i++) {
        pool->free_list[i] = 0x00;
    }
    
    return pool;
}

void memory_pool_destroy(memory_pool_t *pool) {
    if (!pool) {
        return;
    }
    
    if (pool->pool_start) {
        kernel_free(pool->pool_start);
    }
    
    if (pool->free_list) {
        kernel_free(pool->free_list);
    }
    
    kernel_free(pool);
}

void* memory_pool_alloc(memory_pool_t *pool) {
    if (!pool || !pool->initialized || pool->used_blocks >= pool->total_blocks) {
        return NULL;
    }
    
    // Find next free block starting from hint
    uint32_t block_index = find_next_free_block(pool, pool->next_free_hint);
    if (block_index >= pool->total_blocks) {
        // No free blocks found
        return NULL;
    }
    
    // Mark block as used in bitmap
    uint32_t byte_index = block_index / 8;
    uint32_t bit_index = block_index % 8;
    pool->free_list[byte_index] |= (1 << bit_index);
    
    // Update pool statistics
    pool->used_blocks++;
    if (pool->used_blocks > pool->peak_usage) {
        pool->peak_usage = pool->used_blocks;
    }
    
    // Update hint for next allocation
    pool->next_free_hint = (block_index + 1) % pool->total_blocks;
    
    // Calculate and return block address
    uint8_t *block_addr = (uint8_t*)pool->pool_start + (block_index * pool->block_size);
    return (void*)block_addr;
}

void memory_pool_free(memory_pool_t *pool, void *ptr) {
    if (!pool || !pool->initialized || !ptr) {
        return;
    }
    
    // Check if pointer is within pool bounds
    if (ptr < pool->pool_start || ptr >= pool->pool_end) {
        return;
    }
    
    // Calculate block index
    uint32_t offset = (uint8_t*)ptr - (uint8_t*)pool->pool_start;
    uint32_t block_index = offset / pool->block_size;
    
    // Verify alignment
    if (offset % pool->block_size != 0) {
        return;
    }
    
    // Check if block is actually allocated
    uint32_t byte_index = block_index / 8;
    uint32_t bit_index = block_index % 8;
    if (!(pool->free_list[byte_index] & (1 << bit_index))) {
        // Block is already free
        return;
    }
    
    // Mark block as free
    pool->free_list[byte_index] &= ~(1 << bit_index);
    pool->used_blocks--;
    
    // Update hint if this block is before current hint
    if (block_index < pool->next_free_hint) {
        pool->next_free_hint = block_index;
    }
}

void memory_pool_clear(memory_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return;
    }
    
    // Clear all allocation bits
    uint32_t bitmap_size = (pool->total_blocks + 7) / 8;
    for (uint32_t i = 0; i < bitmap_size; i++) {
        pool->free_list[i] = 0x00;
    }
    
    // Reset statistics
    pool->used_blocks = 0;
    pool->next_free_hint = 0;
}

uint32_t memory_pool_get_usage(const memory_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return 0;
    }
    
    return (pool->used_blocks * 100) / pool->total_blocks;
}

uint32_t memory_pool_get_fragmentation(const memory_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return 0;
    }
    
    return calculate_fragmentation(pool);
}

void memory_pool_defragment(memory_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return;
    }
    
    compact_pool_memory(pool);
}

// Physics memory management implementation

physics_memory_t* physics_memory_create(uint32_t max_particles, uint32_t max_quadtree_nodes, uint32_t max_collisions) {
    physics_memory_t *memory = (physics_memory_t*)kernel_malloc(sizeof(physics_memory_t));
    if (!memory) {
        return NULL;
    }
    
    // Initialize memory structure
    memory->defrag_enabled = 1;
    memory->defrag_threshold = 75; // Defragment when 75% fragmented
    memory->monitoring_enabled = 1;
    
    // Initialize statistics
    memory->stats.total_allocated = 0;
    memory->stats.total_used = 0;
    memory->stats.peak_usage = 0;
    memory->stats.allocation_count = 0;
    memory->stats.deallocation_count = 0;
    memory->stats.fragmentation_level = 0;
    memory->stats.utilization_ratio = 0.0f;
    
    // Create particle pool
    memory_pool_t *particle_pool = memory_pool_create(sizeof(particle_t), max_particles);
    if (!particle_pool) {
        kernel_free(memory);
        return NULL;
    }
    memory->particle_pool = *particle_pool;
    kernel_free(particle_pool);
    
    // Create quadtree node pool
    memory_pool_t *quadtree_pool = memory_pool_create(sizeof(quadtree_node_t), max_quadtree_nodes);
    if (!quadtree_pool) {
        memory_pool_destroy(&memory->particle_pool);
        kernel_free(memory);
        return NULL;
    }
    memory->quadtree_pool = *quadtree_pool;
    kernel_free(quadtree_pool);
    
    // Create collision pool if needed
    if (max_collisions > 0) {
        memory_pool_t *collision_pool = memory_pool_create(sizeof(collision_t), max_collisions);
        if (!collision_pool) {
            memory_pool_destroy(&memory->particle_pool);
            memory_pool_destroy(&memory->quadtree_pool);
            kernel_free(memory);
            return NULL;
        }
        memory->collision_pool = *collision_pool;
        kernel_free(collision_pool);
    } else {
        // Initialize empty collision pool
        memory->collision_pool.pool_start = NULL;
        memory->collision_pool.initialized = 0;
    }
    
    // Update total allocated memory
    memory->stats.total_allocated = 
        (memory->particle_pool.total_blocks * memory->particle_pool.block_size) +
        (memory->quadtree_pool.total_blocks * memory->quadtree_pool.block_size) +
        (memory->collision_pool.initialized ? 
         (memory->collision_pool.total_blocks * memory->collision_pool.block_size) : 0);
    
    return memory;
}

void physics_memory_destroy(physics_memory_t *memory) {
    if (!memory) {
        return;
    }
    
    // Destroy all memory pools
    memory_pool_destroy(&memory->particle_pool);
    memory_pool_destroy(&memory->quadtree_pool);
    
    if (memory->collision_pool.initialized) {
        memory_pool_destroy(&memory->collision_pool);
    }
    
    kernel_free(memory);
}

void* physics_memory_alloc_particle(physics_memory_t *memory) {
    if (!memory) {
        return NULL;
    }
    
    void *ptr = memory_pool_alloc(&memory->particle_pool);
    if (ptr && memory->monitoring_enabled) {
        memory->stats.allocation_count++;
        physics_memory_update_stats(memory);
    }
    
    return ptr;
}

void* physics_memory_alloc_quadtree_node(physics_memory_t *memory) {
    if (!memory) {
        return NULL;
    }
    
    void *ptr = memory_pool_alloc(&memory->quadtree_pool);
    if (ptr && memory->monitoring_enabled) {
        memory->stats.allocation_count++;
        physics_memory_update_stats(memory);
    }
    
    return ptr;
}

void* physics_memory_alloc_collision(physics_memory_t *memory) {
    if (!memory || !memory->collision_pool.initialized) {
        return NULL;
    }
    
    void *ptr = memory_pool_alloc(&memory->collision_pool);
    if (ptr && memory->monitoring_enabled) {
        memory->stats.allocation_count++;
        physics_memory_update_stats(memory);
    }
    
    return ptr;
}

void physics_memory_free_particle(physics_memory_t *memory, void *ptr) {
    if (!memory || !ptr) {
        return;
    }
    
    memory_pool_free(&memory->particle_pool, ptr);
    if (memory->monitoring_enabled) {
        memory->stats.deallocation_count++;
        physics_memory_update_stats(memory);
    }
}

void physics_memory_free_quadtree_node(physics_memory_t *memory, void *ptr) {
    if (!memory || !ptr) {
        return;
    }
    
    memory_pool_free(&memory->quadtree_pool, ptr);
    if (memory->monitoring_enabled) {
        memory->stats.deallocation_count++;
        physics_memory_update_stats(memory);
    }
}

void physics_memory_free_collision(physics_memory_t *memory, void *ptr) {
    if (!memory || !ptr || !memory->collision_pool.initialized) {
        return;
    }
    
    memory_pool_free(&memory->collision_pool, ptr);
    if (memory->monitoring_enabled) {
        memory->stats.deallocation_count++;
        physics_memory_update_stats(memory);
    }
}

// Memory monitoring and reporting functions

void physics_memory_update_stats(physics_memory_t *memory) {
    if (!memory || !memory->monitoring_enabled) {
        return;
    }
    
    // Calculate total memory in use
    uint32_t particle_used = memory->particle_pool.used_blocks * memory->particle_pool.block_size;
    uint32_t quadtree_used = memory->quadtree_pool.used_blocks * memory->quadtree_pool.block_size;
    uint32_t collision_used = memory->collision_pool.initialized ? 
        (memory->collision_pool.used_blocks * memory->collision_pool.block_size) : 0;
    
    memory->stats.total_used = particle_used + quadtree_used + collision_used;
    
    // Update peak usage
    if (memory->stats.total_used > memory->stats.peak_usage) {
        memory->stats.peak_usage = memory->stats.total_used;
    }
    
    // Calculate utilization ratio
    if (memory->stats.total_allocated > 0) {
        memory->stats.utilization_ratio = (float)memory->stats.total_used / (float)memory->stats.total_allocated;
    }
    
    // Calculate average fragmentation
    uint32_t particle_frag = memory_pool_get_fragmentation(&memory->particle_pool);
    uint32_t quadtree_frag = memory_pool_get_fragmentation(&memory->quadtree_pool);
    uint32_t collision_frag = memory->collision_pool.initialized ? 
        memory_pool_get_fragmentation(&memory->collision_pool) : 0;
    
    memory->stats.fragmentation_level = (particle_frag + quadtree_frag + collision_frag) / 
        (memory->collision_pool.initialized ? 3 : 2);
    
    // Trigger defragmentation if needed
    if (memory->defrag_enabled && memory->stats.fragmentation_level > memory->defrag_threshold) {
        physics_memory_defragment_all(memory);
    }
}

void physics_memory_get_stats(const physics_memory_t *memory, memory_stats_t *stats) {
    if (!memory || !stats) {
        return;
    }
    
    *stats = memory->stats;
}

uint32_t physics_memory_get_total_usage(const physics_memory_t *memory) {
    if (!memory) {
        return 0;
    }
    
    return memory->stats.total_used;
}

float physics_memory_get_utilization(const physics_memory_t *memory) {
    if (!memory) {
        return 0.0f;
    }
    
    return memory->stats.utilization_ratio;
}

// Memory optimization functions

void physics_memory_defragment_all(physics_memory_t *memory) {
    if (!memory) {
        return;
    }
    
    memory_pool_defragment(&memory->particle_pool);
    memory_pool_defragment(&memory->quadtree_pool);
    
    if (memory->collision_pool.initialized) {
        memory_pool_defragment(&memory->collision_pool);
    }
    
    // Update statistics after defragmentation
    if (memory->monitoring_enabled) {
        physics_memory_update_stats(memory);
    }
}

void physics_memory_set_defrag_threshold(physics_memory_t *memory, uint32_t threshold) {
    if (!memory || threshold > 100) {
        return;
    }
    
    memory->defrag_threshold = threshold;
}

void physics_memory_enable_monitoring(physics_memory_t *memory, uint8_t enable) {
    if (!memory) {
        return;
    }
    
    memory->monitoring_enabled = enable;
}

int physics_memory_check_integrity(const physics_memory_t *memory) {
    if (!memory) {
        return 0;
    }
    
    // Check particle pool integrity
    if (!memory->particle_pool.initialized || 
        !memory->particle_pool.pool_start || 
        !memory->particle_pool.free_list) {
        return 0;
    }
    
    // Check quadtree pool integrity
    if (!memory->quadtree_pool.initialized || 
        !memory->quadtree_pool.pool_start || 
        !memory->quadtree_pool.free_list) {
        return 0;
    }
    
    // Check collision pool integrity if initialized
    if (memory->collision_pool.initialized) {
        if (!memory->collision_pool.pool_start || 
            !memory->collision_pool.free_list) {
            return 0;
        }
    }
    
    // Verify usage counts don't exceed capacity
    if (memory->particle_pool.used_blocks > memory->particle_pool.total_blocks ||
        memory->quadtree_pool.used_blocks > memory->quadtree_pool.total_blocks) {
        return 0;
    }
    
    if (memory->collision_pool.initialized && 
        memory->collision_pool.used_blocks > memory->collision_pool.total_blocks) {
        return 0;
    }
    
    return 1;
}

// Memory pool recycling functions

void memory_pool_recycle_unused(memory_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return;
    }
    
    // Reset hint to beginning for better allocation patterns
    pool->next_free_hint = 0;
    
    // Update statistics
    update_pool_stats(pool);
}

uint32_t memory_pool_compact(memory_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return 0;
    }
    
    // This is a simplified compaction - in a real implementation,
    // we would need to move allocated blocks to eliminate gaps
    uint32_t compacted_blocks = 0;
    
    // For now, just optimize the free list search
    pool->next_free_hint = 0;
    
    return compacted_blocks;
}

void memory_pool_reset_stats(memory_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return;
    }
    
    pool->peak_usage = pool->used_blocks;
}

// Internal helper function implementations

static uint32_t find_next_free_block(memory_pool_t *pool, uint32_t start_hint) {
    if (!pool || !pool->initialized) {
        return UINT32_MAX;
    }
    
    // Search from hint to end
    for (uint32_t i = start_hint; i < pool->total_blocks; i++) {
        uint32_t byte_index = i / 8;
        uint32_t bit_index = i % 8;
        
        if (!(pool->free_list[byte_index] & (1 << bit_index))) {
            return i;
        }
    }
    
    // Search from beginning to hint
    for (uint32_t i = 0; i < start_hint; i++) {
        uint32_t byte_index = i / 8;
        uint32_t bit_index = i % 8;
        
        if (!(pool->free_list[byte_index] & (1 << bit_index))) {
            return i;
        }
    }
    
    return UINT32_MAX; // No free blocks found
}

static void update_pool_stats(memory_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return;
    }
    
    // Update peak usage if current usage is higher
    if (pool->used_blocks > pool->peak_usage) {
        pool->peak_usage = pool->used_blocks;
    }
}

static uint32_t calculate_fragmentation(const memory_pool_t *pool) {
    if (!pool || !pool->initialized || pool->used_blocks == 0) {
        return 0;
    }
    
    // Count the number of free-used transitions (fragmentation indicator)
    uint32_t transitions = 0;
    uint8_t last_state = 0; // 0 = free, 1 = used
    
    for (uint32_t i = 0; i < pool->total_blocks; i++) {
        uint32_t byte_index = i / 8;
        uint32_t bit_index = i % 8;
        uint8_t current_state = (pool->free_list[byte_index] & (1 << bit_index)) ? 1 : 0;
        
        if (i > 0 && current_state != last_state) {
            transitions++;
        }
        
        last_state = current_state;
    }
    
    // Calculate fragmentation percentage based on transitions
    // More transitions = more fragmentation
    uint32_t max_transitions = pool->used_blocks * 2; // Worst case: alternating pattern
    if (max_transitions == 0) {
        return 0;
    }
    
    return (transitions * 100) / max_transitions;
}

static void compact_pool_memory(memory_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return;
    }
    
    // This is a simplified compaction that just resets the search hint
    // In a full implementation, we would need to move allocated blocks
    // to eliminate gaps, but that requires cooperation from the users
    // of the memory pool to update their pointers
    
    pool->next_free_hint = 0;
    
    // Find the first free block to optimize future allocations
    for (uint32_t i = 0; i < pool->total_blocks; i++) {
        uint32_t byte_index = i / 8;
        uint32_t bit_index = i % 8;
        
        if (!(pool->free_list[byte_index] & (1 << bit_index))) {
            pool->next_free_hint = i;
            break;
        }
    }
}