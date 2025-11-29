/*
 * schedule.c
 * Serotonin Kernel Scheduler
*/

#include "schedule.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../string.h"
#include "../vmm/paging_init.h"
#include "../vmm/vmm.h"
#include "../stdio/stdio.h"
#include "../io/io.h"
#include "../video/vbe/vbe.h"
#include "../gdt.h"

process_control_block_t *current_task = NULL;
process_control_block_t *task_list    = NULL;
process_control_block_t *init_task    = NULL;
lock_t *stdin_lock;
static process_control_block_t *runqueue[MAX_TASKS];
static int rq_head = 0;
static int rq_tail = 0;
static uint32_t next_pid = 0;
static uint32_t next_user_stack = USER_STACK_TOP;
static uint32_t next_kernel_stack = KERNEL_STACK_TOP;
static prio_queue_t prio_q[MAX_PRIORITY];
static uint8_t top_bitmap;
static uint32_t prio_bitmap[8];
volatile uint32_t preempt_count = 0;
volatile uint32_t lock_count = 0;
volatile uint8_t pending_schedule = 0;
static __attribute__((aligned(16))) fpu_fxsave_area_t fx_clean;

static inline void bm_set(uint8_t p) {
    uint8_t word = p >> 5; // div by 32, select which prio_bitmap word
    uint8_t bit = p & 31; // mod 32, select bit inside word

    prio_bitmap[word] |= (1u << bit); // mark this priority as ready
    top_bitmap |= (uint8_t)(1u << word); // mark group as nonempty
}

static inline void bm_clear(uint8_t p) {
    uint8_t word = p >> 5;
    uint8_t bit = p & 31;

    prio_bitmap[word] &= ~(1u << bit); // clear priority bit
    if (prio_bitmap[word] == 0) {
        top_bitmap &= (uint8_t)~(1u << word); // if the group is empty, clear its bit
    }
}

static inline int highest_ready_prio(void) {
    uint8_t tb = top_bitmap;
    if (!tb) return -1;

    // find highest nonempty 32 bit group
    int word = 31 - __builtin_clz((uint8_t)tb);

    // find highest priorty inside group
    uint32_t w = prio_bitmap[word];
    int bit   = 31 - __builtin_clz(w);

    // return the priority number from group index
    return (word << 5) | bit;
}


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

static inline int rq_next(int i) {
    return (i + 1) % MAX_TASKS;
}

static inline int runqueue_is_empty(void) {
    return rq_head == rq_tail;
}

void enqueue_task_list(process_control_block_t* pcb) {
    if (!task_list) {
        task_list = pcb;
    } else {
        process_control_block_t *tail = task_list;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = pcb;
    }
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
    lock_count++;
    clear_interrupts();
}

/**
 * @brief Enables interrupts to unlock the scheduler.
 */
void unlock_scheduler(void) {
    if (multitasking_ready == 0)
        return;
    lock_count--;
    if (!lock_count)
        enable_interrupts();
}

void fpu_get_init_state(void) {
    asm volatile("fxsave %0" : "=m"(fx_clean));
}

/**
 * @brief Initializes multitasking by creating the initial kernel task.
 */
