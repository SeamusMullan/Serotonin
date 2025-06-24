#include "schedule.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../string.h"
#include "../paging.h"
#include "../stdio/stdio.h"

process_control_block_t *current_task = NULL;
process_control_block_t *task_list    = NULL;
static uint32_t next_pid = 0;

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


void multitasking_init(void) {
    volatile process_control_block_t *init_task = (process_control_block_t*)kernel_malloc(sizeof(process_control_block_t));
    memset(init_task, 0, sizeof(*init_task));

    // populate fields
    init_task->pid    = next_pid++;
    init_task->esp    = get_esp();
    init_task->esp0   = NULL;                // TSS setup later
    init_task->cr3    = read_cr3_register();
    init_task->state  = PROCESS_STATE_BLOCKED;
    strncpy(init_task->name, "kernel_init", 32);

    // single‐element circular list
    init_task->next       = init_task;
    task_list             = init_task;
    current_task          = init_task;
}

void task_yield(void) {
    process_control_block_t *start = current_task;
    process_control_block_t *next = current_task;

    if (start->state == PROCESS_STATE_RUNNING) {
        start->state = PROCESS_STATE_READY;
        start->entry = __builtin_return_address(0);
    }

    do {
        next = next->next;
        if (next->state == PROCESS_STATE_READY) {
            printf("found next: %p, name: %s, started:%d, entry:%p, esp:%p\n",next, next->name,next->started,next->entry,next->esp);
            // found someone we can switch into
            start->state    = (start->state == PROCESS_STATE_RUNNING)
                              ? PROCESS_STATE_READY
                              : start->state;
            next->state     = PROCESS_STATE_RUNNING;
            current_task = start;
            switch_task(next);
            __builtin_unreachable();
        }
    } while (next != start);

    kernel_panic("task_yield: No valid task to switch to\n");
}

void task_exit(void) {
    printfs(PRINT_STATUS_DEBUG, "task_exit: Task %s (pid=%u) exited\n", current_task->name, current_task->pid);
    current_task->state = PROCESS_STATE_TERMINATED;

    task_yield();  // pick the next runnable task
    kernel_panic("task_exit: nothing to switch to");
}

__attribute__((noreturn)) void task_trampoline(void) {
    current_task->entry();
    task_exit();
    kernel_panic("task_trampoline: execution continued after task_exit");
    __builtin_unreachable();
}

process_control_block_t* task_create(void (*entry)(void), const char *name) {
    // alloc and init pcb
    volatile process_control_block_t *pcb = (process_control_block_t*)kernel_malloc(sizeof(*pcb));
    memset(pcb, 0, sizeof(*pcb));
    pcb->pid   = next_pid++;
    pcb->cr3   = read_cr3_register();
    pcb->state = PROCESS_STATE_READY;
    pcb->started = 0;
    strncpy(pcb->name, name, sizeof(pcb->name)-1);

    // create stack
    uint8_t *stack = (uint8_t*)kernel_malloc(KERNEL_STACK_SIZE);
    uint32_t *stk_top = (uint32_t*)(stack + KERNEL_STACK_SIZE);

    *(--stk_top) = 0; // EBP
    *(--stk_top) = 0; // EBX
    *(--stk_top) = 0; // ESI
    *(--stk_top) = 0; // EDI

    pcb->esp = stk_top;
    pcb->entry = entry;

    // insert into scheduler
    if (!task_list) {
        task_list = pcb;
        pcb->next = pcb;
    } else {
        volatile process_control_block_t *tail = task_list;
        while (tail->next != task_list)
            tail = tail->next;

        tail->next = pcb;
        pcb->next = task_list;
    }

    printfs(PRINT_STATUS_DEBUG,"Creating task '%s', esp=%p\n", name, pcb->esp);

    return pcb;
}