#ifndef _KERNEL_SYSCALl
#define _KERNEL_SYSCALl

#include <stdint.h>
#include "../io/io.h"
#include "../schedule/schedule.h"

#define STDIN_BUFFER_SIZE 4096
#define FIRST_FD 3

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
    SYSTEM_CALL_SHM_UNMAP  = 28
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
extern void isr_syscall(void);

#endif
