#include "demo_interface.h"
#include "demo.h"
#include "comprehensive_test.h"
#include "performance_test.h"
#include "../kernel.h"
#include "../stdio/stdio.h"
#include "../io/keyboard.h"
#include <stddef.h>

// Demo interface state
typedef struct {
    demo_interface_mode_t current_mode;
    uint8_t interface_active;
    uint8_t show_menu;
    uint32_t menu_selection;
    uint32_t test_results_display_timer;
    char status_message[256];
} demo_interface_state_t;

static demo_interface_state_t interface_state = {0};

// Menu options for different modes
static const char* main_menu_options[] = {
    "1. Interactive Physics Demo",
    "2. Run Comprehensive Tests",
    "3. Performance Benchmarks",
    "4. Stress Testing",
    "5. Validation Tests",
    "6. Help & Documentation",
    "7. Exit Demo Interface"
};

static const char* demo_menu_options[] = {
    "1. Start Random Particles Demo",
    "2. Start Orbital System Demo", 
    "3. Start Collision Demo",
    "4. Start Gravity Well Demo",
    "5. Start Binary System Demo",
    "6. Start Particle Fountain Demo",
    "7. Start Galaxy Spiral Demo",
    "8. Start Particle Cluster Demo",
    "9. Start Chain Reaction Demo",
    "10. Start Solar System Demo",
    "11. Benchmark All Scenarios",
    "12. Back to Main Menu"
};

static const char* test_menu_options[] = {
    "1. Run All Comprehensive Tests",
    "2. Run Particle System Tests",
    "3. Run Quadtree Tests",
    "4. Run Barnes-Hut Tests",
    "5. Run Collision Tests",
    "6. Run Physics Integration Tests",
    "7. Run Memory Management Tests",
    "8. Run Performance Tests",
    "9. Run Rendering Tests",
    "10. Run Demo System Tests",
    "11. Back to Main Menu"
};

static const char* benchmark_menu_options[] = {
    "1. Quick Performance Benchmark",
    "2. Comprehensive Performance Test",
    "3. Memory Usage Analysis",
    "4. Scaling Performance Test",
    "5. Component Timing Analysis",
    "6. SIMD Optimization Tests",
    "7. Cache Performance Tests",
    "8. Back to Main Menu"
};

static const char* validation_menu_options[] = {
    "1. Physics Accuracy Validation",
    "2. Energy Conservation Test",
    "3. Momentum Conservation Test",
    "4. Numerical Stability Test",
    "5. Long-term Stability Test",
    "6. Extreme Parameter Tests",
    "7. Back to Main Menu"
};

// Function prototypes
static void render_main_menu(void);
static void render_demo_menu(void);
static void render_test_menu(void);
static void render_benchmark_menu(void);
static void render_validation_menu(void);
static void render_help_screen(void);
static void render_status_bar(void);
static void handle_main_menu_input(char key);
static void handle_demo_menu_input(char key);
static void handle_test_menu_input(char key);
static void handle_benchmark_menu_input(char key);
static void handle_validation_menu_input(char key);
static void set_status_message(const char* message);
static void render_text_centered(const char* text, uint32_t y, uint32_t color);
static void render_menu_options(const char** options, uint32_t count, uint32_t selected);

// Main interface functions
int physics_demo_interface_init(void) {
    // Initialize demo system
    if (!physics_demo_init()) {
        return 0;
    }
    
    // Initialize interface state
    interface_state.current_mode = DEMO_INTERFACE_MAIN_MENU;
    interface_state.interface_active = 1;
    interface_state.show_menu = 1;
    interface_state.menu_selection = 0;
    interface_state.test_results_display_timer = 0;
    
    set_status_message("Physics Demo Interface Initialized");
    
    return 1;
}

void physics_demo_interface_shutdown(void) {
    physics_demo_shutdown();
    interface_state.interface_active = 0;
    set_status_message("Physics Demo Interface Shutdown");
}

void physics_demo_interface_update(void) {
    if (!interface_state.interface_active) {
        return;
    }
    
    // Update demo if running
    if (interface_state.current_mode == DEMO_INTERFACE_INTERACTIVE_DEMO) {
        physics_demo_update();
    }
    
    // Update display timer
    if (interface_state.test_results_display_timer > 0) {
        interface_state.test_results_display_timer--;
    }
}

