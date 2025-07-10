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
static uint32_t next_kernel_stack = KERNEL_STACK_TOP;
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

void *alloc_kernel_stack(void) {
    if (next_kernel_stack < KERNEL_STACK_BOTTOM + KERNEL_STACK_SIZE) {
        kernel_panic("alloc_kernel_stack: out of user stack space!");
        return NULL;
    }

    next_kernel_stack -= KERNEL_STACK_SIZE;

    return (void *)next_kernel_stack;
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

/**
 * @brief Initializes multitasking by creating the initial kernel task.
 */
void multitasking_init(void) {
    process_control_block_t *init_task = (process_control_block_t*)kernel_malloc(sizeof(process_control_block_t));
    memset(init_task, 0, sizeof(*init_task));

    // populate fields
    init_task->pid     = next_pid++;
    init_task->esp     = get_esp();
    init_task->esp_max = (void*)0x00200000;
    init_task->esp0    = get_esp();
    init_task->cr3     = read_cr3_register();
    init_task->state   = PROCESS_STATE_BLOCKED;
    strncpy(init_task->name, "Serotonin Kernel", 32);

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
void task_yield(int irq) {
    lock_scheduler();

    if (current_task->state == PROCESS_STATE_RUNNING) {
        if ((unsigned int)current_task->esp < (unsigned int)current_task->esp_max) {
            printfs(PRINT_STATUS_ERROR, "Stack overflow detected in task '%s' (attempted esp=%p, esp_max=%p)\n", current_task->name, current_task->esp,current_task->esp_max);
            task_exit(EXIT_SIGSEGV);
        }
        current_task->state = PROCESS_STATE_READY;
        enqueue(current_task);
    }

    process_control_block_t* next = NULL;
    while ((next = dequeue()) != NULL) {
        if (next->state == PROCESS_STATE_READY) {
            // printf("found next: %p, name: %s, ring:%d, entry:%p, esp:%p\n",next, next->name,next->priv,next->entry,next->esp);
            // found someone we can switch into
            next->state = PROCESS_STATE_RUNNING;
            unlock_scheduler();

            // if nothing is pending, switch
            if (irq == 1) {
                switch_task_iret(next);
            }
            switch_task(next);
        }
    }

    kernel_panic("task_yield: no valid task to switch to");
}

/**
 * @brief Terminates the currently running task and switches to the next one.
 */
void task_exit(uint8_t exit) {
    printfs(PRINT_STATUS_DEBUG, "task_exit: Task %s (pid=%u) exited:%s\n", current_task->name, current_task->pid,to_signal_name(exit));
    current_task->state = PROCESS_STATE_TERMINATED;
    current_task->signal = exit;

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

    processor_context_t *ctx = (processor_context_t *)kernel_malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    pcb->processor_context = ctx;

    // create stack
    uint8_t *stack;
    uint32_t *stk_top;
    if (priv == CPU_USER_MODE) {
        stack = (uint8_t*)alloc_user_stack();
        stk_top = (uint32_t*)(stack + USER_STACK_SIZE);
        pcb->processor_context->ds          = 0x23;
        pcb->processor_context->es          = 0x23;
        pcb->processor_context->fs          = 0x23;
        pcb->processor_context->gs          = 0x23;
        pcb->processor_context->ss          = 0x23; 
        pcb->processor_context->esp_at_trap = (uint32_t)stk_top;
        pcb->processor_context->stub_eflags = 0x00000202;
        pcb->processor_context->eflags      = 0x00000202;
        pcb->processor_context->cs          = 0x1B; 
        pcb->processor_context->eip         = (uint32_t)entry;
    } else {
        stack = (uint8_t*)alloc_kernel_stack();
        stk_top = (uint32_t*)(stack + KERNEL_STACK_SIZE);
    }
    memset(stack, 0, sizeof(*stack));

    pcb->esp = stk_top;
    pcb->esp0 = get_esp();
    pcb->esp_max = stack;
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

/**
 * @brief Yield control from the current kernel mode task and switches to the next task. Kernel mode tasks only.
 */
__attribute__((naked)) 
void kernel_yield(void) {
    void *esp;
    void *ebx;
    void *ebp;
    void *esi;
    void *edi;
    void *entry;
    asm volatile (
        "movl 0(%%esp), %0\n\t"
        "movl %%esp, %1\n\t"
        "movl %%ebx, %2\n\t"
        "movl %%ebp, %3\n\t"
        "movl %%esi, %4\n\t"
        "movl %%edi, %5\n\t"
        : "=r"(entry),
          "=r"(esp),
          "=r"(ebx),
          "=r"(ebp),
          "=r"(esi),
          "=r"(edi)
        :
        :
    );
    
    current_task->esp = (void*)((unsigned int)esp+0x4); // (return addr was pushed to stack)
    current_task->ebx = ebx;
    current_task->esi = esi;
    current_task->edi = edi;
    current_task->ebp = ebp;  
    current_task->entry = entry;
    
    task_yield(0);

    return;
}