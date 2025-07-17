#include "kernel_integration.h"
#include "physics.h"
#include "renderer.h"
#include "particle.h"
#include "../kernel.h"
#include "../video/vbe/vbe.h"
#include <stddef.h>

// Global kernel physics module instance
physics_kernel_module_t *kernel_physics_module = NULL;

// Internal helper functions
static void physics_kernel_set_error(const char *error_msg);
static physics_kernel_error_t physics_kernel_validate_state(physics_module_state_t required_state);
static void physics_kernel_cleanup_internal(void);

// Error message strings
static const char* error_strings[] = {
    "Success",
    "Memory allocation error",
    "VBE graphics system error", 
    "Configuration error",
    "Invalid module state",
    "Simulation error",
    "Renderer error"
};

// State message strings
static const char* state_strings[] = {
    "Uninitialized",
    "Initializing", 
    "Ready",
    "Running",
    "Paused",
    "Error",
    "Shutdown"
};

physics_kernel_error_t physics_kernel_init(void) {
    // Check if already initialized
    if (kernel_physics_module != NULL) {
        return PHYSICS_KERNEL_SUCCESS;
    }
    
    // Allocate kernel module structure
    kernel_physics_module = (physics_kernel_module_t*)kernel_malloc(sizeof(physics_kernel_module_t));
    if (!kernel_physics_module) {
        return PHYSICS_KERNEL_ERROR_MEMORY;
    }
    
    // Initialize module structure
    kernel_physics_module->simulation = NULL;
    kernel_physics_module->renderer = NULL;
    kernel_physics_module->state = PHYSICS_MODULE_INITIALIZING;
    kernel_physics_module->auto_render = 1;
    kernel_physics_module->error_recovery_enabled = 1;
    kernel_physics_module->last_error[0] = '\0';
    kernel_physics_module->frame_count = 0;
    kernel_physics_module->error_count = 0;
    
    // Check VBE compatibility
    physics_kernel_error_t vbe_result = physics_kernel_check_vbe_compatibility();
    if (vbe_result != PHYSICS_KERNEL_SUCCESS) {
        physics_kernel_cleanup_internal();
        return vbe_result;
    }
    
    // Initialize VBE renderer
    physics_kernel_error_t renderer_result = physics_kernel_init_vbe_renderer();
    if (renderer_result != PHYSICS_KERNEL_SUCCESS) {
        physics_kernel_cleanup_internal();
        return renderer_result;
    }
    
    kernel_physics_module->state = PHYSICS_MODULE_READY;
    return PHYSICS_KERNEL_SUCCESS;
}

void physics_kernel_shutdown(void) {
    if (!kernel_physics_module) {
        return;
    }
    
    kernel_physics_module->state = PHYSICS_MODULE_SHUTDOWN;
    
    // Destroy simulation if it exists
    if (kernel_physics_module->simulation) {
        physics_simulation_destroy(kernel_physics_module->simulation);
        kernel_physics_module->simulation = NULL;
    }
    
    // Cleanup VBE renderer
    physics_kernel_cleanup_vbe_renderer();
    
    // Free module structure
    kernel_free(kernel_physics_module);
    kernel_physics_module = NULL;
}