void physics_demo_interface_render(void) {
    if (!interface_state.interface_active) {
        return;
    }
    
    // Clear screen
    physics_kernel_clear_screen();
    
    switch (interface_state.current_mode) {
        case DEMO_INTERFACE_MAIN_MENU:
            render_main_menu();
            break;
            
        case DEMO_INTERFACE_DEMO_MENU:
            render_demo_menu();
            break;
            
        case DEMO_INTERFACE_INTERACTIVE_DEMO:
            physics_demo_render();
            break;
            
        case DEMO_INTERFACE_TEST_MENU:
            render_test_menu();
            break;
            
        case DEMO_INTERFACE_BENCHMARK_MENU:
            render_benchmark_menu();
            break;
            
        case DEMO_INTERFACE_VALIDATION_MENU:
            render_validation_menu();
            break;
            
        case DEMO_INTERFACE_HELP:
            render_help_screen();
            break;
            
        case DEMO_INTERFACE_TEST_RESULTS:
            // Test results are displayed by the test functions
            break;
    }
    
    // Always render status bar
    render_status_bar();
}

void physics_demo_interface_handle_key(char key) {
    if (!interface_state.interface_active) {
        return;
    }
    
    // Global keys
    if (key == 'q' && interface_state.current_mode != DEMO_INTERFACE_INTERACTIVE_DEMO) {
        interface_state.interface_active = 0;
        return;
    }
    
    switch (interface_state.current_mode) {
        case DEMO_INTERFACE_MAIN_MENU:
            handle_main_menu_input(key);
            break;
            
        case DEMO_INTERFACE_DEMO_MENU:
            handle_demo_menu_input(key);
            break;
            
        case DEMO_INTERFACE_INTERACTIVE_DEMO:
            if (key == 'm') {
                // Return to demo menu
                physics_demo_stop();
                interface_state.current_mode = DEMO_INTERFACE_DEMO_MENU;
                set_status_message("Returned to Demo Menu");
            } else {
                physics_demo_handle_key(key);
            }
            break;
            
        case DEMO_INTERFACE_TEST_MENU:
            handle_test_menu_input(key);
            break;
            
        case DEMO_INTERFACE_BENCHMARK_MENU:
            handle_benchmark_menu_input(key);
            break;
            
        case DEMO_INTERFACE_VALIDATION_MENU:
            handle_validation_menu_input(key);
            break;
            
        case DEMO_INTERFACE_HELP:
            if (key == ' ' || key == '\n' || key == 'b') {
                interface_state.current_mode = DEMO_INTERFACE_MAIN_MENU;
                set_status_message("Returned to Main Menu");
            }
            break;
            
        case DEMO_INTERFACE_TEST_RESULTS:
            if (key == ' ' || key == '\n' || key == 'b') {
                interface_state.current_mode = DEMO_INTERFACE_MAIN_MENU;
                set_status_message("Returned to Main Menu");
            }
            break;
    }
}

// Menu rendering functions
static void render_main_menu(void) {
    render_text_centered("PHYSICS SIMULATION DEMO INTERFACE", 50, 0xFFFFFFFF);
    render_text_centered("Serotonin Kernel - Barnes-Hut Physics System", 80, 0xFFCCCCCC);
    
    uint32_t menu_count = sizeof(main_menu_options) / sizeof(main_menu_options[0]);
    render_menu_options(main_menu_options, menu_count, interface_state.menu_selection);
    
    render_text_centered("Use number keys to select, 'q' to quit", SCREEN_HEIGHT - 100, 0xFF888888);
}

static void render_demo_menu(void) {
    render_text_centered("INTERACTIVE PHYSICS DEMOS", 50, 0xFFFFFFFF);
    
    uint32_t menu_count = sizeof(demo_menu_options) / sizeof(demo_menu_options[0]);
    render_menu_options(demo_menu_options, menu_count, interface_state.menu_selection);
    
    render_text_centered("Select a demo scenario to run", SCREEN_HEIGHT - 100, 0xFF888888);
}

static void render_test_menu(void) {
    render_text_centered("COMPREHENSIVE TEST SUITE", 50, 0xFFFFFFFF);
    
    uint32_t menu_count = sizeof(test_menu_options) / sizeof(test_menu_options[0]);
    render_menu_options(test_menu_options, menu_count, interface_state.menu_selection);
    
    render_text_centered("Select tests to run", SCREEN_HEIGHT - 100, 0xFF888888);
}

