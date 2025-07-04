#ifndef _KERNEL_SYSCALl
#define _KERNEL_SYSCALl
#include <stdint.h>

void system_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
extern void isr_syscall(void);

#endif