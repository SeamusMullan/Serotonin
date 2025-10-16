// kernel/test/ktest.c
#include "ktest.h"
#include "../stdio/stdio.h"
#include "../stdlib/stdlib.h"

static ktest_results_t global_results = {0, 0, 0, 0};

/**
 * @brief Run a test suite and track results.
 * 
 * @param suite_name Name of the test suite
 * @param tests Array of test functions
 * @param count Number of tests in the suite
 */
void ktest_run_suite(const char *suite_name, ktest_t *tests, size_t count) {
    printf("\n=== Running Test Suite: %s ===\n", suite_name);
    
    ktest_results_t suite_results = {0, 0, 0, 0};
    
    for (size_t i = 0; i < count; i++) {
        printf("  Running: %s... ", tests[i].name);
        
        int result = tests[i].test_func();
        suite_results.total++;
        global_results.total++;
        
        if (result == 1) {
            printf("[PASS]\n");
            suite_results.passed++;
            global_results.passed++;
        } else if (result == -1) {
            // Test was skipped
            suite_results.skipped++;
            global_results.skipped++;
        } else {
            suite_results.failed++;
            global_results.failed++;
        }
    }
    
    // Print suite summary
    printf("\n--- Suite Summary ---\n");
    printf("  Total:   %u\n", suite_results.total);
    printf("  Passed:  %u\n", suite_results.passed);
    printf("  Failed:  %u\n", suite_results.failed);
    printf("  Skipped: %u\n", suite_results.skipped);
    
    if (suite_results.failed == 0) {
        printf("  Status:  [ALL PASSED]\n");
    } else {
        printf("  Status:  [FAILURES DETECTED]\n");
    }
}

/**
 * @brief Get the global test results.
 * 
 * @return ktest_results_t Global test results
 */
ktest_results_t ktest_get_global_results(void) {
    return global_results;
}

/**
 * @brief Print the global test summary.
 */
void ktest_print_summary(void) {
    printf("\n");
    printf("========================================\n");
    printf("         KERNEL TEST SUMMARY\n");
    printf("========================================\n");
    printf("  Total Tests:   %u\n", global_results.total);
    printf("  Passed:        %u\n", global_results.passed);
    printf("  Failed:        %u\n", global_results.failed);
    printf("  Skipped:       %u\n", global_results.skipped);
    printf("----------------------------------------\n");
    
    if (global_results.failed == 0) {
        printf("  Result: ALL TESTS PASSED ✓\n");
    } else {
        printf("  Result: %u TESTS FAILED ✗\n", global_results.failed);
    }
    
    printf("========================================\n\n");
}
