#ifndef _KERNEL_SCHEDULER
#define _KERNEL_SCHEDULER

#include <stdint.h>
#include "../io/io.h"

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

    processor_context_t *processor_context;

    void *ebx;
    void *ebp;
    void *esi;
    void *edi;

    uint8_t signal;

    void* esp_max;
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

enum {
    EXIT_SIG0     = 0,  // Typically unused (reserved)
    EXIT_SIGHUP   = 1,  // Hangup detected on controlling terminal or death of controlling process
    EXIT_SIGINT   = 2,  // Interrupt from keyboard
    EXIT_SIGQUIT  = 3,  // Quit from keyboard
    EXIT_SIGILL   = 4,  // Illegal Instruction
    EXIT_SIGTRAP  = 5,  // Trace/breakpoint trap
    EXIT_SIGABRT  = 6,  // Abort signal from abort()
    EXIT_SIGBUS   = 7,  // Bus error (bad memory access)
    EXIT_SIGFPE   = 8,  // Floating point exception
    EXIT_SIGKILL  = 9,  // Kill signal
    EXIT_SIGUSR1  = 10, // User-defined signal 1
    EXIT_SIGSEGV  = 11, // Invalid memory reference
    EXIT_SIGUSR2  = 12, // User-defined signal 2
    EXIT_SIGPIPE  = 13, // Broken pipe: write to pipe with no readers
    EXIT_SIGALRM  = 14, // Timer signal from alarm()
    EXIT_SIGTERM  = 15  // Termination signal
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
void task_exit(uint8_t exit);
void enqueue(process_control_block_t* pcb);
process_control_block_t* dequeue();
void lock_scheduler(void);
void unlock_scheduler(void);
void task_set_state(process_control_block_t *pcb, int state);
void task_block(void);
void task_unblock(process_control_block_t *pcb);
void *alloc_user_stack(void);
void *alloc_kernel_stack(void);
void kernel_yield(void);

static inline const char* to_signal_name(int signal_id) {
    static const char* const signal_names[16] = {
        "SIG0",     "SIGHUP",  "SIGINT",  "SIGQUIT",
        "SIGILL",   "SIGTRAP", "SIGABRT", "SIGBUS",
        "SIGFPE",   "SIGKILL", "SIGUSR1", "SIGSEGV",
        "SIGUSR2",  "SIGPIPE", "SIGALRM", "SIGTERM"
    };
    return (signal_id >= 0 && signal_id < 16) ? signal_names[signal_id] : "UNKNOWN";
}

#endif