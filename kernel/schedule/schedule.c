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
#include "../syscall/sys/errno.h"

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
static process_control_block_t *zombie_list = NULL;
volatile int foreground_pid = 0;
vfs_ops_t task_ipc_pipe_ops;
static unix_socket_t *bound_sockets[MAX_BOUND_SOCKETS];
static uint32_t bound_socket_count = 0;

vfs_ops_t task_ipc_unix_socket_ops = {
    .read  = task_ipc_unix_socket_read,
    .write = task_ipc_unix_socket_write,
    .close = task_ipc_unix_socket_close,
    .truncate = NULL,
    .unlink   = NULL,
    .rmdir    = NULL,
    .open     = NULL,
    .readdir  = NULL,
    .finddir  = NULL,
    .create   = NULL,
    .mkdir    = NULL,
};

static void reap_zombies(void) {
    process_control_block_t *task = zombie_list;
    process_control_block_t *prev = NULL;

    while (task) {
        if (task == current_task) {
            prev = task;
            task = task->next;
            continue;
        }

        process_control_block_t *next = task->next;
        if (prev) {
            prev->next = next;
        } else {
            zombie_list = next;
        }

        destroy_address_space(task->address_space);
        kernel_free_align(task->processor_context);
        kernel_free_align(task->signal_processor_context);
        kernel_free_align(task);
        task = next;
    }
}

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
 *
 * Saves the hardware IF state on the outermost lock so that
 * unlock_scheduler restores it correctly.  This prevents sti
 * from being called inside an IRQ handler whose interrupt gate
 * already cleared IF.
 */
static uint32_t saved_eflags = 0;

void lock_scheduler(void) {
    if (multitasking_ready == 0)
        return;
    if (lock_count == 0) {
        uint32_t flags;
        asm volatile("pushfl; popl %0" : "=r"(flags) : : "memory");
        saved_eflags = flags;
    }
    lock_count++;
    asm volatile("cli" ::: "memory");
}

/**
 * @brief Unlocks the scheduler and restores the interrupt state
 *        that was active before the outermost lock_scheduler call.
 */
void unlock_scheduler(void) {
    if (multitasking_ready == 0)
        return;
    lock_count--;
    if (!lock_count) {
        if (saved_eflags & 0x200)
            asm volatile("sti" ::: "memory");
    }
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
    task_ipc_pipe_ops.read = task_ipc_pipe_read;
    task_ipc_pipe_ops.write = task_ipc_pipe_write;
    task_ipc_pipe_ops.close = task_ipc_pipe_close;

    printfs(PRINT_STATUS_INFO,"scheduler: init\n");
    vbe_flip();
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
    reap_zombies();

    if (current_task->state == PROCESS_STATE_RUNNING) {
        current_task->state = PROCESS_STATE_READY;
        if (!current_task->no_requeue)
            enqueue(current_task);
    }

    process_control_block_t* next = dequeue();
    while (next) {
        if (next->state == PROCESS_STATE_READY) {
            if (next->priv == CPU_USER_MODE) {
                switch_address_space(next->address_space);
                task_ipc_deliver_signals(next, next->processor_context);
            }
            // found someone we can switch into
            next->state = PROCESS_STATE_RUNNING;
            preempt_enable();

            // if nothing is pending, switch
            switch_task(next);
            __builtin_unreachable();
        }
        // shouldn't happen but let's try recover
        next = dequeue();
    }

    kernel_panic("task_yield: no valid task to switch to");
}

/**
 * @brief Terminates the currently running task and switches to the next one.
 */
