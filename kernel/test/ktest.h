// kernel/test/ktest.h
#ifndef KTEST_H
#define KTEST_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char *name;
    int (*test_func)(void);
} ktest_t;

typedef struct {
    uint32_t total;
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
} ktest_results_t;

// Core assertion macros
#define KTEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  [FAIL] %s at %s:%d\n", msg, __FILE__, __LINE__); \
            return 0; \
        } \
    } while(0)

#define KTEST_ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            printf("  [FAIL] %s: expected %d, got %d at %s:%d\n", \
                   msg, (int)(b), (int)(a), __FILE__, __LINE__); \
            return 0; \
        } \
    } while(0)

#define KTEST_ASSERT_NEQ(a, b, msg) \
    do { \
        if ((a) == (b)) { \
            printf("  [FAIL] %s: values should not be equal at %s:%d\n", \
                   msg, __FILE__, __LINE__); \
            return 0; \
        } \
    } while(0)

#define KTEST_ASSERT_NULL(ptr, msg) \
    KTEST_ASSERT((ptr) == NULL, msg)

#define KTEST_ASSERT_NOT_NULL(ptr, msg) \
    KTEST_ASSERT((ptr) != NULL, msg)

#define KTEST_ASSERT_STR_EQ(s1, s2, msg) \
    do { \
        if (strcmp(s1, s2) != 0) { \
            printf("  [FAIL] %s: expected '%s', got '%s' at %s:%d\n", \
                   msg, s2, s1, __FILE__, __LINE__); \
            return 0; \
        } \
    } while(0)

#define KTEST_ASSERT_MEM_EQ(m1, m2, size, msg) \
    do { \
        if (memcmp(m1, m2, size) != 0) { \
            printf("  [FAIL] %s: memory regions differ at %s:%d\n", \
                   msg, __FILE__, __LINE__); \
            return 0; \
        } \
    } while(0)

// Test definition macros
#define KTEST_DEFINE(name) \
    static int test_##name(void)

#define KTEST_RUN(name) \
    { #name, test_##name }

// Skip test macro
#define KTEST_SKIP(msg) \
    do { \
        printf("  [SKIP] %s\n", msg); \
        return -1; \
    } while(0)

// Test suite functions
void ktest_run_suite(const char *suite_name, ktest_t *tests, size_t count);
void ktest_run_all_suites(void);
ktest_results_t ktest_get_global_results(void);
void ktest_print_summary(void);

#endif