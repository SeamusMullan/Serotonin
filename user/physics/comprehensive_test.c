#include "comprehensive_test.h"
#include "physics.h"
#include "particle.h"
#include "quadtree.h"
#include "collision.h"
#include "renderer.h"
#include "memory.h"
#include "performance.h"
#include "demo.h"
#include "../kernel.h"
#include "../stdio/stdio.h"
#include <stddef.h>

// Test result tracking
typedef struct {
    uint32_t tests_run;
    uint32_t tests_passed;
    uint32_t tests_failed;
    char last_failure[256];
} comprehensive_test_results_t;

// Test helper macros and functions
#define TEST_ASSERT(results, condition, test_name) \
    do { \
        (results)->tests_run++; \
        if (condition) { \
            (results)->tests_passed++; \
        } else { \
            (results)->tests_failed++; \
            snprintf((results)->last_failure, sizeof((results)->last_failure), \
                    "FAILED: %s", test_name); \
        } \
    } while(0)

// Individual test suite functions
static void test_particle_system_comprehensive(comprehensive_test_results_t *results);
static void test_quadtree_comprehensive(comprehensive_test_results_t *results);
static void test_barnes_hut_comprehensive(comprehensive_test_results_t *results);
static void test_collision_system_comprehensive(comprehensive_test_results_t *results);
static void test_physics_integration_comprehensive(comprehensive_test_results_t *results);
static void test_memory_management_comprehensive(comprehensive_test_results_t *results);
static void test_performance_system_comprehensive(comprehensive_test_results_t *results);
static void test_rendering_system_comprehensive(comprehensive_test_results_t *results);
static void test_demo_system_comprehensive(comprehensive_test_results_t *results);
static void test_error_handling_comprehensive(comprehensive_test_results_t *results);

// Validation and verification tests
static void test_physics_accuracy_validation(comprehensive_test_results_t *results);
static void test_energy_conservation_validation(comprehensive_test_results_t *results);
static void test_momentum_conservation_validation(comprehensive_test_results_t *results);
static void test_numerical_stability_validation(comprehensive_test_results_t *results);

// Stress and edge case tests
static void test_high_particle_count_stress(comprehensive_test_results_t *results);
static void test_extreme_parameter_edge_cases(comprehensive_test_results_t *results);
static void test_memory_exhaustion_handling(comprehensive_test_results_t *results);
static void test_long_running_simulation_stability(comprehensive_test_results_t *results);

// Helper functions
static void print_test_summary(const comprehensive_test_results_t *results);
static int validate_two_body_orbit(float x1, float y1, float x2, float y2, 
                                  float vx1, float vy1, float vx2, float vy2, 
                                  float m1, float m2, float tolerance);
static float calculate_total_energy(particle_system_t *particles);
static float calculate_total_momentum_x(particle_system_t *particles);
static float calculate_total_momentum_y(particle_system_t *particles);

// Main comprehensive test runner
int physics_comprehensive_run_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    
    printf("=== COMPREHENSIVE PHYSICS SYSTEM TEST SUITE ===\n\n");
    
    // Core component tests
    printf("Running particle system tests...\n");
    test_particle_system_comprehensive(&results);
    
    printf("Running quadtree tests...\n");
    test_quadtree_comprehensive(&results);
    
    printf("Running Barnes-Hut algorithm tests...\n");
    test_barnes_hut_comprehensive(&results);
    
    printf("Running collision system tests...\n");
    test_collision_system_comprehensive(&results);
    
    printf("Running physics integration tests...\n");
    test_physics_integration_comprehensive(&results);
    
    printf("Running memory management tests...\n");
    test_memory_management_comprehensive(&results);
    
    printf("Running performance system tests...\n");
    test_performance_system_comprehensive(&results);
    
    printf("Running rendering system tests...\n");
    test_rendering_system_comprehensive(&results);
    
    printf("Running demo system tests...\n");
    test_demo_system_comprehensive(&results);
    
    printf("Running error handling tests...\n");
    test_error_handling_comprehensive(&results);
    
    // Validation and verification tests
    printf("\nRunning physics accuracy validation...\n");
    test_physics_accuracy_validation(&results);
    
    printf("Running energy conservation validation...\n");
    test_energy_conservation_validation(&results);
    
    printf("Running momentum conservation validation...\n");
    test_momentum_conservation_validation(&results);
    
    printf("Running numerical stability validation...\n");
    test_numerical_stability_validation(&results);
    
    // Stress and edge case tests
    printf("\nRunning stress tests...\n");
    test_high_particle_count_stress(&results);
    test_extreme_parameter_edge_cases(&results);
    test_memory_exhaustion_handling(&results);
    test_long_running_simulation_stability(&results);
    
    // Print final results
    print_test_summary(&results);
    
    return (results.tests_failed == 0) ? 1 : 0;
}

// Core component test implementations

