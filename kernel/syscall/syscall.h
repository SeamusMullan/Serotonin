#ifndef _KERNEL_SYSCALl
#define _KERNEL_SYSCALl

#include <stdint.h>
#include "../io/io.h"
#include "../schedule/schedule.h"

#define STDIN_BUFFER_SIZE 4096

enum {
    SYSTEM_CALL_EXIT     = 0,
    SYSTEM_CALL_WRITE    = 1,
    SYSTEM_CALL_READ     = 2,
    SYSTEM_CALL_EXECVE   = 3,
    SYSTEM_CALL_FORK     = 4,
    SYSTEM_CALL_GET_PID  = 5,
    SYSTEM_CALL_TTY      = 6,
    SYSTEM_CALL_OPEN     = 7,
    SYSTEM_CALL_CLOSE    = 8,
    SYSTEM_CALL_WAIT     = 9,
    SYSTEM_CALL_TOD      = 10
};

enum {
    EXIT_REGISTER_HANDLER = 0,
    EXIT_RAISE_SIGNAL     = 1,
    EXIT_HANDLER_RETURN   = 2
};

enum {
    WRITE_STDOUT = 0,
    WRITE_STDERR = 1,
    WRITE_FRMBUF = 2,
    WRITE_FS     = 3
};

enum {
    READ_STDIN = 0,
    READ_FS    = 1
};

void system_call(processor_context_t *ctx);
extern void isr_syscall(void);

#endif