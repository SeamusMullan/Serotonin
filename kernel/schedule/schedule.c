/*
 * schedule.c
 * Serotonin Kernel Scheduler
*/

#include "schedule.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../string.h"
#include "../paging.h"
#include "../stdio/stdio.h"
#include "../io/io.h"
#include "../video/vbe/vbe.h"

process_control_block_t *current_task = NULL;
process_control_block_t *task_list    = NULL;
static uint32_t next_pid = 0;
static uint32_t next_user_stack = USER_STACK_TOP;
volatile uint32_t preempt_count = 0;
volatile uint8_t pending_schedule = 0;

// TODO: I should probably not scatter a repeat function but fuck it later issue
static inline void* get_esp(void) {
    void* esp;
    asm volatile("mov %%esp, %0" : "=r"(esp));
    return esp;
}

static inline void* read_cr3_register(void) {
    void* cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void *alloc_user_stack(void) {
    if (next_user_stack < USER_STACK_BOTTOM + USER_STACK_SIZE) {
        kernel_panic("alloc_user_stack: out of user stack space!");
        return NULL;
    }

    next_user_stack -= USER_STACK_SIZE;

    return (void *)next_user_stack;
}

/**
 * @brief Disables interrupts to lock the scheduler.
 */
void lock_scheduler(void) {
    if (multitasking_ready == 0)
        return;
    clear_interrupts();
}

/**
 * @brief Enables interrupts to unlock the scheduler.
 */
void unlock_scheduler(void) {
    if (multitasking_ready == 0)
        return;
    enable_interrupts();
}

void preempt_enable(void) {
    preempt_count--;
    if (pending_schedule == 1 && preempt_count == 0&& current_task->state == PROCESS_STATE_RUNNING) {
        pending_schedule = 0;
        task_yield(0);
    }
}

void preempt_disable(void) {
    preempt_count++;
}

/**
 * @brief Initializes multitasking by creating the initial kernel task.
 */
void multitasking_init(void) {
    process_control_block_t *init_task = (process_control_block_t*)kernel_malloc(sizeof(process_control_block_t));
    memset(init_task, 0, sizeof(*init_task));

    // populate fields
    init_task->pid    = next_pid++;
    init_task->esp    = get_esp();
    init_task->esp0   = get_esp();
    init_task->cr3    = read_cr3_register();
    init_task->state  = PROCESS_STATE_BLOCKED;
    strncpy(init_task->name, "kernel_init", 32);

    // single‐element list
    task_list             = init_task;
    current_task          = init_task;
}


void multitasking_make_ready(void) {
    multitasking_ready = 1;
}

/**
 * @brief Yields control from the current task and switches to the next ready task.
 * @param irq The IRQ number that caused the yield.
 */
__attribute__((noreturn)) void task_yield(int irq) {
    void *esp;


    asm volatile ("movl %%esp, %0"
        : "=r"(esp)
        :
        :
    );
    lock_scheduler();
    //preempt_disable();

    if (current_task->state == PROCESS_STATE_RUNNING) {
        current_task->state = PROCESS_STATE_READY;
        if (current_task->priv == CPU_KERNEL_MODE) {
            current_task->esp = esp;
        }
        enqueue(current_task);
    }

    process_control_block_t* next = NULL;
    while ((next = dequeue()) != NULL) {
        if (next->state == PROCESS_STATE_READY) {
            printf("found next: %p, name: %s, ring:%d, entry:%p, esp:%p\n",next, next->name,next->priv,next->entry,next->esp);
            // found someone we can switch into
            next->state = PROCESS_STATE_RUNNING;
            unlock_scheduler();
            //preempt_enable();

            // if nothing is pending, switch
            if (irq == 1)
                switch_task_iret(next);
            switch_task(next);
            __builtin_unreachable();
        }
    }

    kernel_panic("task_yield: no valid task to switch to");
    __builtin_unreachable();
}

/**
 * @brief Terminates the currently running task and switches to the next one.
 */
void task_exit(void) {
    printfs(PRINT_STATUS_DEBUG, "task_exit: Task %s (pid=%u) exited\n", current_task->name, current_task->pid);
    current_task->state = PROCESS_STATE_TERMINATED;

    task_yield(0);  // pick the next runnable task
    kernel_panic("task_exit: nothing to switch to");
}

/**
 * @brief Creates a new task with the given entry point and name.
 * @param entry Pointer to the task's entry function.
 * @param name  Name of the task.
 * @return Pointer to the newly created process control block.
 */
process_control_block_t* task_create(void (*entry)(void), const char *name, uint8_t priv) {
    // alloc and init pcb
    process_control_block_t *pcb = (process_control_block_t*)kernel_malloc(sizeof(*pcb));
    memset(pcb, 0, sizeof(*pcb));
    pcb->pid   = next_pid++;
    pcb->cr3   = read_cr3_register();
    pcb->state = PROCESS_STATE_READY;
    pcb->started = 0;
    pcb->priv  = priv;
    strncpy(pcb->name, name, sizeof(pcb->name)-1);

    // create stack
    uint8_t *stack;
    uint32_t *stk_top;
    if (priv == CPU_USER_MODE) {
        stack = (uint8_t*)alloc_user_stack();
        stk_top = (uint32_t*)(stack + USER_STACK_SIZE);
    } else {
        stack = (uint8_t*)kernel_malloc(KERNEL_STACK_SIZE);
        stk_top = (uint32_t*)(stack + KERNEL_STACK_SIZE);
    }
    memset(stack, 0, sizeof(*stack));

    pcb->esp = stk_top;
    pcb->esp0 = get_esp();
    pcb->entry = entry;

    printfs(PRINT_STATUS_DEBUG,"Creating task '%s', esp=%p, esp0=%p\n", name, pcb->esp,pcb->esp0);

    return pcb;
}

/**
 * @brief Adds a task to the scheduler's queue.
 * @param pcb Pointer to the task's process control block.
 */
void enqueue(process_control_block_t* pcb) {
    lock_scheduler();

    pcb->next = NULL;

    // insert into scheduler
    if (!task_list) {
        task_list = pcb;
    } else {
        process_control_block_t *tail = task_list;
        while (tail->next)
            tail = tail->next;
        tail->next = pcb;
    }

    unlock_scheduler();
}

/**
 * @brief Removes and returns the next task from the scheduler's queue.
 * @return Pointer to the dequeued process control block.
 */
process_control_block_t* dequeue() {
    if (!task_list)
        return NULL;

    process_control_block_t* head = task_list;
    task_list = task_list->next;
    head->next = NULL;
    return head;
}

/**
 * @brief Sets the state of the specified task.
 * @param pcb Pointer to the task's process control block.
 * @param state New state to set for the task.
 */
void task_set_state(process_control_block_t *pcb, int state) {
    lock_scheduler();
    pcb->state = state;
    unlock_scheduler();
}

/**
 * @brief Blocks the current task and yields to the next one.
 */
void task_block(void) {
    task_set_state(current_task,PROCESS_STATE_BLOCKED);
    task_yield(0);
    __builtin_unreachable();
}

/**
 * @brief Unblocks the specified task and makes it ready to run.
 * @param pcb Pointer to the task's process control block.
 */
void task_unblock(process_control_block_t *pcb) {
    lock_scheduler();
    pcb->state = PROCESS_STATE_READY;
    enqueue(pcb);
    unlock_scheduler();
}