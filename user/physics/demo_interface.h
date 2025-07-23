#ifndef PHYSICS_DEMO_INTERFACE_H
#define PHYSICS_DEMO_INTERFACE_H

#include <stdint.h>

// Demo interface modes
typedef enum {
    DEMO_INTERFACE_MAIN_MENU = 0,
    DEMO_INTERFACE_DEMO_MENU,
    DEMO_INTERFACE_INTERACTIVE_DEMO,
    DEMO_INTERFACE_TEST_MENU,
    DEMO_INTERFACE_BENCHMARK_MENU,
    DEMO_INTERFACE_VALIDATION_MENU,
    DEMO_INTERFACE_HELP,
    DEMO_INTERFACE_TEST_RESULTS
} demo_interface_mode_t;

// Main interface functions
int physics_demo_interface_init(void);
void physics_demo_interface_shutdown(void);
void physics_demo_interface_update(void);
void physics_demo_interface_render(void);
void physics_demo_interface_handle_key(char key);

// Interface state functions
int physics_demo_interface_is_active(void);
demo_interface_mode_t physics_demo_interface_get_mode(void);
void physics_demo_interface_set_mode(demo_interface_mode_t mode);

// Screen dimensions (should match VBE configuration)
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 800

#endif // PHYSICS_DEMO_INTERFACE_H