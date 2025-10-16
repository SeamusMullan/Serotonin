// kernel/test/test_mem.c
#include "ktest.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../stdio/stdio.h"

extern void* kernel_malloc(uint32_t size);
extern void kernel_free(void* ptr);
extern void* kernel_realloc(void* ptr, uint32_t size);

// Test basic allocation
KTEST_DEFINE(malloc_basic) {
    void *p1 = kernel_malloc(100);
    KTEST_ASSERT_NOT_NULL(p1, "kernel_malloc returns non-NULL");
    
    void *p2 = kernel_malloc(200);
    KTEST_ASSERT_NOT_NULL(p2, "second kernel_malloc succeeds");
    KTEST_ASSERT_NEQ(p1, p2, "allocations don't overlap");
    
    kernel_free(p1);
    kernel_free(p2);
    return 1;
}

// Test multiple allocations
KTEST_DEFINE(malloc_multiple) {
    void *ptrs[10];
    
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kernel_malloc((i + 1) * 10);
        KTEST_ASSERT_NOT_NULL(ptrs[i], "allocation succeeds");
    }
    
    // Check all pointers are unique
    for (int i = 0; i < 10; i++) {
        for (int j = i + 1; j < 10; j++) {
            KTEST_ASSERT_NEQ(ptrs[i], ptrs[j], "allocations are unique");
        }
    }
    
    for (int i = 0; i < 10; i++) {
        kernel_free(ptrs[i]);
    }
    
    return 1;
}

// Test zero allocation
KTEST_DEFINE(malloc_zero) {
    void *p = kernel_malloc(0);
    // Implementation dependent - some allow, some don't
    // Just make sure it doesn't crash
    if (p != NULL) {
        kernel_free(p);
    }
    return 1;
}

// Test large allocation
KTEST_DEFINE(malloc_large) {
    void *p = kernel_malloc(8192);
    KTEST_ASSERT_NOT_NULL(p, "large allocation succeeds");
    
    // Write to it to make sure it's valid
    char *data = (char *)p;
    for (int i = 0; i < 8192; i++) {
        data[i] = (char)(i % 256);
    }
    
    // Verify
    for (int i = 0; i < 8192; i++) {
        KTEST_ASSERT_EQ(data[i], (char)(i % 256), "data integrity");
    }
    
    kernel_free(p);
    return 1;
}

// Test reallocation - SKIPPED: kernel_realloc not implemented
KTEST_DEFINE(realloc_basic) {
    KTEST_SKIP("kernel_realloc not implemented");
    return 1;
}

// Test memset
KTEST_DEFINE(memset_basic) {
    char buffer[128];
    
    memset(buffer, 0, 128);
    for (int i = 0; i < 128; i++) {
        KTEST_ASSERT_EQ(buffer[i], 0, "memset to zero");
    }
    
    memset(buffer, 0xAA, 128);
    for (int i = 0; i < 128; i++) {
        KTEST_ASSERT_EQ((unsigned char)buffer[i], 0xAA, "memset to pattern");
    }
    
    return 1;
}

// Test memcpy
KTEST_DEFINE(memcpy_basic) {
    char src[64] = "Hello, World! This is a test.";
    char dest[64];
    
    memcpy(dest, src, 64);
    KTEST_ASSERT_MEM_EQ(dest, src, 64, "memcpy copies correctly");
    
    return 1;
}

// Test memcpy with overlap protection (should use memmove)
KTEST_DEFINE(memmove_basic) {
    char buffer[64] = "Hello, World!";
    
    // Move within same buffer (overlapping)
    memmove(buffer + 2, buffer, 13);
    KTEST_ASSERT_STR_EQ(buffer + 2, "Hello, World!", "memmove handles overlap");
    
    return 1;
}

// Test memcmp
KTEST_DEFINE(memcmp_basic) {
    char buf1[32] = "Hello";
    char buf2[32] = "Hello";
    char buf3[32] = "World";
    
    KTEST_ASSERT_EQ(memcmp(buf1, buf2, 5), 0, "memcmp equal buffers");
    KTEST_ASSERT(memcmp(buf1, buf3, 5) != 0, "memcmp different buffers");
    
    return 1;
}

// Test allocation and free pattern
KTEST_DEFINE(alloc_free_pattern) {
    void *ptrs[5];
    
    // Allocate
    for (int i = 0; i < 5; i++) {
        ptrs[i] = kernel_malloc(100);
        KTEST_ASSERT_NOT_NULL(ptrs[i], "allocation succeeds");
    }
    
    // Free every other one
    kernel_free(ptrs[1]);
    kernel_free(ptrs[3]);
    
    // Allocate again (should potentially reuse freed blocks)
    void *p1 = kernel_malloc(50);
    void *p2 = kernel_malloc(50);
    KTEST_ASSERT_NOT_NULL(p1, "allocation after free succeeds");
    KTEST_ASSERT_NOT_NULL(p2, "second allocation after free succeeds");
    
    // Cleanup
    kernel_free(ptrs[0]);
    kernel_free(ptrs[2]);
    kernel_free(ptrs[4]);
    kernel_free(p1);
    kernel_free(p2);
    
    return 1;
}

// Run all memory tests
void test_mem_suite(void) {
    ktest_t tests[] = {
        KTEST_RUN(malloc_basic),
        KTEST_RUN(malloc_multiple),
        KTEST_RUN(malloc_zero),
        KTEST_RUN(malloc_large),
        KTEST_RUN(realloc_basic),
        KTEST_RUN(memset_basic),
        KTEST_RUN(memcpy_basic),
        KTEST_RUN(memmove_basic),
        KTEST_RUN(memcmp_basic),
        KTEST_RUN(alloc_free_pattern),
    };
    
    ktest_run_suite("Memory Management", tests, sizeof(tests) / sizeof(tests[0]));
}