void task_exit(process_control_block_t* task_exited, uint8_t exit) {
    lock_scheduler();

    printfs(PRINT_STATUS_DEBUG, "task_exit: Task %s (pid=%u) exited:%s\n", task_exited->name, task_exited->pid,to_signal_name(exit));

    if (task_exited->pid == 1) {
        kernel_panic("init died");
    }

    task_exited->state = PROCESS_STATE_TERMINATED;
    task_exited->signal = exit;

    for (int i = 0; i < FD_MAX; i++) {
        if (task_exited->fd_table[i]) {
            close_fd(task_exited, i);
        }
    }

    process_control_block_t *task = task_list;
    process_control_block_t *prev_task = NULL;
    while (task) {
        if (task->waiting_on == task_exited->pid) {
            task->waiting_on = -1;
            int write_rc = 0;
            if (task->status_ptr) {
                uint32_t va = (uint32_t)task->status_ptr;
                uint32_t phys = get_mapping(task->address_space, va);
                if (phys) {
                    uint8_t *dst = (uint8_t*)kmap(phys);
                    dst[va & (PAGE_SIZE - 1)] = exit;
                    kunmap();
                } else {
                    write_rc = -EFAULT;
                }
            } else {
                write_rc = -EFAULT;
            }
            task->state = PROCESS_STATE_READY;
            task->processor_context->eax = write_rc ? write_rc : exit;
            enqueue(task);
        }
        if (task == task_exited) {
            if (prev_task) {
                prev_task->next = task->next;
            } else {
                task_list = task->next;
            }
        }
        prev_task = task;
        task = task->next;
    }
    address_space_t *as = task_exited->address_space;
    if (as) {
        while (as->shmem_list) {
            shm_unmap(as, as->shmem_list->start);
        }
    }

    task_exited->next = zombie_list;
    zombie_list = task_exited;

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
    strcpy(pcb->cwd, "/");

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
        pcb->processor_context->ds          = USER_MODE_SEGMENT;
        pcb->processor_context->es          = USER_MODE_SEGMENT;
        pcb->processor_context->fs          = USER_MODE_SEGMENT;
        pcb->processor_context->gs          = USER_MODE_SEGMENT;
        pcb->processor_context->ss          = USER_MODE_SEGMENT;
        pcb->processor_context->stub_eflags = INIT_EFLAGS;
        pcb->processor_context->eflags      = INIT_EFLAGS;
        pcb->processor_context->cs          = USER_MODE_CODE_SEGMENT;
        pcb->processor_context->eip         = (uint32_t)entry;
        pcb->brk_start                      = USER_HEAP_START;
        pcb->brk_end                        = USER_HEAP_START;
        memcpy(signal_ctx, ctx, sizeof(processor_context_t));

        uint8_t *kstack = (uint8_t*)alloc_kernel_stack();
        uint32_t kstack_top = (uint32_t)kstack + KERNEL_STACK_SIZE;
        memset(kstack, 0, KERNEL_STACK_SIZE);
        pcb->esp0 = (void*)kstack_top;
    } else {
        stack = (uint8_t*)alloc_kernel_stack();
        stk_top = (uint32_t*)(stack + KERNEL_STACK_SIZE);
        memset(stack, 0, KERNEL_STACK_SIZE);
        pcb->esp = stk_top;
        pcb->esp0 = (void*)stk_top;
    }

    pcb->esp_max = (priv == CPU_USER_MODE) ? NULL : (void*)stack;
    pcb->esp_min = (priv == CPU_USER_MODE) ? NULL : (void*)stk_top;
    pcb->entry = entry;

    printfs(PRINT_STATUS_INFO,"scheduler: spawned new task \"%s\"\n",pcb->name);
    vbe_flip();

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
    return;
}

/**
 * @brief Unblocks the specified task and makes it ready to run.
 * @param pcb Pointer to the task's process control block.
 */