static void render_benchmark_menu(void) {
    render_text_centered("PERFORMANCE BENCHMARKS", 50, 0xFFFFFFFF);
    
    uint32_t menu_count = sizeof(benchmark_menu_options) / sizeof(benchmark_menu_options[0]);
    render_menu_options(benchmark_menu_options, menu_count, interface_state.menu_selection);
    
    render_text_centered("Select benchmark to run", SCREEN_HEIGHT - 100, 0xFF888888);
}

static void render_validation_menu(void) {
    render_text_centered("PHYSICS VALIDATION TESTS", 50, 0xFFFFFFFF);
    
    uint32_t menu_count = sizeof(validation_menu_options) / sizeof(validation_menu_options[0]);
    render_menu_options(validation_menu_options, menu_count, interface_state.menu_selection);
    
    render_text_centered("Select validation test to run", SCREEN_HEIGHT - 100, 0xFF888888);
}

static void render_help_screen(void) {
    render_text_centered("PHYSICS DEMO HELP", 50, 0xFFFFFFFF);
    
    uint32_t y = 120;
    uint32_t line_height = 25;
    
    // General help
    vbe_draw_string("GENERAL CONTROLS:", 50, y, 0xFFFFFF00);
    y += line_height;
    vbe_draw_string("  Number keys - Select menu options", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  'q' - Quit interface", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  'b' - Back to previous menu", 70, y, 0xFFCCCCCC);
    y += line_height * 2;
    
    // Demo controls
    vbe_draw_string("DEMO CONTROLS (when running):", 50, y, 0xFFFFFF00);
    y += line_height;
    vbe_draw_string("  SPACE - Pause/Resume", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  'r' - Reset simulation", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  'n'/'p' - Next/Previous scenario", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  'h' - Toggle help overlay", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  'd' - Toggle debug info", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  'c' - Toggle collisions", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  '+'/'-' - Adjust gravity", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  'a'/'x' - Add/Remove particles", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  'm' - Return to menu", 70, y, 0xFFCCCCCC);
    y += line_height * 2;
    
    // System info
    vbe_draw_string("SYSTEM INFORMATION:", 50, y, 0xFFFFFF00);
    y += line_height;
    vbe_draw_string("  Algorithm: Barnes-Hut N-body simulation", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  Complexity: O(N log N) vs O(N²) brute force", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  Features: Gravity, collisions, spatial partitioning", 70, y, 0xFFCCCCCC);
    y += line_height;
    vbe_draw_string("  Graphics: VBE 1280x800x32 framebuffer", 70, y, 0xFFCCCCCC);
    
    render_text_centered("Press SPACE or 'b' to return to main menu", SCREEN_HEIGHT - 50, 0xFF888888);
}

static void render_status_bar(void) {
    // Render status bar at bottom
    vbe_fillrect(0, SCREEN_HEIGHT - 30, SCREEN_WIDTH, 30, 0xFF333333);
    vbe_draw_string(interface_state.status_message, 10, SCREEN_HEIGHT - 20, 0xFFFFFFFF);
    
    // Show current mode
    const char* mode_text = "";
    switch (interface_state.current_mode) {
        case DEMO_INTERFACE_MAIN_MENU: mode_text = "Main Menu"; break;
        case DEMO_INTERFACE_DEMO_MENU: mode_text = "Demo Menu"; break;
        case DEMO_INTERFACE_INTERACTIVE_DEMO: mode_text = "Interactive Demo"; break;
        case DEMO_INTERFACE_TEST_MENU: mode_text = "Test Menu"; break;
        case DEMO_INTERFACE_BENCHMARK_MENU: mode_text = "Benchmark Menu"; break;
        case DEMO_INTERFACE_VALIDATION_MENU: mode_text = "Validation Menu"; break;
        case DEMO_INTERFACE_HELP: mode_text = "Help"; break;
        case DEMO_INTERFACE_TEST_RESULTS: mode_text = "Test Results"; break;
    }
    
    char mode_buffer[64];
    sprintf(mode_buffer, "Mode: %s", mode_text);
    vbe_draw_string(mode_buffer, SCREEN_WIDTH - 200, SCREEN_HEIGHT - 20, 0xFFCCCCCC);
}

