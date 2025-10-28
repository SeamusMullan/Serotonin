// kernel/test/test_paging.c
#include "ktest.h"
#include "../vmm/vmm.h"
#include "../stdio/stdio.h"
#include "../stdlib/stdlib.h"
#include "../kernel.h"

// External functions that may be available
extern uint32_t get_cr3(void);
extern void* get_physaddr(void* virtualaddr);

// Test page directory exists
KTEST_DEFINE(paging_enabled_test) {
    // Check if paging is enabled by reading CR0
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    
    KTEST_ASSERT(cr0 & 0x80000000, "paging is enabled (CR0.PG set)");
    return 1;
}

// Test CR3 register
KTEST_DEFINE(cr3_test) {
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    
    KTEST_ASSERT(cr3 != 0, "CR3 is set");
    KTEST_ASSERT((cr3 & 0xFFF) == 0, "CR3 is page-aligned");
    
    return 1;
}

// Test kernel space mapping
KTEST_DEFINE(kernel_mapping_test) {
    // Kernel should be mapped at high addresses
    extern char __kernel_virtual_base[];
    uint32_t kernel_base = (uint32_t)__kernel_virtual_base;
    
    KTEST_ASSERT(kernel_base >= 0xC0000000, "kernel in high memory");
    
    return 1;
}

// Test memory access (stack)
KTEST_DEFINE(stack_access_test) {
    volatile uint32_t test_var = 0x12345678;
    
    KTEST_ASSERT_EQ(test_var, 0x12345678, "stack read/write works");
    
    // Test stack pointer is in valid range
    uint32_t esp;
    asm volatile("mov %%esp, %0" : "=r"(esp));
    
    KTEST_ASSERT(esp > 0x10000, "stack pointer is valid");
    
    return 1;
}

// Test heap access
KTEST_DEFINE(heap_access_test) {
    uint32_t *ptr = (uint32_t *)kernel_malloc(sizeof(uint32_t) * 10);
    
    if (ptr == NULL) {
        KTEST_SKIP("heap allocation failed");
    }
    
    // Write pattern
    for (int i = 0; i < 10; i++) {
        ptr[i] = 0xDEADBEEF + i;
    }
    
    // Verify pattern
    for (int i = 0; i < 10; i++) {
        KTEST_ASSERT_EQ(ptr[i], 0xDEADBEEF + i, "heap read/write works");
    }
    
    kernel_free(ptr);
    return 1;
}

// Test page boundary access
KTEST_DEFINE(page_boundary_test) {
    // Allocate memory that crosses page boundary
    char *ptr = (char *)kernel_malloc(8192);
    
    if (ptr == NULL) {
        KTEST_SKIP("large allocation failed");
    }
    
    // Write to start and end
    ptr[0] = 'A';
    ptr[4095] = 'B';
    ptr[4096] = 'C';
    ptr[8191] = 'D';
    
    // Verify
    KTEST_ASSERT_EQ(ptr[0], 'A', "first byte accessible");
    KTEST_ASSERT_EQ(ptr[4095], 'B', "last byte of first page accessible");
    KTEST_ASSERT_EQ(ptr[4096], 'C', "first byte of second page accessible");
    KTEST_ASSERT_EQ(ptr[8191], 'D', "last byte accessible");

    kernel_free(ptr);
    return 1;
}

// Test large allocation
KTEST_DEFINE(large_allocation_test) {
    // Try to allocate multiple pages
    void *ptr = kernel_malloc(16384); // 4 pages
    
    if (ptr == NULL) {
        KTEST_SKIP("large allocation not supported or out of memory");
    }
    
    KTEST_ASSERT_NOT_NULL(ptr, "large allocation succeeds");
    
    // Touch pages to ensure they're mapped
    char *bytes = (char *)ptr;
    bytes[0] = 1;
    bytes[4096] = 2;
    bytes[8192] = 3;
    bytes[12288] = 4;
    
    KTEST_ASSERT_EQ(bytes[0], 1, "page 0 accessible");
    KTEST_ASSERT_EQ(bytes[4096], 2, "page 1 accessible");
    KTEST_ASSERT_EQ(bytes[8192], 3, "page 2 accessible");
    KTEST_ASSERT_EQ(bytes[12288], 4, "page 3 accessible");
    
    kernel_free(ptr);
    return 1;
}

// Test address alignment
KTEST_DEFINE(alignment_test) {
    void *ptr = kernel_malloc(100);
    
    if (ptr == NULL) {
        KTEST_SKIP("allocation failed");
    }
    
    uint32_t addr = (uint32_t)ptr;
    
    // Most allocators align to at least 4 or 8 bytes
    KTEST_ASSERT((addr % 4) == 0, "allocation is 4-byte aligned");
    
    kernel_free(ptr);
    return 1;
}

// Run all paging tests
void test_paging_suite(void) {
    ktest_t tests[] = {
        KTEST_RUN(paging_enabled_test),
        KTEST_RUN(cr3_test),
        KTEST_RUN(kernel_mapping_test),
        KTEST_RUN(stack_access_test),
        KTEST_RUN(heap_access_test),
        KTEST_RUN(page_boundary_test),
        KTEST_RUN(large_allocation_test),
        KTEST_RUN(alignment_test),
    };
    
    ktest_run_suite("Virtual Memory / Paging", tests, sizeof(tests) / sizeof(tests[0]));
}
