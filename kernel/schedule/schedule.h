#ifndef _KERNEL_SCHEDULER
#define _KERNEL_SCHEDULER

#include <stdint.h>
#include "../io/io.h"
#include "../filesystem/user_fs/user_fs.h"
#include "../vmm/vmm.h"

#define USER_MODE_SEGMENT      0x23
#define USER_MODE_CODE_SEGMENT 0x1B
#define INIT_EFLAGS            0x00000202 // RSVD, IF
#define MAX_TASKS              256
#define NGROUPS_MAX            16
#define PCB_ALIGNMENT          16
#define MAX_PRIORITY           256
#define PRIORITY_DECAY_RATE    10
#define PRIORITY_QUANTA_PUNISH 10

#define MAX_BOUND_SOCKETS 64

typedef struct fpu_fxsave_area {
    uint8_t bytes[512];
} fpu_fxsave_area_t __attribute__((aligned(16)));

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

    void* ebx;
    void* ebp;
    void* esi;
    void* edi;
    void* eflags;

    uint8_t signal;

    void* esp_max;
    void* esp_min;
    void* ipc_ptr;
    void* lck_ptr;

    __attribute__((aligned(16))) fpu_fxsave_area_t fpu_fx;

    file_handle_t* fd_table[FD_MAX];
    struct process_control_block *rq_next;
    uint8_t priority;
    address_space_t *address_space;
    uint32_t *argv;
    uint32_t *envp;
    uint32_t brk_start;
    uint32_t brk_end;
    int waiting_on;
    int* status_ptr;
    uint8_t exit_status;
    uint32_t quanta_used;
    uint8_t original_priority;
    uint32_t signal_handlers[16];
    uint32_t signal_bitmask;
    uint32_t blocked_signals;
    processor_context_t *signal_processor_context;
    __attribute__((aligned(16))) fpu_fxsave_area_t signal_fpu_fx;
    uint8_t in_signal_handler;
    uint8_t no_requeue;
    char cwd[256];
    uint16_t uid, gid, euid, egid;
    uint16_t groups[NGROUPS_MAX];
    uint8_t ngroups;
    uint32_t umask;
    uint32_t current_fd_flags;
    uint32_t current_user_buf;
    uint32_t alarm_ticks;
} process_control_block_t;

typedef struct pipe_waiter {
    process_control_block_t *task;
    struct pipe_waiter *next;
} pipe_waiter_t;

typedef struct pipe_state {
    char *buffer;
    uint32_t size;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t data_len;
    uint32_t readers;
    uint32_t writers;
    pipe_waiter_t *read_waiters_head;
    pipe_waiter_t *read_waiters_tail;
    pipe_waiter_t *write_waiters_head;
    pipe_waiter_t *write_waiters_tail;
} pipe_state_t;

typedef struct pipe_endpoint {
    pipe_state_t *pipe;
    uint8_t is_read_end;
} pipe_endpoint_t;

extern vfs_ops_t task_ipc_pipe_ops;

#define AF_UNIX       1
#define SOCK_STREAM   1
#define SOCK_DGRAM    2

#define UNIX_PATH_MAX   108
#define SOCK_BUFFER_SIZE 4096
#define SOCK_BACKLOG_MAX 8

struct sockaddr_un {
    uint16_t sun_family;
    char     sun_path[UNIX_PATH_MAX];
};

typedef struct unix_socket unix_socket_t;

typedef struct sock_waiter {
    process_control_block_t *task;
    struct sock_waiter *next;
    uint32_t user_buf;          /* user-space destination/source address */
    uint32_t buf_size;          /* requested read/write size */
} sock_waiter_t;

