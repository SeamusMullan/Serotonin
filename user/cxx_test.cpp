/**
 * @file cxx_test.cpp
 * @brief C++ runtime test program for Serotonin OS
 *
 * Tests basic C++ functionality including:
 * - Global constructor/destructor execution
 * - new/delete operators
 * - Basic class instantiation
 */

extern "C" int write(int, const void*, unsigned);

/**
 * @brief Test class for C++ runtime verification
 *
 * This class tests that global constructors and destructors
 * are properly executed by the C++ runtime.
 */
struct Test {
    Test() {
        write(1, "bob the builder\n", 16);
    }
    ~Test() {
        write(1, "destructor ran\n", 14);
    }
};

/** @brief Global Test instance to verify constructor execution */
Test t;

/**
 * @brief Main entry point for C++ runtime test
 *
 * Tests new/delete operators and verifies dynamic memory allocation.
 *
 * @return 0 on success
 */
int main() {
    write(1, "HELLO CPP\n", 10);

    int* x = new int(123);
    if (*x == 123)
        write(1, "yo\n", 3);
    delete x;

    return 0;
}