void task_unblock(process_control_block_t *pcb) {
    lock_scheduler();
    if (pcb->state == PROCESS_STATE_BLOCKED) {
        pcb->state = PROCESS_STATE_READY;
        enqueue(pcb);
    }
    unlock_scheduler();
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
    lock_scheduler();

    if (parent->priv == CPU_KERNEL_MODE) {
        printfs(PRINT_STATUS_ERROR,"Process '%s' attempted fork in kernel mode and will be terminated.\n",current_task->name);
        task_exit(parent,EXIT_SIGILL);
    }

    process_control_block_t *pcb = (process_control_block_t*)kernel_malloc_align(PCB_ALIGNMENT, sizeof(process_control_block_t));
    memcpy(pcb, parent, sizeof(process_control_block_t));
    processor_context_t *ctx = (processor_context_t *)kernel_malloc_align(PCB_ALIGNMENT, sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    pcb->processor_context = ctx;
    memcpy(pcb->processor_context, parent->processor_context, sizeof(processor_context_t));
    processor_context_t *signal_ctx = (processor_context_t *)kernel_malloc_align(PCB_ALIGNMENT, sizeof(*signal_ctx));
    memset(signal_ctx, 0, sizeof(*signal_ctx));
    pcb->signal_processor_context = signal_ctx;
    if (parent->signal_processor_context) {
        memcpy(pcb->signal_processor_context, parent->signal_processor_context, sizeof(processor_context_t));
    } else {
        memcpy(pcb->signal_processor_context, pcb->processor_context, sizeof(processor_context_t));
    }
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

    pcb->signal_bitmask = 0;
    pcb->alarm_ticks = 0;
    for (int i = 0; i < 16; i++) {
        pcb->signal_handlers[i] = 0;
    }
    for (int i = 0; i < FD_MAX; i++) {
        if (pcb->fd_table[i]) {
            pcb->fd_table[i]->refcount++;
        }
    }

    uint8_t *child_kstack = (uint8_t*)alloc_kernel_stack();
    uint32_t child_kstack_top = (uint32_t)child_kstack + KERNEL_STACK_SIZE;
    memset(child_kstack, 0, KERNEL_STACK_SIZE);
    pcb->esp0 = (void*)child_kstack_top;

    printfs(PRINT_STATUS_DEBUG,"Forking task '%s', esp=%p, esp0=%p\n", pcb->name, pcb->esp,pcb->esp0);

    enqueue_task_list(pcb);

    unlock_scheduler();

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

process_control_block_t *task_lookup_by_pid(uint32_t pid) {
    process_control_block_t *task = task_list;
    while (task) {
        if (pid == task->pid) {
            return task;
        }
        task = task->next;
    }
    return NULL;
}

int task_ipc_signal_raise(process_control_block_t *task, uint8_t signal) {
    if (signal >= 16)
        return -1;

    task->signal_bitmask |= (1u << signal);

    if (task->state == PROCESS_STATE_BLOCKED)
        task_unblock(task);

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
        task_exit(task,sig);
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

// FIXME
void task_ipc_break_fid() {
    process_control_block_t *fpcb = task_lookup_by_pid(foreground_pid);

    if (!fpcb)
        return;

    if (fpcb->priv == CPU_KERNEL_MODE)
        kernel_panic("kernel process as foreground pid!");

    task_ipc_signal_raise(fpcb, EXIT_SIGINT);
}

int task_ipc_pipe_waiter_enqueue(pipe_waiter_t **head, pipe_waiter_t **tail, process_control_block_t *task) {
    pipe_waiter_t *node = kernel_malloc(sizeof(*node));
    if (!node)
        return -ENOMEM;
    node->task = task;
    node->next = NULL;
    if (*tail) {
        (*tail)->next = node;
        *tail = node;
    } else {
        *head = *tail = node;
    }
    return 0;
}

process_control_block_t *task_ipc_pipe_waiter_dequeue(pipe_waiter_t **head, pipe_waiter_t **tail) {
    if (!*head)
        return NULL;
    pipe_waiter_t *node = *head;
    process_control_block_t *task = node->task;
    *head = node->next;
    if (!*head)
        *tail = NULL;
    kernel_free(node);
    return task;
}

void task_ipc_pipe_wake_one_reader(pipe_state_t *pipe) {
    process_control_block_t *task = task_ipc_pipe_waiter_dequeue(&pipe->read_waiters_head, &pipe->read_waiters_tail);
    if (task)
        task_unblock(task);
}

void task_ipc_pipe_wake_one_writer(pipe_state_t *pipe) {
    process_control_block_t *task = task_ipc_pipe_waiter_dequeue(&pipe->write_waiters_head, &pipe->write_waiters_tail);
    if (task)
        task_unblock(task);
}

void task_ipc_pipe_wake_all_readers(pipe_state_t *pipe) {
    process_control_block_t *task;
    while ((task = task_ipc_pipe_waiter_dequeue(&pipe->read_waiters_head, &pipe->read_waiters_tail)) != NULL) {
        task_unblock(task);
    }
}

void task_ipc_pipe_wake_all_writers(pipe_state_t *pipe) {
    process_control_block_t *task;
    while ((task = task_ipc_pipe_waiter_dequeue(&pipe->write_waiters_head, &pipe->write_waiters_tail)) != NULL) {
        task_unblock(task);
    }
}

int task_ipc_pipe_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    pipe_endpoint_t *endpoint = (pipe_endpoint_t*)node->fs_data;
    if (!endpoint || !endpoint->pipe || !endpoint->is_read_end)
        return -EBADF;

    pipe_state_t *pipe = endpoint->pipe;

    while (1) {
        lock_scheduler();
        if (pipe->data_len > 0) {
            uint32_t to_read = size < pipe->data_len ? size : pipe->data_len;
            for (uint32_t i = 0; i < to_read; i++) {
                buffer[i] = pipe->buffer[pipe->read_pos];
                pipe->read_pos = (pipe->read_pos + 1) % pipe->size;
            }
            pipe->data_len -= to_read;
            if (pipe->write_waiters_head)
                task_ipc_pipe_wake_one_writer(pipe);
            unlock_scheduler();
            return (int)to_read;
        }

        if (pipe->writers == 0) {
            unlock_scheduler();
            return 0;
        }

        if (task_ipc_pipe_waiter_enqueue(&pipe->read_waiters_head, &pipe->read_waiters_tail, current_task) != 0) {
            unlock_scheduler();
            return -ENOMEM;
        }
        current_task->state = PROCESS_STATE_BLOCKED;
        unlock_scheduler();
        task_yield(1);
    }
}

int task_ipc_pipe_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    pipe_endpoint_t *endpoint = (pipe_endpoint_t*)node->fs_data;
    if (!endpoint || !endpoint->pipe || endpoint->is_read_end)
        return -EBADF;

    pipe_state_t *pipe = endpoint->pipe;
    uint32_t written = 0;

    while (written < size) {
        lock_scheduler();
        if (pipe->readers == 0) {
            unlock_scheduler();
            task_ipc_signal_raise(current_task, EXIT_SIGPIPE);
            return -EPIPE;
        }

        if (pipe->data_len < pipe->size) {
            uint32_t space = pipe->size - pipe->data_len;
            uint32_t to_write = (size - written) < space ? (size - written) : space;
            for (uint32_t i = 0; i < to_write; i++) {
                pipe->buffer[pipe->write_pos] = buffer[written + i];
                pipe->write_pos = (pipe->write_pos + 1) % pipe->size;
            }
            pipe->data_len += to_write;
            written += to_write;
            if (pipe->read_waiters_head)
                task_ipc_pipe_wake_one_reader(pipe);
            unlock_scheduler();
            return (int)written;
        }

        if (task_ipc_pipe_waiter_enqueue(&pipe->write_waiters_head, &pipe->write_waiters_tail, current_task) != 0) {
            unlock_scheduler();
            return -ENOMEM;
        }
        current_task->state = PROCESS_STATE_BLOCKED;
        unlock_scheduler();
        task_yield(1);
    }

    return (int)written;
}

int task_ipc_pipe_close(vfs_node_t *node) {
    if (!node)
        return 0;

    pipe_endpoint_t *endpoint = (pipe_endpoint_t*)node->fs_data;
    if (!endpoint || !endpoint->pipe)
        return 0;

    pipe_state_t *pipe = endpoint->pipe;
    lock_scheduler();
    if (endpoint->is_read_end) {
        if (pipe->readers > 0)
            pipe->readers--;
        if (pipe->readers == 0)
            task_ipc_pipe_wake_all_writers(pipe);
    } else {
        if (pipe->writers > 0)
            pipe->writers--;
        if (pipe->writers == 0)
            task_ipc_pipe_wake_all_readers(pipe);
    }
    int free_pipe = (pipe->readers == 0 && pipe->writers == 0);
    unlock_scheduler();

    kernel_free(endpoint);
    if (free_pipe) {
        kernel_free(pipe->buffer);
        kernel_free(pipe);
    }
    return 0;
}

int sock_waiter_enqueue(sock_waiter_t **head, sock_waiter_t **tail, process_control_block_t *task, uint32_t user_buf, uint32_t buf_size) {
    sock_waiter_t *node = (sock_waiter_t *)kernel_malloc(sizeof(*node));
    if (!node) return -ENOMEM;
    node->task = task;
    node->next = NULL;
    node->user_buf = user_buf;
    node->buf_size = buf_size;
    if (*tail) {
        (*tail)->next = node;
        *tail = node;
    } else {
        *head = *tail = node;
    }
    return 0;
}

sock_waiter_t *sock_waiter_dequeue(sock_waiter_t **head, sock_waiter_t **tail) {
    if (!*head) return NULL;
    sock_waiter_t *node = *head;
    *head = node->next;
    if (!*head) *tail = NULL;
    return node;
}

void sock_wake_one(sock_waiter_t **head, sock_waiter_t **tail) {
    sock_waiter_t *w = sock_waiter_dequeue(head, tail);
    if (w) {
        w->task->processor_context->eax = 0;
        task_unblock(w->task);
        kernel_free(w);
    }
}

void sock_wake_all(sock_waiter_t **head, sock_waiter_t **tail) {
    sock_waiter_t *w;
    while ((w = sock_waiter_dequeue(head, tail)) != NULL) {
        w->task->processor_context->eax = 0;
        task_unblock(w->task);
        kernel_free(w);
    }
}

unix_socket_t *unix_socket_lookup(const char *path) {
    for (uint32_t i = 0; i < bound_socket_count; i++) {
        if (bound_sockets[i] && strcmp(bound_sockets[i]->path, path) == 0)
            return bound_sockets[i];
    }
    return NULL;
}

int unix_socket_register(unix_socket_t *sock) {
    if (bound_socket_count >= MAX_BOUND_SOCKETS)
        return -ENOSPC;
    bound_sockets[bound_socket_count++] = sock;
    return 0;
}

void unix_socket_unregister(unix_socket_t *sock) {
    for (uint32_t i = 0; i < bound_socket_count; i++) {
        if (bound_sockets[i] == sock) {
            bound_sockets[i] = bound_sockets[--bound_socket_count];
            return;
        }
    }
}

static void sock_read_wake_one(unix_socket_t *sock) {
    sock_waiter_t *w = sock_waiter_dequeue(&sock->read_waiters_head, &sock->read_waiters_tail);
    if (!w) return;

    uint32_t avail = sock->data_len;
    uint32_t to_read = w->buf_size < avail ? w->buf_size : avail;

    if (to_read > 0 && w->user_buf) {
        char tmp[256];
        uint32_t done = 0;
        while (done < to_read) {
            uint32_t chunk = to_read - done;
            if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
            for (uint32_t i = 0; i < chunk; i++) {
                tmp[i] = sock->buffer[sock->read_pos];
                sock->read_pos = (sock->read_pos + 1) % sock->buf_size;
            }
            sock->data_len -= chunk;
            copy_to_user(w->task->address_space, w->user_buf + done, tmp, chunk);
            done += chunk;
        }
        w->task->processor_context->eax = to_read;
    } else {
        w->task->processor_context->eax = 0;
    }

    task_unblock(w->task);
    kernel_free(w);
}

int task_ipc_unix_socket_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    sock_endpoint_t *ep = (sock_endpoint_t *)node->fs_data;
    if (!ep || !ep->sock)
        return -EBADF;

    unix_socket_t *sock = ep->sock;

    if (sock->type == SOCK_STREAM && sock->state != SOCK_STATE_CONNECTED)
        return -ENOTCONN;

    lock_scheduler();

    if (sock->data_len > 0) {
        uint32_t to_read = size < sock->data_len ? size : sock->data_len;
        for (uint32_t i = 0; i < to_read; i++) {
            buffer[i] = sock->buffer[sock->read_pos];
            sock->read_pos = (sock->read_pos + 1) % sock->buf_size;
        }
        sock->data_len -= to_read;
        if (sock->write_waiters_head)
            sock_wake_one(&sock->write_waiters_head, &sock->write_waiters_tail);
        unlock_scheduler();
        return (int)to_read;
    }

    if (sock->type == SOCK_STREAM && (!sock->peer || sock->peer->state == SOCK_STATE_CLOSED)) {
        unlock_scheduler();
        return 0;
    }

    uint32_t user_buf = current_task->current_user_buf;
    if (sock_waiter_enqueue(&sock->read_waiters_head, &sock->read_waiters_tail, current_task, user_buf, size) != 0) {
        unlock_scheduler();
        return -ENOMEM;
    }
    current_task->state = PROCESS_STATE_BLOCKED;
    unlock_scheduler();
    task_yield(1);
    __builtin_unreachable();
}

