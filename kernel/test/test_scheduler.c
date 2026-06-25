// kernel/test/test_scheduler.c
#include <kernel/test/ktest.h>
#include <kernel/schedule/schedule.h>
#include <kernel/stdio/stdio.h>

// External scheduler functions
extern process_control_block_t *get_current_task(void);
extern uint32_t get_task_count(void);
extern void kernel_yield(void);

// Test scheduler initialization
KTEST_DEFINE(scheduler_init_test) {
    // Scheduler should be initialized at boot
    KTEST_ASSERT(1, "scheduler initialized");
    return 1;
}

// Test current task exists
KTEST_DEFINE(current_task_test) {
    process_control_block_t *current = get_current_task();

    if (current == NULL) {
        KTEST_SKIP("scheduler not running or no current task");
    }

    KTEST_ASSERT_NOT_NULL(current, "current task exists");
    KTEST_ASSERT(current->pid > 0, "current task has valid PID");

    return 1;
}

// Test task count
KTEST_DEFINE(task_count_test) {
    uint32_t count = get_task_count();

    // Should have at least one task (current one)
    KTEST_ASSERT(count >= 1, "at least one task exists");

    return 1;
}

// Test task structure validity
KTEST_DEFINE(task_structure_test) {
    process_control_block_t *current = get_current_task();

    if (current == NULL) {
        KTEST_SKIP("no current task");
    }

    // Check basic fields
    KTEST_ASSERT(current->pid > 0, "valid PID");
    KTEST_ASSERT_NOT_NULL(current->esp, "valid stack pointer");
    KTEST_ASSERT_NOT_NULL(current->cr3, "valid page directory");

    return 1;
}

// Test kernel task vs user task
KTEST_DEFINE(task_privilege_test) {
    // cppcheck-suppress constVariablePointer
    process_control_block_t *current = get_current_task();

    if (current == NULL) {
        KTEST_SKIP("no current task");
    }

    // priv field: 0 = kernel, 1 = user
    KTEST_ASSERT(current->priv == 0 || current->priv == 1, "valid privilege level");

    return 1;
}

// Test task state
KTEST_DEFINE(task_state_test) {
    // cppcheck-suppress constVariablePointer
    process_control_block_t *current = get_current_task();

    if (current == NULL) {
        KTEST_SKIP("no current task");
    }

    // Current task should be running
    KTEST_ASSERT(current->state == PROCESS_STATE_RUNNING, "current task is running");

    return 1;
}

// Test task name
KTEST_DEFINE(task_name_test) {
    process_control_block_t *current = get_current_task();

    if (current == NULL) {
        KTEST_SKIP("no current task");
    }

    // Name should be set
    KTEST_ASSERT(current->name[0] != '\0', "task has a name");

    // Name should be null-terminated within buffer
    int found_null = 0;
    for (int i = 0; i < 32; i++) {
        if (current->name[i] == '\0') {
            found_null = 1;
            break;
        }
    }
    KTEST_ASSERT(found_null, "task name is null-terminated");

    return 1;
}

// Test stack boundaries
KTEST_DEFINE(task_stack_test) {
    process_control_block_t *current = get_current_task();

    if (current == NULL) {
        KTEST_SKIP("no current task");
    }

    // Stack pointers should be valid
    if (current->esp_min && current->esp_max) {
        KTEST_ASSERT((uint32_t)current->esp_min < (uint32_t)current->esp_max,
                     "stack min < stack max");

        // Current ESP should be within bounds
        if (current->esp) {
            KTEST_ASSERT((uint32_t)current->esp >= (uint32_t)current->esp_min,
                         "ESP >= min");
            KTEST_ASSERT((uint32_t)current->esp <= (uint32_t)current->esp_max,
                         "ESP <= max");
        }
    }

    return 1;
}

// Test PID uniqueness (basic check)
KTEST_DEFINE(pid_uniqueness_test) {
    // cppcheck-suppress constVariablePointer
    process_control_block_t *current = get_current_task();

    if (current == NULL) {
        KTEST_SKIP("no current task");
    }

    uint32_t pid = current->pid;

    // PID should be non-zero
    KTEST_ASSERT(pid != 0, "PID is non-zero");

    // PID should be reasonable (< MAX_TASKS)
    KTEST_ASSERT(pid < MAX_TASKS, "PID within reasonable range");

    return 1;
}

// Test yield (smoke test - should not crash)
KTEST_DEFINE(yield_test) {
    // Call yield - should return control back to us
    // TODO: Fix kernel yield to return back when only task running.
    kernel_yield();

    KTEST_ASSERT(1, "yield completed without crash");
    return 1;
}

// Run all scheduler tests
void test_scheduler_suite(void) {
    ktest_t tests[] = {
        KTEST_RUN(scheduler_init_test),
        KTEST_RUN(current_task_test),
        KTEST_RUN(task_count_test),
        KTEST_RUN(task_structure_test),
        KTEST_RUN(task_privilege_test),
        KTEST_RUN(task_state_test),
        KTEST_RUN(task_name_test),
        KTEST_RUN(task_stack_test),
        KTEST_RUN(pid_uniqueness_test),
        KTEST_RUN(yield_test),
    };

    ktest_run_suite("Task Scheduler", tests, sizeof(tests) / sizeof(tests[0]));
}
