#ifndef PHYSICS_COMPREHENSIVE_TEST_H
#define PHYSICS_COMPREHENSIVE_TEST_H

#include <stdint.h>

// Main comprehensive test runner
int physics_comprehensive_run_tests(void);

// Individual test suite runners
int physics_comprehensive_run_particle_tests(void);
int physics_comprehensive_run_quadtree_tests(void);
int physics_comprehensive_run_barnes_hut_tests(void);
int physics_comprehensive_run_collision_tests(void);
int physics_comprehensive_run_integration_tests(void);
int physics_comprehensive_run_memory_tests(void);
int physics_comprehensive_run_performance_tests(void);
int physics_comprehensive_run_rendering_tests(void);
int physics_comprehensive_run_demo_tests(void);
int physics_comprehensive_run_error_handling_tests(void);

// Validation and verification test runners
int physics_comprehensive_run_accuracy_validation(void);
int physics_comprehensive_run_conservation_validation(void);
int physics_comprehensive_run_stability_validation(void);

// Stress and edge case test runners
int physics_comprehensive_run_stress_tests(void);
int physics_comprehensive_run_edge_case_tests(void);
int physics_comprehensive_run_long_term_tests(void);

// Benchmarking and performance validation
int physics_comprehensive_run_benchmarks(void);
int physics_comprehensive_run_performance_validation(void);

// Test utilities and helpers
void physics_comprehensive_print_test_report(void);
void physics_comprehensive_reset_test_state(void);
int physics_comprehensive_validate_system_state(void);

#endif // PHYSICS_COMPREHENSIVE_TEST_H