void multitasking_init(void) {
    init_task = (process_control_block_t*)kernel_malloc_align(PCB_ALIGNMENT, sizeof(process_control_block_t));
    memset(init_task, 0, sizeof(*init_task));

    init_task->pid     = next_pid++;
    init_task->esp     = get_esp();
    init_task->esp_max = (void*)0x00200000; // piratesoftware
    init_task->esp0    = get_esp();
    init_task->cr3     = read_cr3_register();
    init_task->state   = PROCESS_STATE_BLOCKED;
    strncpy(init_task->name, "Serotonin Kernel", 32);

    task_list             = init_task;
    current_task          = init_task;

    fpu_get_init_state();

    stdin_lock = kernel_malloc(sizeof(lock_t));
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
        if ((unsigned int)current_task->esp < (unsigned int)current_task->esp_min && current_task->priv == CPU_USER_MODE) {
            printfs(PRINT_STATUS_ERROR, "Stack overflow detected in task '%s' (attempted esp=%p, esp_max=%p)\n", current_task->name, current_task->esp,current_task->esp_max);
            task_exit(EXIT_SIGSEGV);
        }
        current_task->state = PROCESS_STATE_READY;
        enqueue(current_task);
    }

    process_control_block_t* next = NULL;
    while ((next = dequeue()) != NULL) {
        if (next->state == PROCESS_STATE_READY) {
            lock_scheduler();
            if (next->priv == CPU_USER_MODE) {
                switch_address_space(next->address_space);
                task_ipc_deliver_signals(next, next->processor_context);
            }
            // found someone we can switch into
            next->state = PROCESS_STATE_RUNNING;
            unlock_scheduler();
            preempt_enable();

            // if nothing is pending, switch
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

    process_control_block_t *waiter = task_list;
    while (waiter) {
        if (waiter->waiting_on == (int)current_task->pid) {
            waiter->waiting_on = -1;
            switch_address_space(waiter->address_space);
            memset(waiter->status_ptr, exit, sizeof(uint8_t));
            waiter->state = PROCESS_STATE_READY;
            waiter->processor_context->eax = exit;
            enqueue(waiter);
            break;
        }
        waiter = waiter->next;
    }

    kernel_free_align(current_task->processor_context);
    kernel_free_align(current_task);

    task_yield(0);  // pick the next runnable task
    kernel_panic("task_exit: nothing to switch to");
}

/**
 * @brief Creates a new task with the given entry point and name.
 * @param entry Pointer to the task's entry function.
 * @param name  Name of the task.
 * @return Pointer to the newly created process control block.
 */
process_control_block_t* task_create(void (*entry)(void), const char *name, uint8_t priv, uint8_t prio) {
    // alloc and init pcb
    process_control_block_t *pcb = (process_control_block_t*)kernel_malloc_align(PCB_ALIGNMENT,sizeof(process_control_block_t));
    memset(pcb, 0, sizeof(*pcb));
    pcb->pid      = next_pid++;
    pcb->cr3      = read_cr3_register();
    pcb->state    = PROCESS_STATE_READY;
    pcb->started  = 0;
    pcb->priv     = priv;
    pcb->eflags   = (void*)INIT_EFLAGS;
    pcb->rq_next  = NULL;
    pcb->priority = prio;
    pcb->original_priority = prio;
    strncpy(pcb->name, name, sizeof(pcb->name)-1);

    memcpy(&pcb->fpu_fx, &fx_clean, sizeof(fx_clean));
    memcpy(&pcb->signal_fpu_fx, &fx_clean, sizeof(fx_clean));

    processor_context_t *ctx = (processor_context_t *)kernel_malloc_align(PCB_ALIGNMENT, sizeof(processor_context_t));
    memset(ctx, 0, sizeof(*ctx));
    pcb->processor_context = ctx;

    processor_context_t *signal_ctx = (processor_context_t *)kernel_malloc_align(PCB_ALIGNMENT, sizeof(processor_context_t));
    memset(signal_ctx, 0, sizeof(*signal_ctx));
    pcb->signal_processor_context = signal_ctx;

    // create stack
    uint8_t *stack;
    uint32_t *stk_top;
    if (priv == CPU_USER_MODE) {
        //stack = (uint8_t*)alloc_user_stack();
        //stk_top = (uint32_t*)(stack + USER_STACK_SIZE);
        pcb->processor_context->ds          = USER_MODE_SEGMENT;
        pcb->processor_context->es          = USER_MODE_SEGMENT;
        pcb->processor_context->fs          = USER_MODE_SEGMENT;
        pcb->processor_context->gs          = USER_MODE_SEGMENT;
        pcb->processor_context->ss          = USER_MODE_SEGMENT; 
        //pcb->processor_context->esp_at_trap = (uint32_t)stk_top;
        pcb->processor_context->stub_eflags = INIT_EFLAGS;
        pcb->processor_context->eflags      = INIT_EFLAGS;
        pcb->processor_context->cs          = USER_MODE_CODE_SEGMENT; 
        pcb->processor_context->eip         = (uint32_t)entry;
        pcb->brk_start                      = USER_HEAP_START;
        pcb->brk_end                        = USER_HEAP_START;
        memcpy(signal_ctx, ctx, sizeof(processor_context_t));
        //memset(stack, 0, USER_STACK_SIZE);
    } else {
        stack = (uint8_t*)alloc_kernel_stack();
        stk_top = (uint32_t*)(stack + KERNEL_STACK_SIZE);
        memset(stack, 0, KERNEL_STACK_SIZE);
    }

    pcb->esp = stk_top;
    pcb->esp0 = get_esp();
    pcb->esp_max = stack;
    pcb->esp_min = stk_top;
    pcb->entry = entry;

    printfs(PRINT_STATUS_DEBUG,"Creating task '%s', esp=%p, esp0=%p\n", name, pcb->esp,pcb->esp0);

    enqueue_task_list(pcb);

    return pcb;
}

/**
 * @brief Adds a task to the scheduler's queue.
 * @param pcb Pointer to the task's process control block.
 */
void enqueue(process_control_block_t* pcb) {
    lock_scheduler();

    uint8_t p = pcb->priority;
    prio_queue_t *q = &prio_q[p];

    pcb->rq_next = NULL;

    if (!q->head) {
        q->head = q->tail = pcb;
        bm_set(p);
    } else {
        q->tail->rq_next = pcb;
        q->tail = pcb;
    }

    unlock_scheduler();
}

/**
 * @brief Removes and returns the next task from the scheduler's queue.
 * @return Pointer to the dequeued process control block.
 */
process_control_block_t* dequeue(void) {
    int p = highest_ready_prio();
    if (p < 0) return NULL;

    prio_queue_t *q = &prio_q[p];
    process_control_block_t *pcb = q->head;
    if (!pcb) {
        // shouldnt happen
        bm_clear(p);
        return NULL;
    }

    q->head = pcb->rq_next;
    pcb->rq_next = NULL;

    if (!q->head) {
        q->tail = NULL;
        bm_clear((uint8_t)p);
    }

    return pcb;
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
    pcb->state = PROCESS_STATE_READY;
    enqueue(pcb);
}

void enqueue_waiter(lock_t *lock, process_control_block_t *pcb) {
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

int task_lock_acquire(lock_t *lock) {
    if (!lock->held) {
        lock->held = 1;
        lock->owner = current_task;
        if (lock->block_on_hold) {
            task_block();
        }
    } else {
        if (lock->block_on_hold) {
            enqueue_waiter(lock, current_task);
            task_block();
        } else {
            return -1;
        }
    }
    return 0;
}

void task_lock_release(lock_t *lock) {
    process_control_block_t *owner = lock->owner;
    if (lock->held) {
        //kernel_free(owner->lck_ptr);
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
    if (parent->priv == CPU_KERNEL_MODE) {
        printfs(PRINT_STATUS_ERROR,"Process '%s' attempted fork in kernel mode and will be terminated.\n",current_task->name);
        task_exit(EXIT_SIGILL);
    }

    process_control_block_t *pcb = (process_control_block_t*)kernel_malloc_align(PCB_ALIGNMENT, sizeof(process_control_block_t));
    memcpy(pcb, parent, sizeof(process_control_block_t));
    processor_context_t *ctx = (processor_context_t *)kernel_malloc_align(PCB_ALIGNMENT, sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    pcb->processor_context = ctx;
    memcpy(pcb->processor_context, parent->processor_context, sizeof(processor_context_t));
    pcb->state = PROCESS_STATE_READY;
    pcb->pid   = next_pid++;

    pcb->address_space = create_address_space();
    pcb->cr3 = (void*)pcb->address_space->phys_pdir;

    uint8_t *buf = (uint8_t*)kernel_malloc(PAGE_SIZE);

    uint32_t p_stack_base = (uint32_t)parent->esp_max;
    uint32_t p_stack_top = (uint32_t)parent->esp_min;

    for (uint32_t va = USER_SPACE_START; va < USER_SPACE_END; va += PAGE_SIZE) {
        if (va >= p_stack_base && va < p_stack_top) continue;

        uint32_t src_phys = get_mapping(parent->address_space, va);
        if (!src_phys) continue;

        uint32_t dst_phys = (uint32_t)alloc_frame();

        unmap_page(pcb->address_space, va, 0);
        map_page(pcb->address_space, va, dst_phys, USER_PAGE_FLAGS, 0);

        void *src = kmap(src_phys);
        memcpy(buf, src, PAGE_SIZE);
        kunmap();

        void *dst = kmap(dst_phys);
        memcpy(dst, buf, PAGE_SIZE);
        kunmap();
    }

    uint32_t c_stack_base = (uint32_t)alloc_user_stack();
    uint32_t c_stack_top = (uint32_t)c_stack_base + USER_STACK_SIZE;

    for (uint32_t va = c_stack_base; va < c_stack_top; va += PAGE_SIZE) {
        uint32_t dst_phys = (uint32_t)alloc_frame();
        unmap_page(pcb->address_space, va, 0);
        map_page(pcb->address_space, va, dst_phys, USER_PAGE_FLAGS, 0);
        void *dst = kmap(dst_phys);
        memset(dst, 0, PAGE_SIZE);
        kunmap();
    }
    
    for (uint32_t offset = 0; offset < USER_STACK_SIZE; offset += PAGE_SIZE) {
        uint32_t p_va = p_stack_base + offset;
        uint32_t c_va = c_stack_base + offset;

        uint32_t p_phys = get_mapping(parent->address_space, p_va);
        if (!p_phys) continue;

        uint32_t c_phys = get_mapping(pcb->address_space, c_va);
        if (!c_phys) kernel_panic("task_fork: child stack page not mapped");

        void *src = kmap(p_phys);
        memcpy(buf, src, PAGE_SIZE);
        kunmap();

        void *dst = kmap(c_phys);
        memcpy(dst, buf, PAGE_SIZE);
        kunmap();
    }

    uint32_t c_esp = (uint32_t)parent->processor_context->esp_at_trap;
    uint32_t c_ebp = (uint32_t)parent->processor_context->ebp;

    pcb->esp = (uint32_t*)c_stack_top;
    pcb->esp_max = (void*)c_stack_base;
    pcb->processor_context->esp_at_trap = c_esp;
    pcb->processor_context->ebp = c_ebp;
    pcb->brk_start = USER_HEAP_START;
    pcb->brk_end = USER_HEAP_START;
    pcb->next = NULL;

    /*
    // create stack
    uint8_t *stack;
    uint32_t *stk_top;
    uint32_t ebp;
    stack = (uint8_t*)alloc_user_stack();
    stk_top = (uint32_t*)(stack + USER_STACK_SIZE);
    memset(stack, 0, USER_STACK_SIZE);

    ebp = (uint32_t)stk_top - bp_offset;
    stk_top = (uint32_t*)((uint32_t)stk_top - stk_offset);

    */

    printfs(PRINT_STATUS_DEBUG,"Forking task '%s', esp=%p, esp0=%p\n", pcb->name, pcb->esp,pcb->esp0);

    enqueue_task_list(pcb);

    return pcb;
}

void task_semaphore_init(lock_semaphore_t *semaphore, uint32_t max_count) {
    semaphore->max_count = max_count;
    semaphore->current_count = 0;
    semaphore->waiters_head = NULL;
    semaphore->waiters_tail = NULL;
}

void enqueue_waiter_semaphore(lock_semaphore_t *semaphore, process_control_block_t *pcb) {
    wait_node_t *node = kernel_malloc(sizeof(*node));
    node->task = pcb;
    node->next = NULL;
    if (semaphore->waiters_tail) {
        semaphore->waiters_tail->next = node;
        semaphore->waiters_tail = node;
    } else {
        semaphore->waiters_head = semaphore->waiters_tail = node;
    }
}

process_control_block_t *dequeue_waiter_semaphore(lock_semaphore_t *semaphore) {
    if (!semaphore->waiters_head) return NULL;
    wait_node_t *node = semaphore->waiters_head;
    process_control_block_t *pcb = node->task;
    semaphore->waiters_head = node->next;
    if (!semaphore->waiters_head)
        semaphore->waiters_tail = NULL;
    kernel_free(node);
    return pcb;
}

void task_semaphore_acquire(lock_semaphore_t *semaphore) {
    lock_scheduler();
    
    if (semaphore->current_count < semaphore->max_count) {
        semaphore->current_count++;
    } else {
        enqueue_waiter_semaphore(semaphore, current_task);
        task_block();
    }

    unlock_scheduler();
}

void task_semaphore_release(lock_semaphore_t *semaphore) {
    lock_scheduler();

    if (semaphore->waiters_head != NULL) {
        process_control_block_t *pcb = dequeue_waiter_semaphore(semaphore);
        task_unblock(pcb);
    } else {
        semaphore->current_count--;
    }

    unlock_scheduler();
}

/**
 * @brief Get the current running task.
 * @return Pointer to the current task's PCB.
 */
process_control_block_t* get_current_task(void) {
    return current_task;
}

/**
 * @brief Get the total number of tasks in the system.
 * @return The number of tasks.
 */
uint32_t get_task_count(void) {
    uint32_t count = 0;
    process_control_block_t *task = task_list;
    
    while (task != NULL) {
        count++;
        task = task->next;
    }
    
    return count;
}

int task_priority_decay(process_control_block_t *task) {
    int prio = task->priority;
    int orig_prio = task->original_priority;
    int quanta = task->quanta_used;

    if (quanta < PRIORITY_QUANTA_PUNISH)
        return prio;
    if (prio == 0) 
        return orig_prio;

    task->quanta_used = 0;

    int new_prio = clamp(prio-PRIORITY_DECAY_RATE, 0, MAX_PRIORITY);
    return new_prio;
}

int task_ipc_signal_raise(process_control_block_t *task, uint8_t signal) {
    if (signal >= 16)
        return -1;

    task->signal_bitmask |= (1u << signal);
    return 0;
}

int task_ipc_register_signal_handler(process_control_block_t *task, uint8_t signal, uint32_t handler) {
    if (signal >= 16)
        return -1;

    task->signal_handlers[signal] = handler;
    return 0;
}

int task_ipc_deliver_signals(process_control_block_t *task, processor_context_t* ctx) {
    if (task->priv != CPU_USER_MODE)
        return -1;
    if (task->in_signal_handler)
        return -1;

    uint32_t pending = task->signal_bitmask;
    uint32_t masked = task->blocked_signals;
    uint32_t deliverable = pending & ~masked;

    if (!deliverable)
        return -1;

    int sig = __builtin_ctz(deliverable);
    task->signal_bitmask &= ~(1u << sig);

    memcpy(task->signal_processor_context, ctx, sizeof(processor_context_t));
    memcpy(&task->signal_fpu_fx, &task->fpu_fx, sizeof(fx_clean));

    uint32_t handler = task->signal_handlers[sig];

    if (handler == 0) {
        task_exit(sig);
        return -1;
    }

    uint32_t *user_sp = (uint32_t *)ctx->esp_at_trap;
    user_sp -= 2;

    user_sp[0] = SIGNAL_TRAMPOLINE_ADDR;
    user_sp[1] = sig;
    ctx->esp_at_trap = (uint32_t)user_sp;
    ctx->eip = handler;

    task->in_signal_handler = 1;

    return 0;
}

process_control_block_t *task_lookup_by_pid(uint32_t pid) {
    process_control_block_t *task = task_list;
    while (task) {
        if (pid == task->pid) {
            return task;
        }
        task = task->next;
    }
}