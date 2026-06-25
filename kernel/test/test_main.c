// kernel/test/test_main.c
#include <kernel/test/ktest.h>
#include <kernel/stdio/stdio.h>

// External test suite functions
extern void test_string_suite(void);
extern void test_mem_suite(void);
extern void test_stdlib_suite(void);
extern void test_vfs_suite(void);
extern void test_paging_suite(void);
extern void test_scheduler_suite(void);
extern void test_io_suite(void);

/**
 * @brief Run all kernel test suites.
 * 
 * This function runs all available kernel test suites in sequence
 * and prints a comprehensive summary at the end.
 */
void ktest_run_all_suites(void) {
    printf("\n");
    printf("================================================================================\n");
    printf("                     SEROTONIN KERNEL TEST SUITE\n");
    printf("================================================================================\n");
    printf("\n");
    printf("Starting comprehensive kernel testing...\n");
    printf("\n");
    
    // Run all test suites
    test_string_suite();
    test_stdlib_suite();
    test_mem_suite();
    test_paging_suite();
    test_io_suite();
    test_scheduler_suite();
    test_vfs_suite();
    
    // Print final summary
    ktest_print_summary();
    
    // Get results for return value
    ktest_results_t results = ktest_get_global_results();
    
    if (results.failed == 0) {
        printf("All kernel tests completed successfully!\n");
    } else {
        printf("Some tests failed. Please review the output above.\n");
    }
    
    printf("\n");
}

/**
 * @brief Entry point for kernel test mode.
 * 
 * This function should be called from kernel_main when the system
 * boots in test mode.
 */
void kernel_test_main(void) {
    printf("\n");
    printf("*** KERNEL TEST MODE ACTIVATED ***\n");
    printf("\n");
    
    ktest_run_all_suites();
    
    ktest_results_t results = ktest_get_global_results();
    
    if (results.failed == 0) {
        printf("\n[SUCCESS] All tests passed. System will halt.\n");
    } else {
        printf("\n[FAILURE] %u tests failed. System will halt.\n", results.failed);
    }
    
    printf("\nHalting system...\n");
    
    // Halt the system
    while (1) {
        asm volatile("hlt");
    }
}
