#include "physics.h"
#include "particle.h"
#include "quadtree.h"
#include "collision.h"
#include "../kernel.h"
#include <stddef.h>

// Test result structure
typedef struct {
    int passed;
    int failed;
    char last_error[256];
} test_results_t;

// Test helper macros
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            results->failed++; \
            snprintf(results->last_error, sizeof(results->last_error), "FAIL: %s", message); \
            return 0; \
        } else { \
            results->passed++; \
        } \
    } while(0)

#define TEST_ASSERT_FLOAT_EQUAL(a, b, tolerance, message) \
    do { \
        float diff = (a) - (b); \
        if (diff < 0) diff = -diff; \
        if (diff > (tolerance)) { \
            results->failed++; \
            snprintf(results->last_error, sizeof(results->last_error), "FAIL: %s (%.6f != %.6f)", message, (float)(a), (float)(b)); \
            return 0; \
        } else { \
            results->passed++; \
        } \
    } while(0)

// Forward declarations
static int test_simulation_creation_and_destruction(test_results_t *results);
static int test_simulation_step_integration(test_results_t *results);
static int test_parameter_validation(test_results_t *results);
static int test_collision_integration(test_results_t *results);
static int test_barnes_hut_integration(test_results_t *results);
static int test_boundary_conditions(test_results_t *results);
static int test_configuration_updates(test_results_t *results);
static int test_simulation_state_management(test_results_t *results);

