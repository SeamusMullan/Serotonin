/**
 * @file syscall_table.h
 * @brief System call number definitions for Serotonin OS
 *
 * Defines the system call numbers used by the kernel syscall dispatcher.
 * These numbers are passed in the EAX register when executing int 0x80.
 */

#ifndef _5HT_SYSCALL_TABLE
#define _5HT_SYSCALL_TABLE

/**
 * @brief System call number enumeration
 *
 * Each value corresponds to a specific kernel function that can be
 * invoked from user space via the int 0x80 software interrupt.
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
    SYSTEM_CALL_5HT_QUERY_LAYER = 35
};

#endif