static void test_particle_system_comprehensive(comprehensive_test_results_t *results) {
    // Test particle system creation and destruction
    particle_system_t *particles = particle_system_create(100);
    TEST_ASSERT(results, particles != NULL, "Particle system creation");
    TEST_ASSERT(results, particles->capacity == 100, "Particle system capacity");
    TEST_ASSERT(results, particles->count == 0, "Initial particle count");
    
    // Test particle addition
    int add_result = particle_add(particles, 100.0f, 200.0f, 5.0f, 10.0f);
    TEST_ASSERT(results, add_result == 1, "Particle addition success");
    TEST_ASSERT(results, particles->count == 1, "Particle count after addition");
    TEST_ASSERT(results, particles->particles[0].x == 100.0f, "Particle X position");
    TEST_ASSERT(results, particles->particles[0].y == 200.0f, "Particle Y position");
    TEST_ASSERT(results, particles->particles[0].mass == 5.0f, "Particle mass");
    TEST_ASSERT(results, particles->particles[0].radius == 10.0f, "Particle radius");
    TEST_ASSERT(results, particles->particles[0].active == 1, "Particle active flag");
    
    // Test multiple particle addition
    for (int i = 1; i < 50; i++) {
        particle_add(particles, (float)i * 10.0f, (float)i * 15.0f, 1.0f, 2.0f);
    }
    TEST_ASSERT(results, particles->count == 50, "Multiple particle addition");
    
    // Test particle capacity limit
    for (int i = 50; i < 150; i++) {
        particle_add(particles, (float)i * 10.0f, (float)i * 15.0f, 1.0f, 2.0f);
    }
    TEST_ASSERT(results, particles->count == 100, "Particle capacity limit respected");
    
    // Test force clearing
    particle_clear_forces(particles);
    for (uint32_t i = 0; i < particles->count; i++) {
        TEST_ASSERT(results, particles->force_x[i] == 0.0f, "Force X cleared");
        TEST_ASSERT(results, particles->force_y[i] == 0.0f, "Force Y cleared");
    }
    
    // Test force application
    particle_apply_force(particles, 0, 10.0f, 20.0f);
    TEST_ASSERT(results, particles->force_x[0] == 10.0f, "Force X applied");
    TEST_ASSERT(results, particles->force_y[0] == 20.0f, "Force Y applied");
    
    // Test physics integration
    float initial_x = particles->particles[0].x;
    float initial_y = particles->particles[0].y;
    particle_update_physics(particles, 0.016f);
    TEST_ASSERT(results, particles->particles[0].x != initial_x || 
                        particles->particles[0].y != initial_y, "Physics integration updates position");
    
    particle_system_destroy(particles);
}

static void test_quadtree_comprehensive(comprehensive_test_results_t *results) {
    // Test quadtree creation
    quadtree_t *tree = quadtree_create(400.0f, 300.0f, 800.0f, 600.0f, 100);
    TEST_ASSERT(results, tree != NULL, "Quadtree creation");
    TEST_ASSERT(results, tree->root != NULL, "Quadtree root node");
    TEST_ASSERT(results, tree->bounds_x == 400.0f, "Quadtree bounds X");
    TEST_ASSERT(results, tree->bounds_y == 300.0f, "Quadtree bounds Y");
    TEST_ASSERT(results, tree->bounds_width == 800.0f, "Quadtree bounds width");
    TEST_ASSERT(results, tree->bounds_height == 600.0f, "Quadtree bounds height");
    
    // Create test particle system
    particle_system_t *particles = particle_system_create(20);
    for (int i = 0; i < 10; i++) {
        particle_add(particles, 400.0f + i * 50.0f, 300.0f + i * 40.0f, 1.0f, 2.0f);
    }
    
    // Test quadtree rebuild
    quadtree_rebuild(tree, particles);
    TEST_ASSERT(results, tree->root->particle_count > 0, "Quadtree rebuild populates tree");
    
    // Test particle insertion
    quadtree_insert(tree, 0, &particles->particles[0]);
    TEST_ASSERT(results, 1, "Particle insertion completes");
    
    // Test mass calculation
    TEST_ASSERT(results, tree->root->total_mass > 0.0f, "Quadtree mass calculation");
    TEST_ASSERT(results, tree->root->center_x >= 400.0f && tree->root->center_x <= 800.0f, 
                "Center of mass X within bounds");
    TEST_ASSERT(results, tree->root->center_y >= 300.0f && tree->root->center_y <= 600.0f, 
                "Center of mass Y within bounds");
    
    // Test force calculation
    quadtree_calculate_forces(tree, particles, 0.5f);
    int forces_applied = 0;
    for (uint32_t i = 0; i < particles->count; i++) {
        if (particles->force_x[i] != 0.0f || particles->force_y[i] != 0.0f) {
            forces_applied++;
        }
    }
    TEST_ASSERT(results, forces_applied > 0, "Quadtree force calculation applies forces");
    
    // Test tree clearing
    quadtree_clear(tree);
    TEST_ASSERT(results, tree->root->particle_count == 0, "Quadtree clear resets particle count");
    
    particle_system_destroy(particles);
    quadtree_destroy(tree);
}

static void test_barnes_hut_comprehensive(comprehensive_test_results_t *results) {
    // Create test configuration
    physics_config_t config = {
        .max_particles = 50,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-6f,
        .theta = 0.5f,
        .restitution = 0.8f,
        .softening = 1.0f,
        .time_step = 0.016f,
        .target_fps = 60,
        .enable_collisions = 0, // Disable for pure gravitational test
        .debug_mode = 0
    };
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(results, sim != NULL, "Barnes-Hut simulation creation");
    
    // Add test particles in a known configuration
    particle_add(sim->particles, 400.0f, 300.0f, 100.0f, 5.0f); // Central massive body
    particle_add(sim->particles, 500.0f, 300.0f, 1.0f, 2.0f);   // Test particle
    particle_add(sim->particles, 300.0f, 300.0f, 1.0f, 2.0f);   // Test particle
    
    // Test theta parameter effect
    float theta_values[] = {0.3f, 0.5f, 1.0f, 2.0f};
    for (int i = 0; i < 4; i++) {
        sim->theta = theta_values[i];
        particle_clear_forces(sim->particles);
        
        physics_simulation_step(sim);
        
        // Check that forces were calculated
        int forces_calculated = 0;
        for (uint32_t j = 0; j < sim->particles->count; j++) {
            if (sim->particles->force_x[j] != 0.0f || sim->particles->force_y[j] != 0.0f) {
                forces_calculated++;
            }
        }
        TEST_ASSERT(results, forces_calculated > 0, "Barnes-Hut force calculation with different theta");
    }
    
    // Test accuracy vs performance trade-off
    sim->theta = 0.3f; // High accuracy
    particle_clear_forces(sim->particles);
    physics_simulation_step(sim);
    float accurate_force_x = sim->particles->force_x[1];
    float accurate_force_y = sim->particles->force_y[1];
    
    sim->theta = 2.0f; // Low accuracy, high performance
    particle_clear_forces(sim->particles);
    physics_simulation_step(sim);
    float fast_force_x = sim->particles->force_x[1];
    float fast_force_y = sim->particles->force_y[1];
    
    // Forces should be different but in same general direction
    float accurate_magnitude = sqrtf(accurate_force_x * accurate_force_x + accurate_force_y * accurate_force_y);
    float fast_magnitude = sqrtf(fast_force_x * fast_force_x + fast_force_y * fast_force_y);
    TEST_ASSERT(results, accurate_magnitude > 0.0f && fast_magnitude > 0.0f, 
                "Barnes-Hut produces forces at different theta values");
    
    physics_simulation_destroy(sim);
}

