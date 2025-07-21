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
#include "../gdt.h"

process_control_block_t *current_task = NULL;
process_control_block_t *task_list    = NULL;
lock_t *stdin_lock;
static uint32_t next_pid = 0;
static uint32_t next_user_stack = USER_STACK_TOP;
static uint32_t next_kernel_stack = KERNEL_STACK_TOP;
volatile uint32_t preempt_count = 0;
volatile uint8_t pending_schedule = 0;

// TODO: I should probably not scatter a repeat function but fuck it later issue
// TODO: What I meant by this is this is probably better off defined later elsewhere, sorry for bed england.
//       Don't care, will do it later :troll:
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

void preempt_disable() {
    preempt_count++;
}

void preempt_enable() {
    if (preempt_count > 0)
        preempt_count--;
}

void *alloc_user_stack(void) {
    if (next_user_stack < USER_STACK_BOTTOM + USER_STACK_SIZE) {
        // TODO: Maybe try terminating some tasks or deny creating a new task.
        kernel_panic("alloc_user_stack: out of user stack space!");
        return NULL;
    }

    next_user_stack -= USER_STACK_SIZE;

    return (void *)next_user_stack;
}

void *alloc_kernel_stack(void) {
    if (next_kernel_stack < KERNEL_STACK_BOTTOM + KERNEL_STACK_SIZE) {
        // TODO: !!
        kernel_panic("alloc_kernel_stack: out of kernel stack space!");
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

    task_lock_init(stdin_lock, 1);
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
            preempt_enable();

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
    pcb->esp_min = stk_top;
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
    current_task->state = PROCESS_STATE_BLOCKED;
    task_yield(1);
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
    task_yield(1);
}

/**
 * @brief Yield control from the current kernel mode task and switches to the next task. Kernel mode tasks only.
 */
__attribute__((naked)) 
void kernel_yield(void) {
    // TODO: maybe better off doing all of this in asm

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

static void enqueue_waiter(lock_t *lock, process_control_block_t *pcb) {
    wait_node_t *node = kernel_malloc(sizeof(*node));
    node->task = pcb;
    node->next = NULL;
    if (lock->waiters_tail) {
        lock->waiters_tail->next = node;
        lock->waiters_tail = node;
    } else {
        lock->waiters_head = lock->waiters_tail = node;
    }
}

process_control_block_t *dequeue_waiter(lock_t *lock) {
    if (!lock->waiters_head) return NULL;
    wait_node_t *node = lock->waiters_head;
    process_control_block_t *pcb = node->task;
    lock->waiters_head = node->next;
    if (!lock->waiters_head)
        lock->waiters_tail = NULL;
    kernel_free(node);
    return pcb;
}

void task_lock_init(lock_t *lock, uint8_t block_on_hold) {
    lock->held          = 0;
    lock->block_on_hold = block_on_hold;
    lock->owner         = NULL;
    lock->waiters_head  = NULL;
    lock->waiters_tail  = NULL;
}

void task_lock_acquire(lock_t *lock) {
    if (!lock->held) {
        lock->held = 1;
        lock->owner = current_task;
        if (lock->block_on_hold) {
            task_block();
        }
    } else {
        enqueue_waiter(lock, current_task);
        task_block();
    }
}

void task_lock_release(lock_t *lock) {
    process_control_block_t *owner = lock->owner;
    if (lock->held) {
        kernel_free(owner->ipc_ptr);
        process_control_block_t *next = dequeue_waiter(lock);
        if (next) {
            lock->owner = next;
            lock->held  = 1;
            if (lock->block_on_hold) {
                task_unblock(owner);
            }
        } else {
            lock->held = 0;
            lock->owner = NULL;
            if (lock->block_on_hold) {
                task_unblock(owner);
            };
        }
    }
}

process_control_block_t* task_fork(process_control_block_t *parent) {
    // TODO: fix for kernel mode, if i should support fork in kmode in the first place (lol)
    process_control_block_t *pcb = (process_control_block_t*)kernel_malloc(sizeof(process_control_block_t));
    memcpy(pcb, parent, sizeof(process_control_block_t));
    memcpy(pcb->processor_context, parent->processor_context, sizeof(processor_context_t));
    pcb->state = PROCESS_STATE_READY;
    pcb->pid   = next_pid++;

    // create stack
    uint8_t *stack;
    uint32_t stk_top;
    uint32_t ebp;
    if (pcb->priv == CPU_USER_MODE) {
        stack = (uint8_t*)alloc_user_stack();
    } else {
        stack = (uint8_t*)alloc_kernel_stack();
    }
    memset(stack, 0, sizeof(*stack));

    stk_top = (uint32_t)parent->processor_context->esp_at_trap - USER_STACK_SIZE;
    ebp = (uint32_t)parent->processor_context->ebp - USER_STACK_SIZE;

    printf("THE GHOST OF TERRY DAVIS SAYS: esp:%p ebp:%p, retard: esp:%p, ebp:%p\n",stk_top,ebp,parent->processor_context->esp_at_trap,parent->processor_context->ebp);

    pcb->esp = (uint32_t*)stk_top;
    pcb->esp_max = stack;
    pcb->processor_context->esp_at_trap = (uint32_t)stk_top;
    pcb->processor_context->ebp = (uint32_t)ebp;

    memcpy(pcb->esp_max, parent->esp_max, USER_STACK_SIZE);

    printfs(PRINT_STATUS_DEBUG,"Forking task '%s', esp=%p, esp0=%p\n", pcb->name, pcb->esp,pcb->esp0);

    return pcb;
}