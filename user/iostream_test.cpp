/**
 * @file iostream_test.cpp
 * @brief iostream functionality test for Serotonin OS
 *
 * Tests the STLport iostream implementation including
 * std::cout output and std::string operations.
 */

#include <iostream>
#include <string>

/**
 * @brief Main entry point for iostream test
 *
 * Verifies iostream functionality by printing various
 * data types to stdout using std::cout.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @param envp Environment variables (unused)
 * @return 0 on success
 */
int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    

    std::cout << "Hello from iostream!" << std::endl;
    std::cout << "Testing numbers: " << 42 << std::endl;

    std::string name = "Serotonin";
    std::cout << "OS: " << name << std::endl;

    return 0;
}
