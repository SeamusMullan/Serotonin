#ifndef _KERNEL_SYSCALl
#define _KERNEL_SYSCALl

#include <stdint.h>
#include "../io/io.h"
#include "../schedule/schedule.h"

#define STDIN_BUFFER_SIZE 4096
#define FIRST_FD 3

#define FD_SETSIZE 64
#define _FD_WORDS  (FD_SETSIZE / 32)
#define K_FD_ZERO(s)      do { for (int _i=0;_i<_FD_WORDS;_i++) (s)->bits[_i]=0; } while(0)
#define K_FD_SET(fd,s)    ((s)->bits[(fd)/32] |=  (1U << ((fd)%32)))
#define K_FD_CLR(fd,s)    ((s)->bits[(fd)/32] &= ~(1U << ((fd)%32)))
#define K_FD_ISSET(fd,s)  ((s)->bits[(fd)/32] &   (1U << ((fd)%32)))

#define POLL_WAITER_SELECT 0
#define POLL_WAITER_POLL   1

#define SOCK_BUFFER_SIZE_ALLOC 4096

typedef struct { uint32_t bits[_FD_WORDS]; } kernel_fd_set;

struct kernel_pollfd {
    int      fd;
    int16_t  events;
    int16_t  revents;
};

struct kernel_timeval {
    int32_t tv_sec;
    int32_t tv_usec;
};

typedef struct poll_waiter {
    struct poll_waiter       *next;
    process_control_block_t  *task;
    uint8_t                   type;        /* SELECT or POLL */
    uint64_t                  deadline;    /* 0 = no timeout */
    uint8_t                   has_timeout;

    /* SELECT fields */
    int                       nfds;
    kernel_fd_set             readfds;
    kernel_fd_set             writefds;
    kernel_fd_set             exceptfds;
    uint32_t                  readfds_ptr;
    uint32_t                  writefds_ptr;
    uint32_t                  exceptfds_ptr;

    /* POLL fields */
    struct kernel_pollfd     *pfds;        /* kernel-heap copy */
    uint32_t                  poll_nfds;
    uint32_t                  poll_fds_ptr; /* user-space address */
} poll_waiter_t;

/**
 * @brief Enumeration of system calls.
 *
 * This enum defines the various system calls available in the kernel.
 */
enum {
    SYSTEM_CALL_EXIT       = 0,
    SYSTEM_CALL_WRITE      = 1,
    SYSTEM_CALL_READ       = 2,
    SYSTEM_CALL_EXECVE     = 3,
    SYSTEM_CALL_FORK       = 4,
    SYSTEM_CALL_GETPID     = 5,
    SYSTEM_CALL_TTY        = 6,
    SYSTEM_CALL_OPEN       = 7,
    SYSTEM_CALL_CLOSE      = 8,
    SYSTEM_CALL_WAITPID    = 9,
    SYSTEM_CALL_TOD        = 10,
    SYSTEM_CALL_SBRK       = 11,
    SYSTEM_CALL_ENVIRON    = 12,
    SYSTEM_CALL_LINK       = 13,
    SYSTEM_CALL_LSEEK      = 14,
    SYSTEM_CALL_STAT       = 15,
    SYSTEM_CALL_FSTAT      = 16,
    SYSTEM_CALL_KILL       = 17,
    SYSTEM_CALL_SIGNAL     = 18,
    SYSTEM_CALL_SIGRET     = 19,
    SYSTEM_CALL_MKDIR      = 20,
    SYSTEM_CALL_RMDIR      = 21,
    SYSTEM_CALL_CHDIR      = 22,
    SYSTEM_CALL_GETCWD     = 23,
    SYSTEM_CALL_UNLINK     = 24,
    SYSTEM_CALL_PAUSE      = 25,
    SYSTEM_CALL_SHM_CREATE = 26,
    SYSTEM_CALL_SHM_MAP    = 27,
    SYSTEM_CALL_SHM_UNMAP  = 28,
    SYSTEM_CALL_5HT_LIST_PROC = 29,
    SYSTEM_CALL_LISTDIR    = 30,
    SYSTEM_CALL_5HT_REQ_BUF = 31,
    SYSTEM_CALL_5HT_REL_BUF = 32,
    SYSTEM_CALL_5HT_RCFG_LAYER = 33,
    SYSTEM_CALL_5HT_QUERY_INFO = 34,
    SYSTEM_CALL_5HT_QUERY_LAYER = 35,
    SYSTEM_CALL_5HT_SET_FID = 36,
    SYSTEM_CALL_DUP        = 37,
    SYSTEM_CALL_PIPE       = 38,
    SYSTEM_CALL_GETUID     = 39,
    SYSTEM_CALL_GETGID     = 40,
    SYSTEM_CALL_GETEUID    = 41,
    SYSTEM_CALL_GETEGID    = 42,
    SYSTEM_CALL_SETUID     = 43,
    SYSTEM_CALL_SETGID     = 44,
    SYSTEM_CALL_SETEUID    = 45,
    SYSTEM_CALL_SETEGID    = 46,
    SYSTEM_CALL_GETGROUPS  = 47,
    SYSTEM_CALL_SETGROUPS  = 48,
    SYSTEM_CALL_CHMOD      = 49,
    SYSTEM_CALL_CHOWN      = 50,
    SYSTEM_CALL_UMASK      = 51,
    SYSTEM_CALL_UNAME      = 52,
    SYSTEM_CALL_SETHOSTNAME = 53,
    SYSTEM_CALL_5HT_PTY_OPEN    = 54,
    SYSTEM_CALL_5HT_PTY_SETATTR = 55,
    SYSTEM_CALL_5HT_PTY_GETATTR = 56,
    SYSTEM_CALL_5HT_PTY_WINSIZE = 57,
    SYSTEM_CALL_5HT_PTY_SETPGRP = 58,
    SYSTEM_CALL_ALARM      = 59,
    SYSTEM_CALL_SOCKET     = 60,
    SYSTEM_CALL_BIND       = 61,
    SYSTEM_CALL_LISTEN     = 62,
    SYSTEM_CALL_ACCEPT     = 63,
    SYSTEM_CALL_CONNECT    = 64,
    SYSTEM_CALL_SEND       = 65,
    SYSTEM_CALL_RECV       = 66,
    SYSTEM_CALL_SHUTDOWN   = 67,
    SYSTEM_CALL_SOCKETPAIR = 68,
    SYSTEM_CALL_SELECT     = 69,
    SYSTEM_CALL_POLL       = 70,
    SYSTEM_CALL_5HT_GRAB_INPUT = 71
};

/**
 * @brief Enumeration of write system calls.
 *
 * This enum defines the various write system calls available in the kernel.
 */
enum {
    WRITE_STDOUT = 1,
    WRITE_STDERR = 2
};

/**
 * @brief Enumeration of read system calls.
 *
 * This enum defines the various read system calls available in the kernel.
 */
enum {
    READ_STDIN = 0
};

enum {
    SEEK_SET = 0,
    SEEK_CUR = 1,
    SEEK_END = 2
};

void system_call(processor_context_t *ctx);
void poll_waiter_tick(void);
extern void isr_syscall(void);

#endif
