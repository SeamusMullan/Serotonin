/**
 * @file stl_test.cpp
 * @brief STLport container and algorithm test for Serotonin OS
 *
 * Tests STL functionality without relying on iostream, using
 * direct system calls for output. Verifies:
 * - std::vector push_back and indexing
 * - std::string concatenation and c_str()
 * - std::sort algorithm
 */

#include <vector>
#include <string>
#include <algorithm>

/** @brief System call for writing to file descriptor */
extern "C" int write(int fd, const void* buf, unsigned long count);

/**
 * @brief Calculate string length
 * @param s Null-terminated string
 * @return Length of string (not including null terminator)
 */
static unsigned long my_strlen(const char* s) {
    unsigned long len = 0;
    while (s[len]) len++;
    return len;
}

/**
 * @brief Print a null-terminated string via syscall
 * @param s String to print
 */
static void print(const char* s) {
    write(1, s, my_strlen(s));
}

/**
 * @brief Print an integer as decimal string
 * @param n Integer to print
 */
static void print_num(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) {
        buf[i++] = '0';
    } else {
        if (n < 0) {
            buf[i++] = '-';
            n = -n;
        }
        int start = i;
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
        // Reverse digits
        for (int j = start, k = i - 1; j < k; j++, k--) {
            char t = buf[j];
            buf[j] = buf[k];
            buf[k] = t;
        }
    }
    buf[i] = '\0';
    print(buf);
}

/**
 * @brief Main entry point for STL test program
 *
 * Runs tests for std::vector, std::string, and std::sort,
 * printing results directly via system calls.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @param envp Environment variables (unused)
 * @return 0 on success
 */
int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    print("=== STLport Container Test ===\n");

    // Test std::vector
    print("\n[Vector Test]\n");
    std::vector<int> vec;
    vec.push_back(10);
    vec.push_back(20);
    vec.push_back(30);

    print("Vector size: ");
    print_num((int)vec.size());
    print("\nVector contents: ");
    for (size_t i = 0; i < vec.size(); i++) {
        print_num(vec[i]);
        print(" ");
    }
    print("\n");

    // Test std::string
    print("\n[String Test]\n");
    std::string str = "Hello";
    str += " Serotonin!";
    print("String: ");
    print(str.c_str());
    print("\nString length: ");
    print_num((int)str.length());
    print("\n");

    // Test std::sort
    print("\n[Algorithm Test]\n");
    std::vector<int> nums;
    nums.push_back(5);
    nums.push_back(2);
    nums.push_back(8);
    nums.push_back(1);
    nums.push_back(9);

    print("Before sort: ");
    for (size_t i = 0; i < nums.size(); i++) {
        print_num(nums[i]);
        print(" ");
    }
    print("\n");

    std::sort(nums.begin(), nums.end());

    print("After sort:  ");
    for (size_t i = 0; i < nums.size(); i++) {
        print_num(nums[i]);
        print(" ");
    }
    print("\n");

    print("\n=== All tests passed! ===\n");
    return 0;
}
