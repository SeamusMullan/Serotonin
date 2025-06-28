#ifndef _KERNEL_SCHEDULER
#define _KERNEL_SCHEDULER

#include <stdint.h>

typedef struct process_control_block {
    uint32_t pid;

    void* esp;
    void* esp0;
    void* cr3;

    struct process_control_block *next;

    uint8_t state;
    char name[32];

    void (*entry)(void);
    uint8_t started;
    uint8_t priv;

} process_control_block_t;

enum {
    PROCESS_STATE_UNUSED = 0,
    PROCESS_STATE_RUNNING = 1,
    PROCESS_STATE_READY = 2,
    PROCESS_STATE_BLOCKED = 3,
    PROCESS_STATE_TERMINATED = 4
};

enum {
    CPU_KERNEL_MODE = 0,
    CPU_USER_MODE   = 3
};

extern process_control_block_t *current_task;
extern process_control_block_t *task_list;
extern volatile uint32_t preempt_count;
extern volatile uint8_t pending_schedule;

void multitasking_init(void);
void multitasking_make_ready(void);
__attribute__((naked,noreturn)) extern void switch_task(process_control_block_t* next_thread);
__attribute__((naked,noreturn)) extern void switch_task_iret(process_control_block_t* next_thread);
process_control_block_t* task_create(void (*entry)(void), const char *name, uint8_t priv);
void task_yield(int irq);
void task_trampoline(void);
void task_exit(void);
void enqueue(process_control_block_t* pcb);
process_control_block_t* dequeue();
void lock_scheduler(void);
void unlock_scheduler(void);
void task_set_state(process_control_block_t *pcb, int state);
void task_block(void);
void task_unblock(process_control_block_t *pcb);
void preempt_enable(void);
void preempt_disable(void);

#endif