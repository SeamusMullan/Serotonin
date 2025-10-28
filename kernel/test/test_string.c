// kernel/test/test_string.c
#include "ktest.h"
#include "../string.h"
#include "../stdlib/stdlib.h"
#include "../stdio/stdio.h"

// Test strlen
KTEST_DEFINE(strlen_basic) {
    char empty[8] = "";
    char single[8] = "a";
    char hello[16] = "hello";
    char hello_world[16] = "hello world";
    
    KTEST_ASSERT_EQ(strlen(empty), 0, "empty string length");
    KTEST_ASSERT_EQ(strlen(single), 1, "single char length");
    KTEST_ASSERT_EQ(strlen(hello), 5, "normal string length");
    KTEST_ASSERT_EQ(strlen(hello_world), 11, "string with space length");
    return 1;
}

// Test strcpy
KTEST_DEFINE(strcpy_basic) {
    char dest[64];
    
    strcpy(dest, "hello");
    KTEST_ASSERT_STR_EQ(dest, "hello", "strcpy simple");
    
    strcpy(dest, "");
    KTEST_ASSERT_STR_EQ(dest, "", "strcpy empty string");
    
    strcpy(dest, "a");
    KTEST_ASSERT_STR_EQ(dest, "a", "strcpy single char");
    
    return 1;
}

// Test strncpy
KTEST_DEFINE(strncpy_basic) {
    char dest[64] = {0};
    
    strncpy(dest, "hello", 3);
    KTEST_ASSERT_MEM_EQ(dest, "hel", 3, "strncpy partial");
    
    memset(dest, 0, 64);
    strncpy(dest, "hi", 10);
    KTEST_ASSERT_STR_EQ(dest, "hi", "strncpy with padding");
    
    return 1;
}

// Test strcmp
KTEST_DEFINE(strcmp_basic) {
    char empty1[8] = "", empty2[8] = "";
    char a1[8] = "a", a2[8] = "a", b[8] = "b";
    char hello1[16] = "hello", hello2[16] = "hello";
    
    KTEST_ASSERT_EQ(strcmp(empty1, empty2), 0, "strcmp empty strings");
    KTEST_ASSERT_EQ(strcmp(a1, a2), 0, "strcmp equal strings");
    KTEST_ASSERT_EQ(strcmp(hello1, hello2), 0, "strcmp equal strings");
    
    KTEST_ASSERT(strcmp(a1, b) < 0, "strcmp less than");
    KTEST_ASSERT(strcmp(b, a1) > 0, "strcmp greater than");
    
    return 1;
}

// Test strncmp
KTEST_DEFINE(strncmp_basic) {
    // Use stack-allocated strings to avoid potential issues with string literals
    char str1[16], str2[16], str3[16], str4[16];
    
    strcpy(str1, "hello");
    strcpy(str2, "hello");
    strcpy(str3, "world");
    strcpy(str4, "help");
    
    KTEST_ASSERT_EQ(strncmp(str1, str2, 5), 0, "strncmp equal");
    KTEST_ASSERT_EQ(strncmp(str1, str3, 0), 0, "strncmp zero length");
    KTEST_ASSERT_EQ(strncmp(str1, str4, 3), 0, "strncmp partial match");
    KTEST_ASSERT(strncmp(str1, str4, 4) != 0, "strncmp partial mismatch");
    
    return 1;
}

// Test strcat
KTEST_DEFINE(strcat_basic) {
    char dest[64];
    
    strcpy(dest, "hello");
    strcat(dest, " world");
    KTEST_ASSERT_STR_EQ(dest, "hello world", "strcat basic");
    
    strcpy(dest, "");
    strcat(dest, "test");
    KTEST_ASSERT_STR_EQ(dest, "test", "strcat to empty");
    
    strcpy(dest, "test");
    strcat(dest, "");
    KTEST_ASSERT_STR_EQ(dest, "test", "strcat empty string");
    
    return 1;
}

// Test strchr
KTEST_DEFINE(strchr_basic) {
    char str[32] = "hello world";
    char *result;
    
    result = strchr(str, 'h');
    KTEST_ASSERT_NOT_NULL(result, "strchr found at start");
    KTEST_ASSERT_EQ(*result, 'h', "strchr correct char");
    
    result = strchr(str, 'o');
    KTEST_ASSERT_NOT_NULL(result, "strchr found middle");
    
    result = strchr(str, 'x');
    KTEST_ASSERT_NULL(result, "strchr not found");
    
    result = strchr(str, '\0');
    KTEST_ASSERT_NOT_NULL(result, "strchr found null terminator");
    
    return 1;
}

// Test strstr
KTEST_DEFINE(strstr_basic) {
    const char *str = "hello world";
    char *result;
    
    result = strstr(str, "hello");
    KTEST_ASSERT_NOT_NULL(result, "strstr found at start");
    
    result = strstr(str, "world");
    KTEST_ASSERT_NOT_NULL(result, "strstr found at end");
    
    result = strstr(str, "lo wo");
    KTEST_ASSERT_NOT_NULL(result, "strstr found in middle");
    
    result = strstr(str, "xyz");
    KTEST_ASSERT_NULL(result, "strstr not found");
    
    result = strstr(str, "");
    KTEST_ASSERT_NOT_NULL(result, "strstr empty needle");
    
    return 1;
}

// Run all string tests
void test_string_suite(void) {
    ktest_t tests[] = {
        KTEST_RUN(strlen_basic),
        KTEST_RUN(strcpy_basic),
        // KTEST_RUN(strncpy_basic),
        // KTEST_RUN(strcmp_basic),
        // KTEST_RUN(strncmp_basic),
        KTEST_RUN(strcat_basic),
        KTEST_RUN(strchr_basic),
        KTEST_RUN(strstr_basic),
    };
    
    ktest_run_suite("String Functions", tests, sizeof(tests) / sizeof(tests[0]));
}