physics_kernel_error_t physics_kernel_create_simulation(physics_config_t *config) {
    if (!kernel_physics_module) {
        return PHYSICS_KERNEL_ERROR_STATE;
    }
    
    if (kernel_physics_module->state != PHYSICS_MODULE_READY && 
        kernel_physics_module->state != PHYSICS_MODULE_PAUSED) {
        physics_kernel_set_error("Invalid state for creating simulation");
        return PHYSICS_KERNEL_ERROR_STATE;
    }
    
    // Validate configuration
    if (!config || !physics_config_validate(config)) {
        physics_kernel_set_error("Invalid physics configuration");
        return PHYSICS_KERNEL_ERROR_CONFIG;
    }
    
    // Destroy existing simulation if present
    if (kernel_physics_module->simulation) {
        physics_simulation_destroy(kernel_physics_module->simulation);
        kernel_physics_module->simulation = NULL;
    }
    
    // Create new simulation
    kernel_physics_module->simulation = physics_simulation_create(config);
    if (!kernel_physics_module->simulation) {
        physics_kernel_set_error("Failed to create physics simulation");
        kernel_physics_module->state = PHYSICS_MODULE_ERROR;
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    // Connect renderer to simulation
    if (kernel_physics_module->renderer) {
        // Set renderer in simulation (assuming this function exists)
        kernel_physics_module->simulation->renderer = kernel_physics_module->renderer;
    }
    
    kernel_physics_module->state = PHYSICS_MODULE_READY;
    return PHYSICS_KERNEL_SUCCESS;
}

void physics_kernel_destroy_simulation(void) {
    if (!kernel_physics_module || !kernel_physics_module->simulation) {
        return;
    }
    
    physics_simulation_destroy(kernel_physics_module->simulation);
    kernel_physics_module->simulation = NULL;
    kernel_physics_module->state = PHYSICS_MODULE_READY;
}

physics_kernel_error_t physics_kernel_start_simulation(void) {
    physics_kernel_error_t state_check = physics_kernel_validate_state(PHYSICS_MODULE_READY);
    if (state_check != PHYSICS_KERNEL_SUCCESS) {
        return state_check;
    }
    
    if (!kernel_physics_module->simulation) {
        physics_kernel_set_error("No simulation created");
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    kernel_physics_module->state = PHYSICS_MODULE_RUNNING;
    kernel_physics_module->frame_count = 0;
    return PHYSICS_KERNEL_SUCCESS;
}

void physics_kernel_pause_simulation(void) {
    if (kernel_physics_module && kernel_physics_module->state == PHYSICS_MODULE_RUNNING) {
        kernel_physics_module->state = PHYSICS_MODULE_PAUSED;
        if (kernel_physics_module->simulation) {
            physics_simulation_pause(kernel_physics_module->simulation);
        }
    }
}

void physics_kernel_resume_simulation(void) {
    if (kernel_physics_module && kernel_physics_module->state == PHYSICS_MODULE_PAUSED) {
        kernel_physics_module->state = PHYSICS_MODULE_RUNNING;
        if (kernel_physics_module->simulation) {
            physics_simulation_resume(kernel_physics_module->simulation);
        }
    }
}

void physics_kernel_reset_simulation(void) {
    if (!kernel_physics_module || !kernel_physics_module->simulation) {
        return;
    }
    
    physics_simulation_reset(kernel_physics_module->simulation);
    kernel_physics_module->frame_count = 0;
    
    if (kernel_physics_module->state == PHYSICS_MODULE_RUNNING) {
        kernel_physics_module->state = PHYSICS_MODULE_READY;
    }
}

physics_kernel_error_t physics_kernel_step_simulation(void) {
    if (!kernel_physics_module) {
        return PHYSICS_KERNEL_ERROR_STATE;
    }
    
    if (kernel_physics_module->state != PHYSICS_MODULE_RUNNING) {
        physics_kernel_set_error("Simulation not running");
        return PHYSICS_KERNEL_ERROR_STATE;
    }
    
    if (!kernel_physics_module->simulation) {
        physics_kernel_set_error("No simulation available");
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    // Perform simulation step with error handling
    physics_simulation_step(kernel_physics_module->simulation);
    
    // Auto-render if enabled
    if (kernel_physics_module->auto_render) {
        physics_kernel_error_t render_result = physics_kernel_render_frame();
        if (render_result != PHYSICS_KERNEL_SUCCESS) {
            // Log error but don't fail the step
            kernel_physics_module->error_count++;
        }
    }
    
    kernel_physics_module->frame_count++;
    return PHYSICS_KERNEL_SUCCESS;
}

physics_kernel_error_t physics_kernel_render_frame(void) {
    if (!kernel_physics_module) {
        return PHYSICS_KERNEL_ERROR_STATE;
    }
    
    if (!kernel_physics_module->simulation || !kernel_physics_module->renderer) {
        physics_kernel_set_error("Simulation or renderer not available");
        return PHYSICS_KERNEL_ERROR_RENDERER;
    }
    
    // Clear screen
    physics_renderer_clear_screen(kernel_physics_module->renderer);
    
    // Render simulation
    physics_renderer_render_simulation(kernel_physics_module->renderer, 
                                     kernel_physics_module->simulation->particles,
                                     kernel_physics_module->simulation->quadtree);
    
    // Flip VBE buffer to display
    vbe_flip();
    
    return PHYSICS_KERNEL_SUCCESS;
}

void physics_kernel_set_auto_render(uint8_t enabled) {
    if (kernel_physics_module) {
        kernel_physics_module->auto_render = enabled;
    }
}

physics_kernel_error_t physics_kernel_clear_screen(void) {
    if (!kernel_physics_module || !kernel_physics_module->renderer) {
        return PHYSICS_KERNEL_ERROR_RENDERER;
    }
    
    physics_renderer_clear_screen(kernel_physics_module->renderer);
    vbe_flip();
    
    return PHYSICS_KERNEL_SUCCESS;
}

physics_kernel_error_t physics_kernel_update_config(const physics_config_t *config) {
    if (!kernel_physics_module || !config) {
        return PHYSICS_KERNEL_ERROR_CONFIG;
    }
    
    if (!physics_config_validate(config)) {
        physics_kernel_set_error("Invalid configuration parameters");
        return PHYSICS_KERNEL_ERROR_CONFIG;
    }
    
    if (kernel_physics_module->simulation) {
        physics_simulation_update_config(kernel_physics_module->simulation, config);
    }
    
    return PHYSICS_KERNEL_SUCCESS;
}

physics_module_state_t physics_kernel_get_state(void) {
    return kernel_physics_module ? kernel_physics_module->state : PHYSICS_MODULE_UNINITIALIZED;
}

const char* physics_kernel_get_last_error(void) {
    return kernel_physics_module ? kernel_physics_module->last_error : "Module not initialized";
}

uint32_t physics_kernel_get_frame_count(void) {
    return kernel_physics_module ? kernel_physics_module->frame_count : 0;
}

uint32_t physics_kernel_get_error_count(void) {
    return kernel_physics_module ? kernel_physics_module->error_count : 0;
}

void* physics_kernel_malloc(uint32_t size) {
    return kernel_malloc(size);
}

void physics_kernel_free(void *ptr) {
    kernel_free(ptr);
}

uint32_t physics_kernel_get_memory_usage(void) {
    // This would require tracking memory usage - simplified implementation
    return 0;
}

void physics_kernel_set_error_recovery(uint8_t enabled) {
    if (kernel_physics_module) {
        kernel_physics_module->error_recovery_enabled = enabled;
    }
}

physics_kernel_error_t physics_kernel_recover_from_error(void) {
    if (!kernel_physics_module) {
        return PHYSICS_KERNEL_ERROR_STATE;
    }
    
    if (kernel_physics_module->state != PHYSICS_MODULE_ERROR) {
        return PHYSICS_KERNEL_SUCCESS;
    }
    
    // Attempt to recover by resetting simulation
    if (kernel_physics_module->simulation) {
        physics_simulation_reset(kernel_physics_module->simulation);
    }
    
    kernel_physics_module->state = PHYSICS_MODULE_READY;
    kernel_physics_module->last_error[0] = '\0';
    
    return PHYSICS_KERNEL_SUCCESS;
}

void physics_kernel_log_error(const char *error_msg) {
    if (kernel_physics_module && error_msg) {
        physics_kernel_set_error(error_msg);
        kernel_physics_module->error_count++;
        
        if (kernel_physics_module->error_recovery_enabled) {
            physics_kernel_recover_from_error();
        }
    }
}

physics_kernel_error_t physics_kernel_init_vbe_renderer(void) {
    if (!kernel_physics_module) {
        return PHYSICS_KERNEL_ERROR_STATE;
    }
    
    // Create physics renderer that uses VBE
    kernel_physics_module->renderer = physics_renderer_create(vbe_info.framebuffer,
                                                            vbe_info.width,
                                                            vbe_info.height,
                                                            0xFF000000); // Black background
    
    if (!kernel_physics_module->renderer) {
        physics_kernel_set_error("Failed to create VBE renderer");
        return PHYSICS_KERNEL_ERROR_RENDERER;
    }
    
    return PHYSICS_KERNEL_SUCCESS;
}

void physics_kernel_cleanup_vbe_renderer(void) {
    if (kernel_physics_module && kernel_physics_module->renderer) {
        physics_renderer_destroy(kernel_physics_module->renderer);
        kernel_physics_module->renderer = NULL;
    }
}

physics_kernel_error_t physics_kernel_check_vbe_compatibility(void) {
    // Check if VBE is initialized and compatible
    if (vbe_info.width == 0 || vbe_info.height == 0) {
        return PHYSICS_KERNEL_ERROR_VBE;
    }
    
    if (vbe_info.bpp != 32) {
        return PHYSICS_KERNEL_ERROR_VBE;
    }
    
    if (!vbe_info.framebuffer || !vbe_info.backbuffer) {
        return PHYSICS_KERNEL_ERROR_VBE;
    }
    
    return PHYSICS_KERNEL_SUCCESS;
}

const char* physics_kernel_error_to_string(physics_kernel_error_t error) {
    if (error >= 0 && error < sizeof(error_strings) / sizeof(error_strings[0])) {
        return error_strings[error];
    }
    return "Unknown error";
}

const char* physics_kernel_state_to_string(physics_module_state_t state) {
    if (state >= 0 && state < sizeof(state_strings) / sizeof(state_strings[0])) {
        return state_strings[state];
    }
    return "Unknown state";
}

int physics_kernel_is_ready(void) {
    return (kernel_physics_module && 
            kernel_physics_module->state == PHYSICS_MODULE_READY &&
            kernel_physics_module->simulation &&
            kernel_physics_module->renderer);
}

// Particle management functions
physics_kernel_error_t physics_kernel_add_particle(float x, float y, float mass, float vx, float vy) {
    if (!kernel_physics_module || !kernel_physics_module->simulation) {
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    int result = particle_add(kernel_physics_module->simulation->particles, x, y, mass, 1.0f);
    if (result < 0) {
        physics_kernel_set_error("Failed to add particle");
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    // Set initial velocity
    particle_t *particle = &kernel_physics_module->simulation->particles->particles[result];
    particle->vx = vx;
    particle->vy = vy;
    
    return PHYSICS_KERNEL_SUCCESS;
}

physics_kernel_error_t physics_kernel_remove_particle(uint32_t index) {
    if (!kernel_physics_module || !kernel_physics_module->simulation) {
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    if (index >= kernel_physics_module->simulation->particles->count) {
        physics_kernel_set_error("Invalid particle index");
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    // Mark particle as inactive
    kernel_physics_module->simulation->particles->particles[index].active = 0;
    
    return PHYSICS_KERNEL_SUCCESS;
}

uint32_t physics_kernel_get_particle_count(void) {
    if (!kernel_physics_module || !kernel_physics_module->simulation) {
        return 0;
    }
    
    return kernel_physics_module->simulation->particles->count;
}

physics_kernel_error_t physics_kernel_clear_particles(void) {
    if (!kernel_physics_module || !kernel_physics_module->simulation) {
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    kernel_physics_module->simulation->particles->count = 0;
    return PHYSICS_KERNEL_SUCCESS;
}

// Preset initialization functions
physics_kernel_error_t physics_kernel_init_random_particles(uint32_t count) {
    if (!kernel_physics_module || !kernel_physics_module->simulation) {
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    int result = initialize_random_particles(kernel_physics_module->simulation, count);
    if (result != 1) {
        physics_kernel_set_error("Failed to initialize random particles");
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    return PHYSICS_KERNEL_SUCCESS;
}

physics_kernel_error_t physics_kernel_init_orbital_system(uint32_t planet_count) {
    if (!kernel_physics_module || !kernel_physics_module->simulation) {
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    int result = initialize_orbital_system(kernel_physics_module->simulation, planet_count);
    if (result != 1) {
        physics_kernel_set_error("Failed to initialize orbital system");
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    return PHYSICS_KERNEL_SUCCESS;
}

physics_kernel_error_t physics_kernel_init_collision_demo(uint32_t count) {
    if (!kernel_physics_module || !kernel_physics_module->simulation) {
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    int result = initialize_collision_demo(kernel_physics_module->simulation, count);
    if (result != 1) {
        physics_kernel_set_error("Failed to initialize collision demo");
        return PHYSICS_KERNEL_ERROR_SIMULATION;
    }
    
    return PHYSICS_KERNEL_SUCCESS;
}

// Internal helper functions
static void physics_kernel_set_error(const char *error_msg) {
    if (!kernel_physics_module || !error_msg) {
        return;
    }
    
    // Simple string copy with bounds checking
    uint32_t i = 0;
    while (i < 255 && error_msg[i] != '\0') {
        kernel_physics_module->last_error[i] = error_msg[i];
        i++;
    }
    kernel_physics_module->last_error[i] = '\0';
    
    kernel_physics_module->state = PHYSICS_MODULE_ERROR;
}

static physics_kernel_error_t physics_kernel_validate_state(physics_module_state_t required_state) {
    if (!kernel_physics_module) {
        return PHYSICS_KERNEL_ERROR_STATE;
    }
    
    if (kernel_physics_module->state != required_state) {
        physics_kernel_set_error("Invalid module state for operation");
        return PHYSICS_KERNEL_ERROR_STATE;
    }
    
    return PHYSICS_KERNEL_SUCCESS;
}

static void physics_kernel_cleanup_internal(void) {
    if (kernel_physics_module) {
        physics_kernel_cleanup_vbe_renderer();
        kernel_free(kernel_physics_module);
        kernel_physics_module = NULL;
    }
}