typedef struct unix_socket {
    uint8_t  type;              /* SOCK_STREAM or SOCK_DGRAM */
    uint8_t  state;             /* SOCK_STATE_* */
    uint8_t  bound;
    char     path[UNIX_PATH_MAX];

    /* circular data buffer (stream / dgram recv) */
    char    *buffer;
    uint32_t buf_size;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t data_len;

    /* reference counts */
    uint32_t readers;
    uint32_t writers;

    /* blocking queues */
    sock_waiter_t *read_waiters_head,  *read_waiters_tail;
    sock_waiter_t *write_waiters_head, *write_waiters_tail;

    /* stream connection state */
    unix_socket_t *peer;        /* connected peer (stream) */

    /* listen backlog */
    unix_socket_t *backlog[SOCK_BACKLOG_MAX];
    uint32_t backlog_count;
    uint32_t backlog_max;
    sock_waiter_t *accept_waiters_head, *accept_waiters_tail;
    sock_waiter_t *connect_waiters_head, *connect_waiters_tail;
} unix_socket_t;

enum {
    SOCK_STATE_UNCONNECTED = 0,
    SOCK_STATE_BOUND       = 1,
    SOCK_STATE_LISTENING   = 2,
    SOCK_STATE_CONNECTING  = 3,
    SOCK_STATE_CONNECTED   = 4,
    SOCK_STATE_CLOSED      = 5
};

typedef struct sock_endpoint {
    unix_socket_t *sock;
} sock_endpoint_t;

extern vfs_ops_t task_ipc_unix_socket_ops;

int  task_ipc_unix_socket_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int  task_ipc_unix_socket_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
int  task_ipc_unix_socket_close(vfs_node_t *node);

unix_socket_t *unix_socket_lookup(const char *path);
int  unix_socket_register(unix_socket_t *sock);
void unix_socket_unregister(unix_socket_t *sock);

int  sock_waiter_enqueue(sock_waiter_t **head, sock_waiter_t **tail, process_control_block_t *task, uint32_t user_buf, uint32_t buf_size);
sock_waiter_t *sock_waiter_dequeue(sock_waiter_t **head, sock_waiter_t **tail);
void sock_wake_one(sock_waiter_t **head, sock_waiter_t **tail);
void sock_wake_all(sock_waiter_t **head, sock_waiter_t **tail);

typedef struct wait_node {
    struct process_control_block *task;
    struct wait_node           *next;
} wait_node_t;

typedef struct lock {
    uint8_t held;
    uint8_t block_on_hold;
    process_control_block_t *owner;
    wait_node_t *waiters_head;
    wait_node_t *waiters_tail;
} lock_t;

typedef struct lock_semaphore {
    uint32_t max_count;
    uint32_t current_count;
    wait_node_t *waiters_head;
    wait_node_t *waiters_tail;
} lock_semaphore_t;

void task_semaphore_init(lock_semaphore_t *semaphore, uint32_t count);
void task_semaphore_acquire(lock_semaphore_t *semaphore);
void task_semaphore_release(lock_semaphore_t *semaphore);

typedef struct {
    process_control_block_t *head;
    process_control_block_t *tail;
} prio_queue_t;

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
extern lock_t *stdin_lock;
extern volatile int foreground_pid;

void multitasking_init(void);
void multitasking_make_ready(void);
__attribute__((naked,noreturn)) extern void switch_task(process_control_block_t* next_thread);
__attribute__((naked,noreturn)) extern void switch_task_iret(process_control_block_t* next_thread);
process_control_block_t* task_create(void (*entry)(void), const char *name, uint8_t priv, uint8_t prio);
void task_yield(int irq);
void task_exit(process_control_block_t* task_exited, uint8_t exit);
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
void task_lock_init(lock_t *lock, uint8_t block_on_hold);
int task_lock_acquire(lock_t *lock);
void task_lock_release(lock_t *lock);
process_control_block_t* task_fork(process_control_block_t *parent);
void preempt_disable();
void preempt_enable();
process_control_block_t* get_current_task(void);
uint32_t get_task_count(void);
int task_priority_decay(process_control_block_t *task);
int task_ipc_signal_raise(process_control_block_t *task, uint8_t signal);
int task_ipc_register_signal_handler(process_control_block_t *task, uint8_t signal, uint32_t handler);
int task_ipc_deliver_signals(process_control_block_t *task, processor_context_t* ctx) ;
process_control_block_t *task_lookup_by_pid(uint32_t pid);
void task_set_fid(int pid);
void task_ipc_break_fid();
int task_ipc_pipe_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int task_ipc_pipe_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
int task_ipc_pipe_close(vfs_node_t *node);

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
