/**
 * @file cxx_init.c
 * @brief C++ runtime initialization for Serotonin OS
 *
 * Provides functions to run global constructors and destructors
 * for C++ programs. The linker populates the .init_array and
 * .fini_array sections with pointers to constructor/destructor functions.
 */

#include <stddef.h>

/** @brief Start of constructor function pointer array (linker-defined) */
extern void (*__init_array_start[])(void);
/** @brief End of constructor function pointer array (linker-defined) */
extern void (*__init_array_end[])(void);

/** @brief Start of destructor function pointer array (linker-defined) */
extern void (*__fini_array_start[])(void);
/** @brief End of destructor function pointer array (linker-defined) */
extern void (*__fini_array_end[])(void);

/**
 * @brief Run all global constructors
 *
 * Iterates through the .init_array section and calls each
 * constructor function. Called by crt0.s before main().
 */
void __run_init_array(void) {
    for (size_t i = 0; i < __init_array_end - __init_array_start; i++) {
        __init_array_start[i]();
    }
}

/**
 * @brief Run all global destructors
 *
 * Iterates through the .fini_array section and calls each
 * destructor function. Called by crt0.s after main() returns.
 */
void __run_fini_array(void) {
    for (size_t i = 0; i < __fini_array_end - __fini_array_start; i++)
        __fini_array_start[i]();
}