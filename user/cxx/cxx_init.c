#include <stddef.h>

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

void __run_init_array(void) {
    for (size_t i = 0; i < __init_array_end - __init_array_start; i++) {
        __init_array_start[i]();
    }
}

void __run_fini_array(void) {
    for (size_t i = 0; i < __fini_array_end - __fini_array_start; i++)
        __fini_array_start[i]();
}