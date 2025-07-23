#ifndef PHYSICS_KERNEL_H
#define PHYSICS_KERNEL_H

#include <stdint.h>
#include "physics.h"

// Kernel integration status codes
typedef enum {
    PHYSICS_KERNEL_SUCCESS = 0,
    PHYSICS_KERNEL_ERROR_INIT = -1,
    PHYSICS_KERNEL_ERROR_MEMORY = -2,
    PHYSICS_KERNEL_ERROR_VBE = -3,
    PHYSICS_KERNEL_ERROR_CONFIG = -4,
    PHYSICS_KERNEL_ERROR_STATE = -5
} physics_kernel_status_t;

// Kernel integration state
typedef struct {
    physics_simulation_t *simulation;
    physics_renderer_t *renderer;
    uint8_t initialized;
    uint8_t running;
    uint8_t paused;
    uint32_t frame_count;
    uint32_t last_frame_time;
    uint8_t keyboard_enabled;
    uint8_t demo_mode;
    uint32_t current_demo;
} physics_kernel_state_t;

// Demo scenarios
typedef enum {
    PHYSICS_DEMO_ORBITAL = 0,
    PHYSICS_DEMO_COLLISION = 1,
    PHYSICS_DEMO_GRAVITY_WELL = 2,
    PHYSICS_DEMO_RANDOM = 3,
    PHYSICS_DEMO_COUNT = 4
} physics_demo_type_t;

// Keyboard control mappings
typedef enum {
    PHYSICS_KEY_PAUSE = 'p',
    PHYSICS_KEY_RESET = 'r',
    PHYSICS_KEY_NEXT_DEMO = 'n',
    PHYSICS_KEY_DEBUG = 'd',
    PHYSICS_KEY_QUIT = 'q',
    PHYSICS_KEY_GRAVITY_UP = '+',
    PHYSICS_KEY_GRAVITY_DOWN = '-',
    PHYSICS_KEY_SPEED_UP = ']',
    PHYSICS_KEY_SPEED_DOWN = '['
} physics_key_mapping_t;

// Global kernel integration state
extern physics_kernel_state_t *physics_kernel_state;

// Core kernel integration functions
physics_kernel_status_t physics_kernel_init(void);
void physics_kernel_shutdown(void);
physics_kernel_status_t physics_kernel_start_demo(physics_demo_type_t demo_type);
void physics_kernel_update(void);
void physics_kernel_render(void);

// Keyboard input handling
void physics_kernel_handle_key(char key);
void physics_kernel_enable_keyboard(uint8_t enable);

// Demo management
physics_kernel_status_t physics_kernel_load_demo(physics_demo_type_t demo_type);
void physics_kernel_next_demo(void);
const char* physics_kernel_get_demo_name(physics_demo_type_t demo_type);

// State management
void physics_kernel_pause(void);
void physics_kernel_resume(void);
void physics_kernel_reset(void);
uint8_t physics_kernel_is_running(void);

// Error handling and diagnostics
const char* physics_kernel_get_error_string(physics_kernel_status_t status);
void physics_kernel_print_status(void);
void physics_kernel_handle_error(physics_kernel_status_t status, const char *context);

// Memory management integration
uint32_t physics_kernel_get_memory_usage(void);
void physics_kernel_cleanup_memory(void);

// Performance monitoring
void physics_kernel_get_performance_info(char *buffer, uint32_t buffer_size);

#endif // PHYSICS_KERNEL_H