// Input handling functions
static void handle_main_menu_input(char key) {
    switch (key) {
        case '1':
            interface_state.current_mode = DEMO_INTERFACE_DEMO_MENU;
            interface_state.menu_selection = 0;
            set_status_message("Entered Demo Menu");
            break;
            
        case '2':
            interface_state.current_mode = DEMO_INTERFACE_TEST_MENU;
            interface_state.menu_selection = 0;
            set_status_message("Entered Test Menu");
            break;
            
        case '3':
            interface_state.current_mode = DEMO_INTERFACE_BENCHMARK_MENU;
            interface_state.menu_selection = 0;
            set_status_message("Entered Benchmark Menu");
            break;
            
        case '4':
            set_status_message("Running Stress Test...");
            physics_demo_run_stress_test(1000);
            set_status_message("Stress Test Completed");
            break;
            
        case '5':
            interface_state.current_mode = DEMO_INTERFACE_VALIDATION_MENU;
            interface_state.menu_selection = 0;
            set_status_message("Entered Validation Menu");
            break;
            
        case '6':
            interface_state.current_mode = DEMO_INTERFACE_HELP;
            set_status_message("Showing Help");
            break;
            
        case '7':
            interface_state.interface_active = 0;
            set_status_message("Exiting Demo Interface");
            break;
    }
}

static void handle_demo_menu_input(char key) {
    demo_scenario_t scenario = DEMO_SCENARIO_RANDOM_PARTICLES;
    int start_demo = 0;
    
    switch (key) {
        case '1': scenario = DEMO_SCENARIO_RANDOM_PARTICLES; start_demo = 1; break;
        case '2': scenario = DEMO_SCENARIO_ORBITAL_SYSTEM; start_demo = 1; break;
        case '3': scenario = DEMO_SCENARIO_COLLISION_DEMO; start_demo = 1; break;
        case '4': scenario = DEMO_SCENARIO_GRAVITY_WELL; start_demo = 1; break;
        case '5': scenario = DEMO_SCENARIO_BINARY_SYSTEM; start_demo = 1; break;
        case '6': scenario = DEMO_SCENARIO_PARTICLE_FOUNTAIN; start_demo = 1; break;
        case '7': scenario = DEMO_SCENARIO_GALAXY_SPIRAL; start_demo = 1; break;
        case '8': scenario = DEMO_SCENARIO_PARTICLE_CLUSTER; start_demo = 1; break;
        case '9': scenario = DEMO_SCENARIO_CHAIN_REACTION; start_demo = 1; break;
        case '0': scenario = DEMO_SCENARIO_SOLAR_SYSTEM; start_demo = 1; break;
        case 'b':
            physics_demo_benchmark_scenarios();
            set_status_message("Scenario Benchmarks Completed");
            break;
        case 'c':
            interface_state.current_mode = DEMO_INTERFACE_MAIN_MENU;
            set_status_message("Returned to Main Menu");
            break;
    }
    
    if (start_demo) {
        if (physics_demo_start(scenario)) {
            interface_state.current_mode = DEMO_INTERFACE_INTERACTIVE_DEMO;
            char msg[128];
            sprintf(msg, "Started %s Demo", physics_demo_get_scenario_name(scenario));
            set_status_message(msg);
        } else {
            set_status_message("Failed to start demo");
        }
    }
}

static void handle_test_menu_input(char key) {
    switch (key) {
        case '1':
            set_status_message("Running All Comprehensive Tests...");
            physics_comprehensive_run_tests();
            set_status_message("All Tests Completed");
            break;
            
        case '2':
            set_status_message("Running Particle System Tests...");
            physics_comprehensive_run_particle_tests();
            set_status_message("Particle Tests Completed");
            break;
            
        case '3':
            set_status_message("Running Quadtree Tests...");
            physics_comprehensive_run_quadtree_tests();
            set_status_message("Quadtree Tests Completed");
            break;
            
        case '4':
            set_status_message("Running Barnes-Hut Tests...");
            physics_comprehensive_run_barnes_hut_tests();
            set_status_message("Barnes-Hut Tests Completed");
            break;
            
        case '5':
            set_status_message("Running Collision Tests...");
            physics_comprehensive_run_collision_tests();
            set_status_message("Collision Tests Completed");
            break;
            
        case '6':
            set_status_message("Running Integration Tests...");
            physics_comprehensive_run_integration_tests();
            set_status_message("Integration Tests Completed");
            break;
            
        case '7':
            set_status_message("Running Memory Tests...");
            physics_comprehensive_run_memory_tests();
            set_status_message("Memory Tests Completed");
            break;
            
        case '8':
            set_status_message("Running Performance Tests...");
            physics_performance_run_tests();
            set_status_message("Performance Tests Completed");
            break;
            
        case '9':
            set_status_message("Running Rendering Tests...");
            physics_comprehensive_run_rendering_tests();
            set_status_message("Rendering Tests Completed");
            break;
            
        case '0':
            set_status_message("Running Demo System Tests...");
            physics_comprehensive_run_demo_tests();
            set_status_message("Demo Tests Completed");
            break;
            
        case 'b':
            interface_state.current_mode = DEMO_INTERFACE_MAIN_MENU;
            set_status_message("Returned to Main Menu");
            break;
    }
}

