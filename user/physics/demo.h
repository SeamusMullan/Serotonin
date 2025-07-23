#ifndef PHYSICS_DEMO_H
#define PHYSICS_DEMO_H

#include <stdint.h>
#include "physics.h"
#include "kernel_integration.h"

// Demo scenario types
typedef enum {
    DEMO_SCENARIO_RANDOM_PARTICLES = 0,
    DEMO_SCENARIO_ORBITAL_SYSTEM,
    DEMO_SCENARIO_COLLISION_DEMO,
    DEMO_SCENARIO_GRAVITY_WELL,
    DEMO_SCENARIO_BINARY_SYSTEM,
    DEMO_SCENARIO_PARTICLE_FOUNTAIN,
    DEMO_SCENARIO_GALAXY_SPIRAL,
    DEMO_SCENARIO_PARTICLE_CLUSTER,
    DEMO_SCENARIO_CHAIN_REACTION,
    DEMO_SCENARIO_SOLAR_SYSTEM,
    DEMO_SCENARIO_COUNT
} demo_scenario_t;

// Demo control state
typedef struct {
    demo_scenario_t current_scenario;
    uint8_t demo_running;
    uint8_t demo_paused;
    uint8_t show_help;
    uint8_t show_debug_info;
    uint8_t auto_cycle_scenarios;
    uint32_t scenario_timer;
    uint32_t auto_cycle_interval;
    float gravity_multiplier;
    float time_scale;
    uint32_t particle_count_override;
    uint8_t collision_enabled_override;
} demo_control_t;

// Demo statistics
typedef struct {
    uint32_t frames_rendered;
    uint32_t total_particles;
    uint32_t active_particles;
    uint32_t collisions_detected;
    uint32_t quadtree_nodes_used;
    float average_fps;
    float current_fps;
    uint32_t memory_usage;
    float simulation_time;
} demo_stats_t;

// Keyboard control mappings
typedef enum {
    DEMO_KEY_HELP = 'h',
    DEMO_KEY_PAUSE = ' ',
    DEMO_KEY_RESET = 'r',
    DEMO_KEY_NEXT_SCENARIO = 'n',
    DEMO_KEY_PREV_SCENARIO = 'p',
    DEMO_KEY_DEBUG_INFO = 'd',
    DEMO_KEY_TOGGLE_COLLISIONS = 'c',
    DEMO_KEY_INCREASE_GRAVITY = '+',
    DEMO_KEY_DECREASE_GRAVITY = '-',
    DEMO_KEY_SPEED_UP = '>',
    DEMO_KEY_SLOW_DOWN = '<',
    DEMO_KEY_ADD_PARTICLES = 'a',
    DEMO_KEY_REMOVE_PARTICLES = 'x',
    DEMO_KEY_AUTO_CYCLE = 't',
    DEMO_KEY_QUIT = 'q'
} demo_key_t;

// Global demo control instance
extern demo_control_t *physics_demo_control;

// Demo initialization and cleanup
int physics_demo_init(void);
void physics_demo_shutdown(void);

// Demo control functions
int physics_demo_start(demo_scenario_t scenario);
void physics_demo_stop(void);
void physics_demo_pause(void);
void physics_demo_resume(void);
void physics_demo_reset(void);

// Scenario management
int physics_demo_load_scenario(demo_scenario_t scenario);
void physics_demo_next_scenario(void);
void physics_demo_prev_scenario(void);
const char* physics_demo_get_scenario_name(demo_scenario_t scenario);
const char* physics_demo_get_scenario_description(demo_scenario_t scenario);

// Demo update and rendering
void physics_demo_update(void);
void physics_demo_render(void);
void physics_demo_render_ui(void);
void physics_demo_render_help(void);
void physics_demo_render_debug_info(void);

// Input handling
void physics_demo_handle_key(char key);
void physics_demo_handle_scancode(uint8_t scancode);

// Statistics and monitoring
void physics_demo_update_stats(demo_stats_t *stats);
void physics_demo_get_stats(demo_stats_t *stats);
void physics_demo_reset_stats(void);

// Configuration and parameters
void physics_demo_set_gravity_multiplier(float multiplier);
void physics_demo_set_time_scale(float scale);
void physics_demo_set_particle_count(uint32_t count);
void physics_demo_toggle_collisions(void);
void physics_demo_toggle_auto_cycle(void);

// Scenario implementations
int physics_demo_setup_random_particles(uint32_t count);
int physics_demo_setup_orbital_system(uint32_t planet_count);
int physics_demo_setup_collision_demo(uint32_t count);
int physics_demo_setup_gravity_well(uint32_t particle_count);
int physics_demo_setup_binary_system(void);
int physics_demo_setup_particle_fountain(uint32_t count);
int physics_demo_setup_galaxy_spiral(uint32_t particle_count);
int physics_demo_setup_particle_cluster(uint32_t particle_count);
int physics_demo_setup_chain_reaction(uint32_t particle_count);
int physics_demo_setup_solar_system(void);

// Interactive particle manipulation
int physics_demo_add_particle_at_cursor(float x, float y);
int physics_demo_add_random_particle(void);
int physics_demo_remove_random_particle(void);
void physics_demo_clear_all_particles(void);

// Performance testing
int physics_demo_run_performance_test(void);
int physics_demo_run_stress_test(uint32_t max_particles);
int physics_demo_benchmark_scenarios(void);

// Utility functions
float physics_demo_get_random_float(float min, float max);
uint32_t physics_demo_get_random_uint(uint32_t min, uint32_t max);
uint32_t physics_demo_get_color_for_mass(float mass);
uint32_t physics_demo_get_color_for_velocity(float vx, float vy);

#endif // PHYSICS_DEMO_H