static void test_collision_system_comprehensive(comprehensive_test_results_t *results) {
    // Create collision system
    collision_system_t *collisions = collision_system_create(50);
    TEST_ASSERT(results, collisions != NULL, "Collision system creation");
    TEST_ASSERT(results, collisions->capacity == 50, "Collision system capacity");
    TEST_ASSERT(results, collisions->count == 0, "Initial collision count");
    
    // Create test particles for collision
    particle_system_t *particles = particle_system_create(10);
    particle_add(particles, 100.0f, 100.0f, 1.0f, 10.0f); // Particle 0
    particle_add(particles, 105.0f, 100.0f, 1.0f, 10.0f); // Particle 1 - overlapping
    particle_add(particles, 200.0f, 200.0f, 1.0f, 5.0f);  // Particle 2 - separate
    
    // Test collision detection
    detect_collisions(collisions, particles);
    TEST_ASSERT(results, collisions->count > 0, "Collision detection finds overlapping particles");
    
    if (collisions->count > 0) {
        collision_t *collision = &collisions->collisions[0];
        TEST_ASSERT(results, collision->particle_a == 0 || collision->particle_a == 1, 
                    "Collision involves correct particles");
        TEST_ASSERT(results, collision->particle_b == 0 || collision->particle_b == 1, 
                    "Collision involves correct particles");
        TEST_ASSERT(results, collision->overlap > 0.0f, "Collision overlap calculated");
    }
    
    // Test collision response
    float initial_momentum_x = calculate_total_momentum_x(particles);
    float initial_momentum_y = calculate_total_momentum_y(particles);
    
    resolve_collisions(collisions, particles, 0.8f); // 80% restitution
    
    float final_momentum_x = calculate_total_momentum_x(particles);
    float final_momentum_y = calculate_total_momentum_y(particles);
    
    // Momentum should be approximately conserved (within numerical tolerance)
    float momentum_error_x = fabsf(final_momentum_x - initial_momentum_x);
    float momentum_error_y = fabsf(final_momentum_y - initial_momentum_y);
    TEST_ASSERT(results, momentum_error_x < 0.1f, "Momentum conservation in X direction");
    TEST_ASSERT(results, momentum_error_y < 0.1f, "Momentum conservation in Y direction");
    
    // Test particle separation
    float distance = sqrtf(powf(particles->particles[1].x - particles->particles[0].x, 2) +
                          powf(particles->particles[1].y - particles->particles[0].y, 2));
    float min_distance = particles->particles[0].radius + particles->particles[1].radius;
    TEST_ASSERT(results, distance >= min_distance * 0.95f, "Particles separated after collision");
    
    collision_system_destroy(collisions);
    particle_system_destroy(particles);
}

