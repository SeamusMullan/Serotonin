// kernel/test/test_stdlib.c
#include "ktest.h"
#include "../stdlib/stdlib.h"
#include "../stdio/stdio.h"

// Test itoa
KTEST_DEFINE(itoa_basic) {
    char buffer[32];
    
    kitoa(0, buffer);
    KTEST_ASSERT_STR_EQ(buffer, "0", "itoa zero");
    
    kitoa(123, buffer);
    KTEST_ASSERT_STR_EQ(buffer, "123", "itoa positive");
    
    kitoa(-456, buffer);
    KTEST_ASSERT_STR_EQ(buffer, "-456", "itoa negative");
    
    return 1;
}

// Test atoi
KTEST_DEFINE(atoi_basic) {
    KTEST_ASSERT_EQ(atoi("0"), 0, "atoi zero");
    KTEST_ASSERT_EQ(atoi("123"), 123, "atoi positive");
    KTEST_ASSERT_EQ(atoi("-456"), -456, "atoi negative");
    KTEST_ASSERT_EQ(atoi("  789"), 789, "atoi with whitespace");
    
    return 1;
}

// Test abs
KTEST_DEFINE(abs_basic) {
    KTEST_ASSERT_EQ(abs(0), 0, "abs zero");
    KTEST_ASSERT_EQ(abs(10), 10, "abs positive");
    KTEST_ASSERT_EQ(abs(-10), 10, "abs negative");
    
    return 1;
}

// Test isdigit
KTEST_DEFINE(isdigit_basic) {
    KTEST_ASSERT(isdigit('0'), "isdigit '0'");
    KTEST_ASSERT(isdigit('5'), "isdigit '5'");
    KTEST_ASSERT(isdigit('9'), "isdigit '9'");
    KTEST_ASSERT(!isdigit('a'), "isdigit 'a' false");
    KTEST_ASSERT(!isdigit(' '), "isdigit space false");
    
    return 1;
}

// Test isalpha
KTEST_DEFINE(isalpha_basic) {
    KTEST_ASSERT(isalpha('a'), "isalpha 'a'");
    KTEST_ASSERT(isalpha('Z'), "isalpha 'Z'");
    KTEST_ASSERT(!isalpha('0'), "isalpha '0' false");
    KTEST_ASSERT(!isalpha(' '), "isalpha space false");
    KTEST_ASSERT(!isalpha('!'), "isalpha '!' false");
    
    return 1;
}

// Test isalnum
KTEST_DEFINE(isalnum_basic) {
    KTEST_ASSERT(isalnum('a'), "isalnum 'a'");
    KTEST_ASSERT(isalnum('Z'), "isalnum 'Z'");
    KTEST_ASSERT(isalnum('0'), "isalnum '0'");
    KTEST_ASSERT(isalnum('9'), "isalnum '9'");
    KTEST_ASSERT(!isalnum(' '), "isalnum space false");
    KTEST_ASSERT(!isalnum('!'), "isalnum '!' false");
    
    return 1;
}

// Test isspace
KTEST_DEFINE(isspace_basic) {
    KTEST_ASSERT(isspace(' '), "isspace ' '");
    KTEST_ASSERT(isspace('\t'), "isspace tab");
    KTEST_ASSERT(isspace('\n'), "isspace newline");
    KTEST_ASSERT(isspace('\r'), "isspace carriage return");
    KTEST_ASSERT(!isspace('a'), "isspace 'a' false");
    KTEST_ASSERT(!isspace('0'), "isspace '0' false");
    
    return 1;
}

// Test toupper
KTEST_DEFINE(toupper_basic) {
    KTEST_ASSERT_EQ(toupper('a'), 'A', "toupper 'a'");
    KTEST_ASSERT_EQ(toupper('z'), 'Z', "toupper 'z'");
    KTEST_ASSERT_EQ(toupper('A'), 'A', "toupper 'A' unchanged");
    KTEST_ASSERT_EQ(toupper('0'), '0', "toupper '0' unchanged");
    KTEST_ASSERT_EQ(toupper(' '), ' ', "toupper space unchanged");
    
    return 1;
}

// Test tolower
KTEST_DEFINE(tolower_basic) {
    KTEST_ASSERT_EQ(tolower('A'), 'a', "tolower 'A'");
    KTEST_ASSERT_EQ(tolower('Z'), 'z', "tolower 'Z'");
    KTEST_ASSERT_EQ(tolower('a'), 'a', "tolower 'a' unchanged");
    KTEST_ASSERT_EQ(tolower('0'), '0', "tolower '0' unchanged");
    KTEST_ASSERT_EQ(tolower(' '), ' ', "tolower space unchanged");
    
    return 1;
}

// Test isupper
KTEST_DEFINE(isupper_basic) {
    KTEST_ASSERT(isupper('A'), "isupper 'A'");
    KTEST_ASSERT(isupper('Z'), "isupper 'Z'");
    KTEST_ASSERT(!isupper('a'), "isupper 'a' false");
    KTEST_ASSERT(!isupper('0'), "isupper '0' false");
    
    return 1;
}

// Test islower
KTEST_DEFINE(islower_basic) {
    KTEST_ASSERT(islower('a'), "islower 'a'");
    KTEST_ASSERT(islower('z'), "islower 'z'");
    KTEST_ASSERT(!islower('A'), "islower 'A' false");
    KTEST_ASSERT(!islower('0'), "islower '0' false");
    
    return 1;
}

// Test rand (basic smoke test)
KTEST_DEFINE(rand_basic) {
    srand(12345);
    
    int r1 = rand();
    int r2 = rand();
    
    // Random numbers should be in valid range
    KTEST_ASSERT(r1 >= 0 && r1 <= RAND_MAX, "rand in valid range");
    KTEST_ASSERT(r2 >= 0 && r2 <= RAND_MAX, "second rand in valid range");
    
    // Should be different (probabilistically)
    KTEST_ASSERT_NEQ(r1, r2, "rand produces different values");
    
    return 1;
}

// Test rand determinism
KTEST_DEFINE(rand_determinism) {
    int r1, r2;
    
    srand(42);
    r1 = rand();
    
    srand(42);
    r2 = rand();
    
    KTEST_ASSERT_EQ(r1, r2, "same seed produces same sequence");
    
    return 1;
}

// Run all stdlib tests
void test_stdlib_suite(void) {
    ktest_t tests[] = {
        KTEST_RUN(itoa_basic),
        KTEST_RUN(atoi_basic),
        KTEST_RUN(abs_basic),
        KTEST_RUN(isdigit_basic),
        KTEST_RUN(isalpha_basic),
        KTEST_RUN(isalnum_basic),
        KTEST_RUN(isspace_basic),
        KTEST_RUN(toupper_basic),
        KTEST_RUN(tolower_basic),
        KTEST_RUN(isupper_basic),
        KTEST_RUN(islower_basic),
        KTEST_RUN(rand_basic),
        KTEST_RUN(rand_determinism),
    };
    
    ktest_run_suite("Standard Library", tests, sizeof(tests) / sizeof(tests[0]));
}
