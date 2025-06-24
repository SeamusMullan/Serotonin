#ifndef _KERNEL_SCHEDULER
#define _KERNEL_SCHEDULER

#include <stdint.h>

typedef struct process_control_block {
    uint32_t pid;

    void* esp;
    void* esp0; // Unused, TSS TBD
    void* cr3;

    struct process_control_block *next;

    uint8_t state;
    char name[32];

    void (*entry)(void);
    uint8_t started;

} process_control_block_t;

enum {
    PROCESS_STATE_UNUSED = 0,
    PROCESS_STATE_RUNNING = 1,
    PROCESS_STATE_READY = 2,
    PROCESS_STATE_BLOCKED = 3,
    PROCESS_STATE_TERMINATED = 4
};

extern process_control_block_t *current_task;
extern process_control_block_t *task_list;

void multitasking_init(void);
__attribute__((naked,noreturn)) extern void switch_task(process_control_block_t* next_thread);
__attribute__((naked,noreturn)) extern void switch_task_iret(process_control_block_t* next_thread);
process_control_block_t* task_create(void (*entry)(void), const char *name);
void task_yield(int irq);
void task_trampoline(void);
void task_exit(void);
void schedule_timer_tick(void);

#endif