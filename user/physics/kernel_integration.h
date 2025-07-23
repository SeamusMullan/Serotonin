#ifndef KERNEL_INTEGRATION_H
#define KERNEL_INTEGRATION_H

#include <stdint.h>
#include "../kernel.h"
#include "../video/vbe/vbe.h"
#include "physics.h"

// Kernel physics module state
typedef enum {
    PHYSICS_MODULE_UNINITIALIZED = 0,
    PHYSICS_MODULE_INITIALIZING,
    PHYSICS_MODULE_READY,
    PHYSICS_MODULE_RUNNING,
    PHYSICS_MODULE_PAUSED,
    PHYSICS_MODULE_ERROR,
    PHYSICS_MODULE_SHUTDOWN
} physics_module_state_t;

// Kernel physics module structure
typedef struct {
    physics_simulation_t *simulation;
    physics_renderer_t *renderer;
    physics_module_state_t state;
    uint8_t auto_render;
    uint8_t error_recovery_enabled;
    char last_error[256];
    uint32_t frame_count;
    uint32_t error_count;
} physics_kernel_module_t;

// Error codes for kernel integration
typedef enum {
    PHYSICS_KERNEL_SUCCESS = 0,
    PHYSICS_KERNEL_ERROR_MEMORY,
    PHYSICS_KERNEL_ERROR_VBE,
    PHYSICS_KERNEL_ERROR_CONFIG,
    PHYSICS_KERNEL_ERROR_STATE,
    PHYSICS_KERNEL_ERROR_SIMULATION,
    PHYSICS_KERNEL_ERROR_RENDERER
} physics_kernel_error_t;

// Global kernel physics module instance
extern physics_kernel_module_t *kernel_physics_module;

// Kernel integration functions
physics_kernel_error_t physics_kernel_init(void);
void physics_kernel_shutdown(void);
physics_kernel_error_t physics_kernel_create_simulation(physics_config_t *config);
void physics_kernel_destroy_simulation(void);

// Simulation control functions
physics_kernel_error_t physics_kernel_start_simulation(void);
void physics_kernel_pause_simulation(void);
void physics_kernel_resume_simulation(void);
void physics_kernel_reset_simulation(void);
physics_kernel_error_t physics_kernel_step_simulation(void);

// Rendering control functions
physics_kernel_error_t physics_kernel_render_frame(void);
void physics_kernel_set_auto_render(uint8_t enabled);
physics_kernel_error_t physics_kernel_clear_screen(void);

// Configuration and state functions
physics_kernel_error_t physics_kernel_update_config(const physics_config_t *config);
physics_module_state_t physics_kernel_get_state(void);
const char* physics_kernel_get_last_error(void);
uint32_t physics_kernel_get_frame_count(void);
uint32_t physics_kernel_get_error_count(void);

// Memory management functions
void* physics_kernel_malloc(uint32_t size);
void physics_kernel_free(void *ptr);
uint32_t physics_kernel_get_memory_usage(void);

// Error handling and recovery functions
void physics_kernel_set_error_recovery(uint8_t enabled);
physics_kernel_error_t physics_kernel_recover_from_error(void);
void physics_kernel_log_error(const char *error_msg);

// VBE integration functions
physics_kernel_error_t physics_kernel_init_vbe_renderer(void);
void physics_kernel_cleanup_vbe_renderer(void);
physics_kernel_error_t physics_kernel_check_vbe_compatibility(void);

// Utility functions
const char* physics_kernel_error_to_string(physics_kernel_error_t error);
const char* physics_kernel_state_to_string(physics_module_state_t state);
int physics_kernel_is_ready(void);

// Particle management functions for kernel interface
physics_kernel_error_t physics_kernel_add_particle(float x, float y, float mass, float vx, float vy);
physics_kernel_error_t physics_kernel_remove_particle(uint32_t index);
uint32_t physics_kernel_get_particle_count(void);
physics_kernel_error_t physics_kernel_clear_particles(void);

// Preset initialization functions
physics_kernel_error_t physics_kernel_init_random_particles(uint32_t count);
physics_kernel_error_t physics_kernel_init_orbital_system(uint32_t planet_count);
physics_kernel_error_t physics_kernel_init_collision_demo(uint32_t count);

#endif // KERNEL_INTEGRATION_H