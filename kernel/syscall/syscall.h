#ifndef _KERNEL_SYSCALl
#define _KERNEL_SYSCALl

#include <stdint.h>
#include "../io/io.h"

enum {
    SYSTEM_CALL_EXIT = 0,
    SYSTEM_CALL_WRITE = 1,
    SYSTEM_CALL_READ = 2
};

void system_call(processor_context_t *ctx);
extern void isr_syscall(void);

#endif