int task_ipc_unix_socket_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    sock_endpoint_t *ep = (sock_endpoint_t *)node->fs_data;
    if (!ep || !ep->sock)
        return -EBADF;

    unix_socket_t *sock = ep->sock;

    if (sock->type == SOCK_STREAM) {
        if (sock->state != SOCK_STATE_CONNECTED || !sock->peer)
            return -ENOTCONN;

        unix_socket_t *peer = sock->peer;

        lock_scheduler();
        if (peer->state == SOCK_STATE_CLOSED) {
            unlock_scheduler();
            task_ipc_signal_raise(current_task, EXIT_SIGPIPE);
            return -EPIPE;
        }

        if (peer->data_len < peer->buf_size) {
            uint32_t space = peer->buf_size - peer->data_len;
            uint32_t to_write = size < space ? size : space;
            for (uint32_t i = 0; i < to_write; i++) {
                peer->buffer[peer->write_pos] = buffer[i];
                peer->write_pos = (peer->write_pos + 1) % peer->buf_size;
            }
            peer->data_len += to_write;
            if (peer->read_waiters_head)
                sock_read_wake_one(peer);
            unlock_scheduler();
            return (int)to_write;
        }

        if (sock_waiter_enqueue(&peer->write_waiters_head, &peer->write_waiters_tail, current_task, 0, 0) != 0) {
            unlock_scheduler();
            return -ENOMEM;
        }
        current_task->state = PROCESS_STATE_BLOCKED;
        unlock_scheduler();
        task_yield(1);
        __builtin_unreachable();
    }

    if (sock->peer) {
        unix_socket_t *peer = sock->peer;
        lock_scheduler();
        if (peer->data_len + size > peer->buf_size) {
            unlock_scheduler();
            return -EMSGSIZE;
        }
        for (uint32_t i = 0; i < size; i++) {
            peer->buffer[peer->write_pos] = buffer[i];
            peer->write_pos = (peer->write_pos + 1) % peer->buf_size;
        }
        peer->data_len += size;
        if (peer->read_waiters_head)
            sock_read_wake_one(peer);
        unlock_scheduler();
        return (int)size;
    }

    return -EDESTADDRREQ;
}

int task_ipc_unix_socket_close(vfs_node_t *node) {
    if (!node) return 0;

    sock_endpoint_t *ep = (sock_endpoint_t *)node->fs_data;
    if (!ep || !ep->sock) return 0;

    unix_socket_t *sock = ep->sock;

    lock_scheduler();
    sock->state = SOCK_STATE_CLOSED;

    sock_wake_all(&sock->read_waiters_head, &sock->read_waiters_tail);
    sock_wake_all(&sock->write_waiters_head, &sock->write_waiters_tail);
    sock_wake_all(&sock->accept_waiters_head, &sock->accept_waiters_tail);
    sock_wake_all(&sock->connect_waiters_head, &sock->connect_waiters_tail);

    if (sock->peer) {
        unix_socket_t *peer = sock->peer;
        if (peer->peer == sock)
            peer->peer = NULL;
        sock_wake_all(&peer->read_waiters_head, &peer->read_waiters_tail);
        sock_wake_all(&peer->write_waiters_head, &peer->write_waiters_tail);
        sock->peer = NULL;
    }
    unlock_scheduler();

    if (sock->bound)
        unix_socket_unregister(sock);

    kernel_free(ep);
    kernel_free(sock->buffer);
    kernel_free(sock);
    return 0;
}
