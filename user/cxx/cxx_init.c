/**
 * @file cxx_init.c
 * @brief C++ runtime initialization for Serotonin OS
 *
 * Provides functions to run global constructors and destructors
 * for C++ programs. The linker populates the .init_array and
 * .fini_array sections with pointers to constructor/destructor functions.
 */

#include <stddef.h>

/** @brief Start/end of .init_array (linker-defined) */
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

/** @brief Start/end of .fini_array (linker-defined) */
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

/** @brief Start/end of .ctors (linker-defined, used by C++ instead of .init_array) */
extern void (*__CTOR_LIST__[])(void);
extern void (*__CTOR_END__[])(void);

/** @brief Start/end of .dtors (linker-defined) */
extern void (*__DTOR_LIST__[])(void);
extern void (*__DTOR_END__[])(void);

typedef void (*ctor_fn)(void);

/**
 * @brief Run all global constructors
 *
 * Iterates through both .init_array and .ctors sections.
 * .ctors entries may include a -1 sentinel at the start and 0 sentinel at
 * the end; skip those.  Called by crt0.s before main().
 */
void __run_init_array(void) {
    /* .init_array — straightforward array of function pointers */
    for (size_t i = 0; i < (size_t)(__init_array_end - __init_array_start); i++)
        __init_array_start[i]();

    /* .ctors — may have sentinels: skip (void*)-1 at start, stop at NULL */
    for (ctor_fn *p = (ctor_fn *)__CTOR_LIST__; p < (ctor_fn *)__CTOR_END__; p++) {
        if (*p && *p != (ctor_fn)(size_t)-1)
            (*p)();
    }
}

/**
 * @brief Run all global destructors
 *
 * Iterates through both .fini_array and .dtors sections.
 * Called by crt0.s after main() returns.
 */
void __run_fini_array(void) {
    for (size_t i = 0; i < (size_t)(__fini_array_end - __fini_array_start); i++)
        __fini_array_start[i]();

    for (ctor_fn *p = (ctor_fn *)__DTOR_LIST__; p < (ctor_fn *)__DTOR_END__; p++) {
        if (*p && *p != (ctor_fn)(size_t)-1)
            (*p)();
    }
}