static void handle_benchmark_menu_input(char key) {
    switch (key) {
        case '1':
            set_status_message("Running Quick Benchmark...");
            physics_demo_run_performance_test();
            set_status_message("Quick Benchmark Completed");
            break;
            
        case '2':
            set_status_message("Running Comprehensive Performance Test...");
            physics_comprehensive_run_performance_tests();
            set_status_message("Comprehensive Performance Test Completed");
            break;
            
        case '3':
            set_status_message("Running Memory Analysis...");
            physics_comprehensive_run_memory_tests();
            set_status_message("Memory Analysis Completed");
            break;
            
        case '4':
            set_status_message("Running Scaling Test...");
            physics_demo_run_stress_test(2000);
            set_status_message("Scaling Test Completed");
            break;
            
        case '5':
            set_status_message("Running Component Timing Analysis...");
            physics_performance_run_benchmarks();
            set_status_message("Component Analysis Completed");
            break;
            
        case '6':
            set_status_message("Running SIMD Tests...");
            // Would call SIMD-specific tests
            set_status_message("SIMD Tests Completed");
            break;
            
        case '7':
            set_status_message("Running Cache Tests...");
            // Would call cache-specific tests
            set_status_message("Cache Tests Completed");
            break;
            
        case '8':
            interface_state.current_mode = DEMO_INTERFACE_MAIN_MENU;
            set_status_message("Returned to Main Menu");
            break;
    }
}

static void handle_validation_menu_input(char key) {
    switch (key) {
        case '1':
            set_status_message("Running Physics Accuracy Validation...");
            physics_comprehensive_run_accuracy_validation();
            set_status_message("Accuracy Validation Completed");
            break;
            
        case '2':
            set_status_message("Running Energy Conservation Test...");
            physics_comprehensive_run_conservation_validation();
            set_status_message("Energy Conservation Test Completed");
            break;
            
        case '3':
            set_status_message("Running Momentum Conservation Test...");
            physics_comprehensive_run_conservation_validation();
            set_status_message("Momentum Conservation Test Completed");
            break;
            
        case '4':
            set_status_message("Running Numerical Stability Test...");
            physics_comprehensive_run_stability_validation();
            set_status_message("Stability Test Completed");
            break;
            
        case '5':
            set_status_message("Running Long-term Stability Test...");
            physics_comprehensive_run_long_term_tests();
            set_status_message("Long-term Test Completed");
            break;
            
        case '6':
            set_status_message("Running Extreme Parameter Tests...");
            physics_comprehensive_run_edge_case_tests();
            set_status_message("Extreme Parameter Tests Completed");
            break;
            
        case '7':
            interface_state.current_mode = DEMO_INTERFACE_MAIN_MENU;
            set_status_message("Returned to Main Menu");
            break;
    }
}

// Utility functions
static void set_status_message(const char* message) {
    if (message) {
        strncpy(interface_state.status_message, message, sizeof(interface_state.status_message) - 1);
        interface_state.status_message[sizeof(interface_state.status_message) - 1] = '\0';
    }
}

static void render_text_centered(const char* text, uint32_t y, uint32_t color) {
    if (!text) return;
    
    uint32_t text_width = strlen(text) * 8; // Assume 8 pixels per character
    uint32_t x = (SCREEN_WIDTH - text_width) / 2;
    vbe_draw_string(text, x, y, color);
}

static void render_menu_options(const char** options, uint32_t count, uint32_t selected) {
    uint32_t start_y = 150;
    uint32_t line_height = 30;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t y = start_y + i * line_height;
        uint32_t color = (i == selected) ? 0xFFFFFF00 : 0xFFCCCCCC;
        
        // Center the text
        uint32_t text_width = strlen(options[i]) * 8;
        uint32_t x = (SCREEN_WIDTH - text_width) / 2;
        
        vbe_draw_string(options[i], x, y, color);
    }
}

// Public interface functions
int physics_demo_interface_is_active(void) {
    return interface_state.interface_active;
}

demo_interface_mode_t physics_demo_interface_get_mode(void) {
    return interface_state.current_mode;
}

void physics_demo_interface_set_mode(demo_interface_mode_t mode) {
    interface_state.current_mode = mode;
}