// Simple string copy function (since we don't have standard library)
static void simple_strcpy(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

// Simple sprintf-like function for basic formatting
static void snprintf(char *buffer, int size, const char *format, ...) {
    // Simplified implementation - just copy the format string for now
    simple_strcpy(buffer, format, size);
}

int physics_run_integration_tests(void) {
    test_results_t results = {0, 0, {0}};
    
    // Run all integration tests
    test_simulation_creation_and_destruction(&results);
    test_simulation_step_integration(&results);
    test_parameter_validation(&results);
    test_collision_integration(&results);
    test_barnes_hut_integration(&results);
    test_boundary_conditions(&results);
    test_configuration_updates(&results);
    test_simulation_state_management(&results);
    
    // Return 1 if all tests passed, 0 if any failed
    return (results.failed == 0);
}

static int test_simulation_creation_and_destruction(test_results_t *results) {
    // Test 1: Create simulation with default config
    physics_config_t config = physics_config_default();
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(sim != NULL, "Simulation creation with default config");
    TEST_ASSERT(sim->initialized == 1, "Simulation initialized flag");
    TEST_ASSERT(sim->particles != NULL, "Particle system created");
    TEST_ASSERT(sim->quadtree != NULL, "Quadtree created");
    
    // Test 2: Verify configuration was applied
    TEST_ASSERT_FLOAT_EQUAL(sim->gravitational_constant, config.gravity_constant, 1e-10f, "Gravity constant set");
    TEST_ASSERT_FLOAT_EQUAL(sim->theta, config.theta, 1e-6f, "Theta parameter set");
    TEST_ASSERT_FLOAT_EQUAL(sim->restitution, config.restitution, 1e-6f, "Restitution set");
    
    // Test 3: Test collision system creation when enabled
    if (config.enable_collisions) {
        TEST_ASSERT(sim->collisions != NULL, "Collision system created when enabled");
    }
    
    // Test 4: Clean destruction
    physics_simulation_destroy(sim);
    // If we get here without crashing, destruction worked
    results->passed++;
    
    // Test 5: Create simulation with custom config
    config.max_particles = 50;
    config.world_width = 640.0f;
    config.world_height = 480.0f;
    config.enable_collisions = 0;
    
    sim = physics_simulation_create(&config);
    TEST_ASSERT(sim != NULL, "Simulation creation with custom config");
    TEST_ASSERT(sim->particles->capacity == 50, "Custom particle count");
    TEST_ASSERT(sim->collisions == NULL, "Collision system not created when disabled");
    
    physics_simulation_destroy(sim);
    results->passed++;
    
    return 1;
}

static int test_simulation_step_integration(test_results_t *results) {
    // Create a simple simulation
    physics_config_t config = physics_config_default();
    config.max_particles = 10;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.gravity_constant = 1e-6f;  // Small gravity for testing
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(sim != NULL, "Simulation created for step test");
    
    // Add a few test particles
    uint32_t p1 = particle_add(sim->particles, 25.0f, 25.0f, 1.0f, 2.0f);
    uint32_t p2 = particle_add(sim->particles, 75.0f, 75.0f, 1.0f, 2.0f);
    TEST_ASSERT(p1 != UINT32_MAX && p2 != UINT32_MAX, "Particles added successfully");
    
    // Record initial positions
    float initial_x1 = sim->particles->particles[p1].x;
    float initial_y1 = sim->particles->particles[p1].y;
    float initial_x2 = sim->particles->particles[p2].x;
    float initial_y2 = sim->particles->particles[p2].y;
    
    // Run a simulation step
    physics_simulation_step_complete(sim);
    
    // Verify particles moved (due to gravitational attraction)
    float new_x1 = sim->particles->particles[p1].x;
    float new_y1 = sim->particles->particles[p1].y;
    float new_x2 = sim->particles->particles[p2].x;
    float new_y2 = sim->particles->particles[p2].y;
    
    // Particles should have moved toward each other
    float dist_before = (initial_x2 - initial_x1) * (initial_x2 - initial_x1) + 
                       (initial_y2 - initial_y1) * (initial_y2 - initial_y1);
    float dist_after = (new_x2 - new_x1) * (new_x2 - new_x1) + 
                      (new_y2 - new_y1) * (new_y2 - new_y1);
    
    TEST_ASSERT(dist_after < dist_before, "Particles moved closer due to gravity");
    
    // Test multiple steps
    for (int i = 0; i < 10; i++) {
        physics_simulation_step_complete(sim);
    }
    
    // Simulation should still be stable
    TEST_ASSERT(physics_simulation_is_ready(sim), "Simulation stable after multiple steps");
    
    physics_simulation_destroy(sim);
    return 1;
}

static int test_parameter_validation(test_results_t *results) {
    physics_config_t config = physics_config_default();
    
    // Test invalid configurations
    config.max_particles = 0;
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(sim == NULL, "Reject zero particles");
    
    config = physics_config_default();
    config.theta = -1.0f;
    sim = physics_simulation_create(&config);
    TEST_ASSERT(sim == NULL, "Reject negative theta");
    
    config = physics_config_default();
    config.restitution = 2.0f;
    sim = physics_simulation_create(&config);
    TEST_ASSERT(sim == NULL, "Reject invalid restitution");
    
    config = physics_config_default();
    config.time_step = 0.0f;
    sim = physics_simulation_create(&config);
    TEST_ASSERT(sim == NULL, "Reject zero time step");
    
    // Test valid configuration
    config = physics_config_default();
    sim = physics_simulation_create(&config);
    TEST_ASSERT(sim != NULL, "Accept valid configuration");
    
    // Test parameter validation during runtime
    TEST_ASSERT(physics_simulation_validate_parameters(sim), "Valid parameters pass validation");
    
    // Test parameter updates
    physics_simulation_set_time_step(sim, 0.02f);
    TEST_ASSERT_FLOAT_EQUAL(sim->time_step, 0.02f, 1e-6f, "Time step updated");
    
    physics_simulation_set_frame_rate(sim, 30);
    TEST_ASSERT(sim->config.target_fps == 30, "Frame rate updated");
    
    physics_simulation_destroy(sim);
    return 1;
}

static int test_collision_integration(test_results_t *results) {
    // Create simulation with collisions enabled
    physics_config_t config = physics_config_default();
    config.max_particles = 5;
    config.world_width = 50.0f;
    config.world_height = 50.0f;
    config.enable_collisions = 1;
    config.restitution = 0.9f;
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(sim != NULL, "Collision simulation created");
    TEST_ASSERT(sim->collisions != NULL, "Collision system present");
    
    // Add two particles that will collide
    uint32_t p1 = particle_add(sim->particles, 20.0f, 25.0f, 1.0f, 3.0f);
    uint32_t p2 = particle_add(sim->particles, 30.0f, 25.0f, 1.0f, 3.0f);
    
    // Set velocities to make them collide
    sim->particles->particles[p1].vx = 5.0f;
    sim->particles->particles[p2].vx = -5.0f;
    
    // Run simulation until collision occurs
    int collision_detected = 0;
    for (int step = 0; step < 20 && !collision_detected; step++) {
        physics_simulation_step_complete(sim);
        if (sim->collisions->count > 0) {
            collision_detected = 1;
        }
    }
    
    TEST_ASSERT(collision_detected, "Collision was detected");
    
    // Verify collision response (velocities should have changed)
    float v1x_after = sim->particles->particles[p1].vx;
    float v2x_after = sim->particles->particles[p2].vx;
    
    // After elastic collision, velocities should be roughly reversed
    TEST_ASSERT(v1x_after < 0.0f, "Particle 1 velocity reversed");
    TEST_ASSERT(v2x_after > 0.0f, "Particle 2 velocity reversed");
    
    physics_simulation_destroy(sim);
    return 1;
}

static int test_barnes_hut_integration(test_results_t *results) {
    // Create simulation with multiple particles to test Barnes-Hut
    physics_config_t config = physics_config_default();
    config.max_particles = 20;
    config.world_width = 200.0f;
    config.world_height = 200.0f;
    config.gravity_constant = 1e-5f;
    config.theta = 0.5f;
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(sim != NULL, "Barnes-Hut simulation created");
    
    // Add particles in a cluster
    for (int i = 0; i < 10; i++) {
        float x = 90.0f + (i % 3) * 10.0f;
        float y = 90.0f + (i / 3) * 10.0f;
        particle_add(sim->particles, x, y, 1.0f, 1.0f);
    }
    
    // Add a distant massive particle
    particle_add(sim->particles, 150.0f, 150.0f, 10.0f, 2.0f);
    
    TEST_ASSERT(sim->particles->count == 11, "All particles added");
    
    // Run simulation steps
    for (int i = 0; i < 5; i++) {
        physics_simulation_step_complete(sim);
        TEST_ASSERT(sim->quadtree->pool_index > 0, "Quadtree nodes allocated");
    }
    
    // Verify particles are moving (forces are being calculated)
    int particles_moving = 0;
    for (uint32_t i = 0; i < sim->particles->count; i++) {
        particle_t *p = &sim->particles->particles[i];
        if (p->vx != 0.0f || p->vy != 0.0f) {
            particles_moving++;
        }
    }
    
    TEST_ASSERT(particles_moving > 0, "Particles are moving due to gravitational forces");
    
    physics_simulation_destroy(sim);
    return 1;
}

static int test_boundary_conditions(test_results_t *results) {
    physics_config_t config = physics_config_default();
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.restitution = 0.8f;
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(sim != NULL, "Boundary test simulation created");
    
    // Add particle near boundary with velocity toward boundary
    uint32_t p1 = particle_add(sim->particles, 95.0f, 50.0f, 1.0f, 2.0f);
    sim->particles->particles[p1].vx = 10.0f;  // Moving right toward boundary
    
    // Run simulation step
    physics_simulation_step_complete(sim);
    
    // Particle should have bounced off boundary
    TEST_ASSERT(sim->particles->particles[p1].x <= 100.0f, "Particle stayed within bounds");
    TEST_ASSERT(sim->particles->particles[p1].vx < 0.0f, "Particle velocity reversed at boundary");
    
    physics_simulation_destroy(sim);
    return 1;
}

static int test_configuration_updates(test_results_t *results) {
    physics_config_t config = physics_config_default();
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(sim != NULL, "Configuration update test simulation created");
    
    // Test configuration update
    physics_config_t new_config = config;
    new_config.gravity_constant = 1e-4f;
    new_config.theta = 1.0f;
    new_config.restitution = 0.5f;
    
    physics_simulation_update_config(sim, &new_config);
    
    TEST_ASSERT_FLOAT_EQUAL(sim->gravitational_constant, 1e-4f, 1e-10f, "Gravity constant updated");
    TEST_ASSERT_FLOAT_EQUAL(sim->theta, 1.0f, 1e-6f, "Theta parameter updated");
    TEST_ASSERT_FLOAT_EQUAL(sim->restitution, 0.5f, 1e-6f, "Restitution updated");
    
    physics_simulation_destroy(sim);
    return 1;
}

static int test_simulation_state_management(test_results_t *results) {
    physics_config_t config = physics_config_default();
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(sim != NULL, "State management test simulation created");
    
    // Add some particles
    particle_add(sim->particles, 50.0f, 50.0f, 1.0f, 2.0f);
    particle_add(sim->particles, 60.0f, 60.0f, 1.0f, 2.0f);
    
    TEST_ASSERT(sim->particles->count == 2, "Particles added");
    TEST_ASSERT(physics_simulation_is_ready(sim), "Simulation is ready");
    
    // Test pause/resume
    physics_simulation_pause(sim);
    TEST_ASSERT(sim->initialized == 0, "Simulation paused");
    TEST_ASSERT(!physics_simulation_is_ready(sim), "Simulation not ready when paused");
    
    physics_simulation_resume(sim);
    TEST_ASSERT(sim->initialized == 1, "Simulation resumed");
    TEST_ASSERT(physics_simulation_is_ready(sim), "Simulation ready after resume");
    
    // Test reset
    physics_simulation_reset(sim);
    TEST_ASSERT(sim->particles->count == 0, "Particles cleared after reset");
    
    physics_simulation_destroy(sim);
    return 1;
}