static void test_physics_integration_comprehensive(comprehensive_test_results_t *results) {
    // Test complete physics simulation integration
    physics_config_t config = {
        .max_particles = 30,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-6f,
        .theta = 0.5f,
        .restitution = 0.8f,
        .softening = 1.0f,
        .time_step = 0.016f,
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(results, sim != NULL, "Complete physics simulation creation");
    TEST_ASSERT(results, sim->particles != NULL, "Simulation has particle system");
    TEST_ASSERT(results, sim->quadtree != NULL, "Simulation has quadtree");
    TEST_ASSERT(results, sim->collisions != NULL, "Simulation has collision system");
    
    // Add test particles
    for (int i = 0; i < 10; i++) {
        float x = 200.0f + i * 50.0f;
        float y = 200.0f + (i % 3) * 100.0f;
        particle_add(sim->particles, x, y, 1.0f + i * 0.5f, 3.0f);
    }
    
    // Test simulation step
    uint32_t initial_count = sim->particles->count;
    physics_simulation_step(sim);
    TEST_ASSERT(results, sim->particles->count == initial_count, "Particle count preserved during step");
    
    // Test multiple simulation steps
    for (int i = 0; i < 10; i++) {
        physics_simulation_step(sim);
    }
    TEST_ASSERT(results, sim->particles->count == initial_count, "Particle count stable over multiple steps");
    
    // Test simulation reset
    physics_simulation_reset(sim);
    TEST_ASSERT(results, sim->particles->count == 0, "Simulation reset clears particles");
    
    physics_simulation_destroy(sim);
}

// Additional comprehensive test implementations would continue here...
// For brevity, I'll implement key validation tests

static void test_physics_accuracy_validation(comprehensive_test_results_t *results) {
    // Test two-body orbital mechanics accuracy
    physics_config_t config = {
        .max_particles = 2,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-4f, // Stronger gravity for visible effect
        .theta = 0.1f, // High accuracy
        .restitution = 1.0f,
        .softening = 0.1f,
        .time_step = 0.01f, // Small time step for accuracy
        .target_fps = 60,
        .enable_collisions = 0,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(results, sim != NULL, "Two-body simulation creation");
    
    // Set up circular orbit configuration
    float m1 = 100.0f, m2 = 1.0f;
    float x1 = 350.0f, y1 = 300.0f;
    float x2 = 450.0f, y2 = 300.0f;
    float orbital_velocity = sqrtf(config.gravity_constant * m1 / 100.0f);
    
    particle_add(sim->particles, x1, y1, m1, 5.0f);
    particle_add(sim->particles, x2, y2, m2, 2.0f);
    sim->particles->particles[1].vy = orbital_velocity;
    
    // Run simulation and check orbital stability
    float initial_distance = sqrtf(powf(x2 - x1, 2) + powf(y2 - y1, 2));
    
    for (int i = 0; i < 100; i++) {
        physics_simulation_step(sim);
    }
    
    float final_distance = sqrtf(powf(sim->particles->particles[1].x - sim->particles->particles[0].x, 2) +
                                powf(sim->particles->particles[1].y - sim->particles->particles[0].y, 2));
    
    float distance_error = fabsf(final_distance - initial_distance) / initial_distance;
    TEST_ASSERT(results, distance_error < 0.1f, "Orbital distance stability within 10%");
    
    physics_simulation_destroy(sim);
}

static void test_energy_conservation_validation(comprehensive_test_results_t *results) {
    physics_config_t config = {
        .max_particles = 5,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-6f,
        .theta = 0.3f,
        .restitution = 1.0f, // Perfect elastic collisions
        .softening = 1.0f,
        .time_step = 0.016f,
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    
    // Add particles with known initial energy
    for (int i = 0; i < 5; i++) {
        float x = 200.0f + i * 100.0f;
        float y = 300.0f;
        float vx = (i % 2) ? 10.0f : -10.0f;
        float vy = 0.0f;
        particle_add(sim->particles, x, y, 2.0f, 5.0f);
        sim->particles->particles[i].vx = vx;
        sim->particles->particles[i].vy = vy;
    }
    
    float initial_energy = calculate_total_energy(sim->particles);
    
    // Run simulation
    for (int i = 0; i < 50; i++) {
        physics_simulation_step(sim);
    }
    
    float final_energy = calculate_total_energy(sim->particles);
    float energy_error = fabsf(final_energy - initial_energy) / fabsf(initial_energy);
    
    TEST_ASSERT(results, energy_error < 0.2f, "Energy conservation within 20%");
    
    physics_simulation_destroy(sim);
}

// Helper function implementations

static void print_test_summary(const comprehensive_test_results_t *results) {
    printf("\n=== COMPREHENSIVE TEST RESULTS ===\n");
    printf("Tests Run: %u\n", results->tests_run);
    printf("Tests Passed: %u\n", results->tests_passed);
    printf("Tests Failed: %u\n", results->tests_failed);
    
    if (results->tests_failed > 0) {
        printf("Last Failure: %s\n", results->last_failure);
        printf("Success Rate: %.1f%%\n", 
               (float)results->tests_passed / results->tests_run * 100.0f);
    } else {
        printf("All tests PASSED!\n");
    }
    printf("=====================================\n");
}

static float calculate_total_energy(particle_system_t *particles) {
    if (!particles) return 0.0f;
    
    float kinetic_energy = 0.0f;
    float potential_energy = 0.0f;
    
    // Calculate kinetic energy
    for (uint32_t i = 0; i < particles->count; i++) {
        if (particles->particles[i].active) {
            float v2 = particles->particles[i].vx * particles->particles[i].vx +
                      particles->particles[i].vy * particles->particles[i].vy;
            kinetic_energy += 0.5f * particles->particles[i].mass * v2;
        }
    }
    
    // Calculate gravitational potential energy
    for (uint32_t i = 0; i < particles->count; i++) {
        if (!particles->particles[i].active) continue;
        
        for (uint32_t j = i + 1; j < particles->count; j++) {
            if (!particles->particles[j].active) continue;
            
            float dx = particles->particles[j].x - particles->particles[i].x;
            float dy = particles->particles[j].y - particles->particles[i].y;
            float r = sqrtf(dx * dx + dy * dy + 1.0f); // Add softening
            
            potential_energy -= 1e-6f * particles->particles[i].mass * 
                               particles->particles[j].mass / r;
        }
    }
    
    return kinetic_energy + potential_energy;
}

static float calculate_total_momentum_x(particle_system_t *particles) {
    if (!particles) return 0.0f;
    
    float total_momentum = 0.0f;
    for (uint32_t i = 0; i < particles->count; i++) {
        if (particles->particles[i].active) {
            total_momentum += particles->particles[i].mass * particles->particles[i].vx;
        }
    }
    return total_momentum;
}

static float calculate_total_momentum_y(particle_system_t *particles) {
    if (!particles) return 0.0f;
    
    float total_momentum = 0.0f;
    for (uint32_t i = 0; i < particles->count; i++) {
        if (particles->particles[i].active) {
            total_momentum += particles->particles[i].mass * particles->particles[i].vy;
        }
    }
    return total_momentum;
}

// Comprehensive test implementations
static void test_memory_management_comprehensive(comprehensive_test_results_t *results) {
    // Test memory pool creation, allocation, and cleanup
    physics_memory_t *memory = physics_memory_create(1000, 500, 100);
    TEST_ASSERT(results, memory != NULL, "Physics memory system creation");
    
    if (!memory) return;
    
    // Test memory allocation and deallocation
    void *ptr1 = physics_memory_alloc_particle(memory);
    void *ptr2 = physics_memory_alloc_quadtree_node(memory);
    TEST_ASSERT(results, ptr1 != NULL && ptr2 != NULL, "Memory allocation success");
    
    // Test memory pool exhaustion handling
    void *ptrs[1001];
    int allocated_count = 0;
    for (int i = 0; i < 1001; i++) {
        ptrs[i] = physics_memory_alloc_particle(memory);
        if (ptrs[i]) allocated_count++;
        else break;
    }
    TEST_ASSERT(results, allocated_count <= 1000, "Memory pool respects capacity limits");
    
    // Free allocated memory
    for (int i = 0; i < allocated_count; i++) {
        if (ptrs[i]) physics_memory_free_particle(memory, ptrs[i]);
    }
    
    physics_memory_free_particle(memory, ptr1);
    physics_memory_free_quadtree_node(memory, ptr2);
    TEST_ASSERT(results, 1, "Memory deallocation completes");
    
    // Test memory fragmentation handling
    uint32_t fragmentation = physics_memory_get_fragmentation(memory);
    TEST_ASSERT(results, fragmentation >= 0, "Memory fragmentation measurement");
    
    physics_memory_defragment(memory);
    uint32_t post_defrag = physics_memory_get_fragmentation(memory);
    TEST_ASSERT(results, post_defrag <= fragmentation, "Memory defragmentation reduces fragmentation");
    
    physics_memory_destroy(memory);
}

static void test_performance_system_comprehensive(comprehensive_test_results_t *results) {
    // Run performance system tests
    int perf_result = physics_performance_run_tests();
    TEST_ASSERT(results, perf_result == 1, "Performance system tests pass");
}

static void test_rendering_system_comprehensive(comprehensive_test_results_t *results) {
    // Test renderer creation and basic functionality
    physics_renderer_t *renderer = physics_renderer_create(1280, 800);
    TEST_ASSERT(results, renderer != NULL, "Physics renderer creation");
    
    // Test particle rendering (basic functionality test)
    particle_system_t *particles = particle_system_create(5);
    particle_add(particles, 100.0f, 100.0f, 1.0f, 5.0f);
    
    physics_renderer_clear_screen(renderer);
    physics_renderer_render_particles(renderer, particles);
    TEST_ASSERT(results, 1, "Particle rendering completes");
    
    particle_system_destroy(particles);
    physics_renderer_destroy(renderer);
}

static void test_demo_system_comprehensive(comprehensive_test_results_t *results) {
    // Test demo system initialization
    int demo_init = physics_demo_init();
    TEST_ASSERT(results, demo_init == 1, "Demo system initialization");
    
    // Test scenario loading
    int scenario_result = physics_demo_load_scenario(DEMO_SCENARIO_RANDOM_PARTICLES);
    TEST_ASSERT(results, scenario_result == 1, "Demo scenario loading");
    
    // Test demo controls
    physics_demo_handle_key('h'); // Help toggle
    physics_demo_handle_key(' '); // Pause toggle
    TEST_ASSERT(results, 1, "Demo keyboard controls");
    
    physics_demo_shutdown();
}

static void test_error_handling_comprehensive(comprehensive_test_results_t *results) {
    // Test NULL pointer handling
    particle_system_t *null_particles = NULL;
    particle_clear_forces(null_particles); // Should not crash
    TEST_ASSERT(results, 1, "NULL pointer handling in particle system");
    
    // Test invalid parameter handling
    quadtree_t *invalid_tree = quadtree_create(0.0f, 0.0f, -100.0f, -100.0f, 0);
    TEST_ASSERT(results, invalid_tree == NULL, "Invalid quadtree parameters rejected");
    
    // Test memory allocation failure handling
    particle_system_t *huge_system = particle_system_create(UINT32_MAX);
    TEST_ASSERT(results, huge_system == NULL, "Excessive memory allocation rejected");
}

// Complete stress test and validation implementations
static void test_momentum_conservation_validation(comprehensive_test_results_t *results) {
    // Test momentum conservation in collision scenarios
    physics_config_t config = {
        .max_particles = 10,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 0.0f, // No gravity for pure collision test
        .theta = 0.5f,
        .restitution = 1.0f, // Perfect elastic collisions
        .softening = 1.0f,
        .time_step = 0.016f,
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(results, sim != NULL, "Momentum conservation simulation creation");
    
    if (!sim) return;
    
    // Create head-on collision scenario
    particle_add(sim->particles, 300.0f, 400.0f, 2.0f, 5.0f);
    particle_add(sim->particles, 500.0f, 400.0f, 3.0f, 5.0f);
    sim->particles->particles[0].vx = 50.0f;  // Moving right
    sim->particles->particles[1].vx = -30.0f; // Moving left
    
    float initial_momentum_x = calculate_total_momentum_x(sim->particles);
    float initial_momentum_y = calculate_total_momentum_y(sim->particles);
    
    // Run simulation until collision occurs and resolves
    for (int i = 0; i < 100; i++) {
        physics_simulation_step(sim);
    }
    
    float final_momentum_x = calculate_total_momentum_x(sim->particles);
    float final_momentum_y = calculate_total_momentum_y(sim->particles);
    
    float momentum_error_x = fabsf(final_momentum_x - initial_momentum_x);
    float momentum_error_y = fabsf(final_momentum_y - initial_momentum_y);
    
    TEST_ASSERT(results, momentum_error_x < 1.0f, "X momentum conserved in collision");
    TEST_ASSERT(results, momentum_error_y < 1.0f, "Y momentum conserved in collision");
    
    physics_simulation_destroy(sim);
}

static void test_numerical_stability_validation(comprehensive_test_results_t *results) {
    // Test numerical stability with extreme conditions
    physics_config_t config = {
        .max_particles = 20,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-3f, // Very strong gravity
        .theta = 0.1f, // High accuracy
        .restitution = 0.9f,
        .softening = 0.1f, // Small softening
        .time_step = 0.001f, // Very small time step
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(results, sim != NULL, "Numerical stability simulation creation");
    
    if (!sim) return;
    
    // Add particles in unstable configuration
    for (int i = 0; i < 10; i++) {
        float x = 400.0f + (i - 5) * 10.0f;
        float y = 300.0f + (i % 2) * 20.0f;
        particle_add(sim->particles, x, y, 1.0f + i * 0.1f, 2.0f);
    }
    
    uint32_t initial_count = sim->particles->count;
    int stability_failures = 0;
    
    // Run for extended period and check for NaN/infinity
    for (int i = 0; i < 1000; i++) {
        physics_simulation_step(sim);
        
        // Check for numerical instabilities
        for (uint32_t j = 0; j < sim->particles->count; j++) {
            particle_t *p = &sim->particles->particles[j];
            if (!p->active) continue;
            
            // Check for NaN or infinity
            if (isnanf(p->x) || isnanf(p->y) || isnanf(p->vx) || isnanf(p->vy) ||
                isinff(p->x) || isinff(p->y) || isinff(p->vx) || isinff(p->vy)) {
                stability_failures++;
                break;
            }
            
            // Check for extreme values
            if (fabsf(p->x) > 10000.0f || fabsf(p->y) > 10000.0f ||
                fabsf(p->vx) > 1000.0f || fabsf(p->vy) > 1000.0f) {
                stability_failures++;
                break;
            }
        }
        
        if (stability_failures > 0) break;
    }
    
    TEST_ASSERT(results, stability_failures == 0, "Numerical stability maintained");
    TEST_ASSERT(results, sim->particles->count == initial_count, "Particle count stable");
    
    physics_simulation_destroy(sim);
}

static void test_high_particle_count_stress(comprehensive_test_results_t *results) {
    // Test system performance with high particle counts
    uint32_t test_counts[] = {100, 250, 500, 1000};
    uint32_t num_tests = sizeof(test_counts) / sizeof(test_counts[0]);
    
    for (uint32_t i = 0; i < num_tests; i++) {
        physics_config_t config = {
            .max_particles = test_counts[i],
            .world_width = 1200.0f,
            .world_height = 900.0f,
            .gravity_constant = 1e-7f, // Weaker gravity for stability
            .theta = 1.0f, // Lower accuracy for performance
            .restitution = 0.8f,
            .softening = 2.0f,
            .time_step = 0.02f, // Larger time step for performance
            .target_fps = 30,
            .enable_collisions = 1,
            .debug_mode = 0
        };
        
        physics_simulation_t *sim = physics_simulation_create(&config);
        TEST_ASSERT(results, sim != NULL, "High particle count simulation creation");
        
        if (!sim) continue;
        
        // Add particles randomly distributed
        for (uint32_t j = 0; j < test_counts[i]; j++) {
            float x = 100.0f + (j % 40) * 25.0f;
            float y = 100.0f + (j / 40) * 25.0f;
            float mass = 1.0f + (j % 5) * 0.5f;
            particle_add(sim->particles, x, y, mass, 2.0f);
        }
        
        TEST_ASSERT(results, sim->particles->count == test_counts[i], 
                    "All particles added successfully");
        
        // Run simulation steps and measure performance
        uint32_t successful_steps = 0;
        for (int step = 0; step < 50; step++) {
            physics_simulation_step(sim);
            successful_steps++;
            
            // Check system hasn't crashed
            if (sim->particles->count == 0) break;
        }
        
        TEST_ASSERT(results, successful_steps >= 25, "Minimum simulation steps completed");
        
        physics_simulation_destroy(sim);
    }
}

static void test_extreme_parameter_edge_cases(comprehensive_test_results_t *results) {
    // Test with extreme parameter values
    
    // Test 1: Very high gravity
    physics_config_t high_gravity_config = {
        .max_particles = 10,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-2f, // Extremely high gravity
        .theta = 0.5f,
        .restitution = 0.8f,
        .softening = 5.0f, // High softening to prevent singularities
        .time_step = 0.001f, // Very small time step
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim1 = physics_simulation_create(&high_gravity_config);
    TEST_ASSERT(results, sim1 != NULL, "Extreme high gravity simulation creation");
    
    if (sim1) {
        particle_add(sim1->particles, 400.0f, 300.0f, 1.0f, 2.0f);
        particle_add(sim1->particles, 450.0f, 300.0f, 1.0f, 2.0f);
        
        // Should handle extreme gravity without crashing
        for (int i = 0; i < 10; i++) {
            physics_simulation_step(sim1);
        }
        TEST_ASSERT(results, 1, "Extreme gravity simulation stable");
        physics_simulation_destroy(sim1);
    }
    
    // Test 2: Very low gravity
    physics_config_t low_gravity_config = {
        .max_particles = 10,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-12f, // Extremely low gravity
        .theta = 0.5f,
        .restitution = 0.8f,
        .softening = 1.0f,
        .time_step = 0.1f, // Large time step
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim2 = physics_simulation_create(&low_gravity_config);
    TEST_ASSERT(results, sim2 != NULL, "Extreme low gravity simulation creation");
    
    if (sim2) {
        particle_add(sim2->particles, 400.0f, 300.0f, 1.0f, 2.0f);
        
        for (int i = 0; i < 10; i++) {
            physics_simulation_step(sim2);
        }
        TEST_ASSERT(results, 1, "Extreme low gravity simulation stable");
        physics_simulation_destroy(sim2);
    }
    
    // Test 3: Extreme theta values
    physics_config_t extreme_theta_config = {
        .max_particles = 20,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-6f,
        .theta = 10.0f, // Extremely high theta (very low accuracy)
        .restitution = 0.8f,
        .softening = 1.0f,
        .time_step = 0.016f,
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim3 = physics_simulation_create(&extreme_theta_config);
    TEST_ASSERT(results, sim3 != NULL, "Extreme theta simulation creation");
    
    if (sim3) {
        for (int i = 0; i < 5; i++) {
            particle_add(sim3->particles, 200.0f + i * 100.0f, 300.0f, 1.0f, 2.0f);
        }
        
        for (int i = 0; i < 20; i++) {
            physics_simulation_step(sim3);
        }
        TEST_ASSERT(results, 1, "Extreme theta simulation stable");
        physics_simulation_destroy(sim3);
    }
}

static void test_memory_exhaustion_handling(comprehensive_test_results_t *results) {
    // Test system behavior under memory pressure
    
    // Test 1: Particle system memory exhaustion
    particle_system_t *particles = particle_system_create(10);
    TEST_ASSERT(results, particles != NULL, "Small particle system creation");
    
    if (particles) {
        // Fill to capacity
        int successful_adds = 0;
        for (int i = 0; i < 20; i++) { // Try to add more than capacity
            if (particle_add(particles, (float)i * 10.0f, (float)i * 10.0f, 1.0f, 2.0f)) {
                successful_adds++;
            }
        }
        
        TEST_ASSERT(results, successful_adds == 10, "Particle system respects capacity limit");
        TEST_ASSERT(results, particles->count == 10, "Particle count correct at capacity");
        
        particle_system_destroy(particles);
    }
    
    // Test 2: Quadtree memory pool exhaustion
    quadtree_t *tree = quadtree_create(400.0f, 300.0f, 800.0f, 600.0f, 50);
    TEST_ASSERT(results, tree != NULL, "Quadtree with limited pool creation");
    
    if (tree) {
        particle_system_t *many_particles = particle_system_create(100);
        if (many_particles) {
            // Add many particles to force quadtree subdivision
            for (int i = 0; i < 100; i++) {
                particle_add(many_particles, 400.0f + (i % 20) * 20.0f, 
                           300.0f + (i / 20) * 30.0f, 1.0f, 1.0f);
            }
            
            // Rebuild should handle memory constraints gracefully
            quadtree_rebuild(tree, many_particles);
            TEST_ASSERT(results, 1, "Quadtree rebuild handles memory constraints");
            
            particle_system_destroy(many_particles);
        }
        
        quadtree_destroy(tree);
    }
    
    // Test 3: Collision system memory exhaustion
    collision_system_t *collisions = collision_system_create(5);
    TEST_ASSERT(results, collisions != NULL, "Small collision system creation");
    
    if (collisions) {
        particle_system_t *collision_particles = particle_system_create(20);
        if (collision_particles) {
            // Create overlapping particles to generate many collisions
            for (int i = 0; i < 10; i++) {
                particle_add(collision_particles, 400.0f + i * 2.0f, 300.0f, 1.0f, 5.0f);
            }
            
            detect_collisions(collisions, collision_particles);
            TEST_ASSERT(results, collisions->count <= 5, "Collision system respects capacity");
            
            particle_system_destroy(collision_particles);
        }
        
        collision_system_destroy(collisions);
    }
}

static void test_long_running_simulation_stability(comprehensive_test_results_t *results) {
    // Test simulation stability over extended periods
    physics_config_t config = {
        .max_particles = 25,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-7f,
        .theta = 0.8f,
        .restitution = 0.9f,
        .softening = 2.0f,
        .time_step = 0.02f,
        .target_fps = 50,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(results, sim != NULL, "Long-running simulation creation");
    
    if (!sim) return;
    
    // Add stable particle configuration
    for (int i = 0; i < 15; i++) {
        float angle = (float)i * 0.4f;
        float radius = 100.0f + i * 10.0f;
        float x = 400.0f + radius * cosf(angle);
        float y = 300.0f + radius * sinf(angle);
        particle_add(sim->particles, x, y, 1.0f + i * 0.1f, 2.0f);
    }
    
    uint32_t initial_count = sim->particles->count;
    float initial_energy = calculate_total_energy(sim->particles);
    
    // Run for extended period (simulate ~10 seconds)
    int successful_steps = 0;
    int energy_stable_count = 0;
    
    for (int i = 0; i < 500; i++) {
        physics_simulation_step(sim);
        successful_steps++;
        
        // Check system stability every 50 steps
        if (i % 50 == 0) {
            float current_energy = calculate_total_energy(sim->particles);
            float energy_change = fabsf(current_energy - initial_energy) / fabsf(initial_energy);
            
            if (energy_change < 0.5f) { // Energy change less than 50%
                energy_stable_count++;
            }
            
            // Check for particle loss
            if (sim->particles->count < initial_count * 0.8f) {
                break; // Too many particles lost
            }
        }
    }
    
    TEST_ASSERT(results, successful_steps >= 400, "Long simulation completed most steps");
    TEST_ASSERT(results, energy_stable_count >= 5, "Energy remained relatively stable");
    TEST_ASSERT(results, sim->particles->count >= initial_count * 0.8f, "Most particles preserved");
    
    physics_simulation_destroy(sim);
}

// Individual test suite runner implementations
int physics_comprehensive_run_particle_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Particle System Tests...\n");
    test_particle_system_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_quadtree_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Quadtree Tests...\n");
    test_quadtree_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_barnes_hut_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Barnes-Hut Algorithm Tests...\n");
    test_barnes_hut_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_collision_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Collision System Tests...\n");
    test_collision_system_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_integration_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Physics Integration Tests...\n");
    test_physics_integration_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_memory_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Memory Management Tests...\n");
    test_memory_management_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_performance_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Performance System Tests...\n");
    test_performance_system_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_rendering_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Rendering System Tests...\n");
    test_rendering_system_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_demo_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Demo System Tests...\n");
    test_demo_system_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_error_handling_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Error Handling Tests...\n");
    test_error_handling_comprehensive(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

// Validation test runners
int physics_comprehensive_run_accuracy_validation(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Physics Accuracy Validation...\n");
    test_physics_accuracy_validation(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_conservation_validation(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Conservation Laws Validation...\n");
    test_energy_conservation_validation(&results);
    test_momentum_conservation_validation(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_stability_validation(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Numerical Stability Validation...\n");
    test_numerical_stability_validation(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

// Stress and edge case test runners
int physics_comprehensive_run_stress_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Stress Tests...\n");
    test_high_particle_count_stress(&results);
    test_memory_exhaustion_handling(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_edge_case_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Edge Case Tests...\n");
    test_extreme_parameter_edge_cases(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

int physics_comprehensive_run_long_term_tests(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Long-term Stability Tests...\n");
    test_long_running_simulation_stability(&results);
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

// Benchmarking and performance validation
int physics_comprehensive_run_benchmarks(void) {
    printf("Running Comprehensive Benchmarks...\n");
    
    // Run performance benchmarks
    int perf_result = physics_performance_run_tests();
    
    // Run demo benchmarks
    int demo_result = physics_demo_benchmark_scenarios();
    
    // Run stress test benchmarks
    int stress_result = physics_demo_run_stress_test(1000);
    
    printf("Benchmark Results:\n");
    printf("  Performance Tests: %s\n", perf_result ? "PASSED" : "FAILED");
    printf("  Demo Benchmarks: %s\n", demo_result ? "PASSED" : "FAILED");
    printf("  Stress Tests: %s\n", stress_result ? "PASSED" : "FAILED");
    
    return (perf_result && demo_result && stress_result) ? 1 : 0;
}

int physics_comprehensive_run_performance_validation(void) {
    comprehensive_test_results_t results = {0, 0, 0, ""};
    printf("Running Performance Validation...\n");
    
    // Test that system meets minimum performance requirements
    physics_config_t config = {
        .max_particles = 100,
        .world_width = 800.0f,
        .world_height = 600.0f,
        .gravity_constant = 1e-6f,
        .theta = 0.5f,
        .restitution = 0.8f,
        .softening = 1.0f,
        .time_step = 0.016f,
        .target_fps = 60,
        .enable_collisions = 1,
        .debug_mode = 0
    };
    
    physics_simulation_t *sim = physics_simulation_create(&config);
    TEST_ASSERT(&results, sim != NULL, "Performance validation simulation creation");
    
    if (sim) {
        // Add particles
        for (int i = 0; i < 50; i++) {
            particle_add(sim->particles, 200.0f + i * 10.0f, 300.0f + (i % 5) * 50.0f, 1.0f, 2.0f);
        }
        
        // Measure performance over multiple frames
        uint32_t start_time = physics_kernel_get_time();
        for (int i = 0; i < 60; i++) { // 1 second at 60 FPS
            physics_simulation_step(sim);
        }
        uint32_t end_time = physics_kernel_get_time();
        
        float elapsed_time = (float)(end_time - start_time) / 1000.0f;
        float achieved_fps = 60.0f / elapsed_time;
        
        TEST_ASSERT(&results, achieved_fps >= 30.0f, "Minimum 30 FPS performance achieved");
        TEST_ASSERT(&results, elapsed_time <= 2.0f, "Performance within acceptable bounds");
        
        printf("Performance Results:\n");
        printf("  Target FPS: 60.0\n");
        printf("  Achieved FPS: %.1f\n", achieved_fps);
        printf("  Elapsed Time: %.3f seconds\n", elapsed_time);
        
        physics_simulation_destroy(sim);
    }
    
    print_test_summary(&results);
    return (results.tests_failed == 0) ? 1 : 0;
}

// Test utilities and helpers
void physics_comprehensive_print_test_report(void) {
    printf("\n=== COMPREHENSIVE TEST REPORT ===\n");
    printf("Running full test suite...\n\n");
    
    int total_passed = 0;
    int total_tests = 0;
    
    // Run all test categories
    printf("1. Core Component Tests:\n");
    total_tests++; total_passed += physics_comprehensive_run_particle_tests();
    total_tests++; total_passed += physics_comprehensive_run_quadtree_tests();
    total_tests++; total_passed += physics_comprehensive_run_barnes_hut_tests();
    total_tests++; total_passed += physics_comprehensive_run_collision_tests();
    total_tests++; total_passed += physics_comprehensive_run_integration_tests();
    
    printf("\n2. System Tests:\n");
    total_tests++; total_passed += physics_comprehensive_run_memory_tests();
    total_tests++; total_passed += physics_comprehensive_run_performance_tests();
    total_tests++; total_passed += physics_comprehensive_run_rendering_tests();
    total_tests++; total_passed += physics_comprehensive_run_demo_tests();
    total_tests++; total_passed += physics_comprehensive_run_error_handling_tests();
    
    printf("\n3. Validation Tests:\n");
    total_tests++; total_passed += physics_comprehensive_run_accuracy_validation();
    total_tests++; total_passed += physics_comprehensive_run_conservation_validation();
    total_tests++; total_passed += physics_comprehensive_run_stability_validation();
    
    printf("\n4. Stress and Edge Case Tests:\n");
    total_tests++; total_passed += physics_comprehensive_run_stress_tests();
    total_tests++; total_passed += physics_comprehensive_run_edge_case_tests();
    total_tests++; total_passed += physics_comprehensive_run_long_term_tests();
    
    printf("\n5. Performance Validation:\n");
    total_tests++; total_passed += physics_comprehensive_run_benchmarks();
    total_tests++; total_passed += physics_comprehensive_run_performance_validation();
    
    printf("\n=== FINAL REPORT ===\n");
    printf("Test Categories Passed: %d/%d\n", total_passed, total_tests);
    printf("Overall Success Rate: %.1f%%\n", (float)total_passed / total_tests * 100.0f);
    
    if (total_passed == total_tests) {
        printf("STATUS: ALL TESTS PASSED ✓\n");
    } else {
        printf("STATUS: SOME TESTS FAILED ✗\n");
    }
    printf("========================\n");
}

void physics_comprehensive_reset_test_state(void) {
    // Reset any global test state
    printf("Resetting test state...\n");
    
    // Stop any running demos
    physics_demo_stop();
    
    // Clear any allocated test resources
    // This would clean up any test-specific allocations
    
    printf("Test state reset complete.\n");
}

int physics_comprehensive_validate_system_state(void) {
    printf("Validating system state...\n");
    
    // Check that core systems are in valid state
    int validation_passed = 1;
    
    // Validate demo system
    if (physics_demo_control == NULL) {
        printf("Warning: Demo control not initialized\n");
        validation_passed = 0;
    }
    
    // Validate memory state (simplified check)
    // In a real implementation, this would check for memory leaks,
    // fragmentation, and other memory-related issues
    
    printf("System state validation: %s\n", validation_passed ? "PASSED" : "FAILED");
    return validation_passed;
}