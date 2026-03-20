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
    SYSTEM_CALL_POLL       = 70
};

#endif
