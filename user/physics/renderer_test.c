/*
    renderer_test.c
    Basic integration test for the physics renderer
*/

#include "renderer.h"
#include "particle.h"
#include "quadtree.h"
#include "../kernel.h"
#include <stdint.h>

/**
 * @brief Test basic renderer functionality
 * 
 * @return int 0 on success, non-zero on failure
 */
int test_renderer_basic() {
    // Create a mock framebuffer
    uint32_t *mock_framebuffer = (uint32_t*)kernel_malloc(1280 * 800 * sizeof(uint32_t));
    if (!mock_framebuffer) {
        return 1;
    }
    
    // Create renderer
    physics_renderer_t *renderer = physics_renderer_create(mock_framebuffer, 1280, 800, 1280 * 4);
    if (!renderer) {
        kernel_free(mock_framebuffer);
        return 2;
    }
    
    // Test basic functionality
    physics_renderer_clear_screen(renderer);
    
    // Test coordinate transformation
    int screen_x, screen_y;
    world_to_screen(renderer, 100.0f, 100.0f, &screen_x, &screen_y);
    
    float world_x, world_y;
    screen_to_world(renderer, screen_x, screen_y, &world_x, &world_y);
    
    // Basic validation
    if (world_x < 99.0f || world_x > 101.0f || world_y < 99.0f || world_y > 101.0f) {
        physics_renderer_destroy(renderer);
        kernel_free(mock_framebuffer);
        return 3;
    }
    
    // Cleanup
    physics_renderer_destroy(renderer);
    kernel_free(mock_framebuffer);
    
    return 0;
}

/**
 * @brief Test frame management functionality
 * 
 * @return int 0 on success, non-zero on failure
 */
int test_renderer_frame_management() {
    // Create a mock framebuffer
    uint32_t *mock_framebuffer = (uint32_t*)kernel_malloc(1280 * 800 * sizeof(uint32_t));
    if (!mock_framebuffer) {
        return 1;
    }
    
    // Create renderer
    physics_renderer_t *renderer = physics_renderer_create(mock_framebuffer, 1280, 800, 1280 * 4);
    if (!renderer) {
        kernel_free(mock_framebuffer);
        return 2;
    }
    
    // Test frame management
    render_stats_t stats = {0};
    
    physics_renderer_begin_frame(renderer, &stats);
    
    // Simulate some rendering work
    stats.particles_rendered = 100;
    stats.quadtree_nodes_rendered = 25;
    
    physics_renderer_end_frame(renderer, &stats);
    
    // Validate stats were updated
    if (stats.particles_rendered != 100 || stats.quadtree_nodes_rendered != 25) {
        physics_renderer_destroy(renderer);
        kernel_free(mock_framebuffer);
        return 3;
    }
    
    // Test frame rate limiting
    int should_limit = physics_renderer_should_limit_framerate(renderer, 60);
    // First call should not limit
    if (should_limit != 0) {
        physics_renderer_destroy(renderer);
        kernel_free(mock_framebuffer);
        return 4;
    }
    
    // Cleanup
    physics_renderer_destroy(renderer);
    kernel_free(mock_framebuffer);
    
    return 0;
}

/**
 * @brief Run all renderer tests
 * 
 * @return int 0 on success, non-zero on failure
 */
int run_renderer_tests() {
    int result;
    
    result = test_renderer_basic();
    if (result != 0) {
        return result;
    }
    
    result = test_renderer_frame_management();
    if (result != 0) {
        return result + 10; // Offset to distinguish test failures
    }
    
    return 0;
}