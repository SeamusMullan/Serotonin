#include "syscall.h"
#include "../stdio/stdio.h"
#include "../schedule/schedule.h"
#include <stdint.h>

void system_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    //preempt_disable();
    printfs(PRINT_STATUS_INFO, "[SYSCALL] Hello from syscall handler: arg1:%d, arg2:%d, arg3:%d, arg4:%d\n",arg1,arg2,arg3,arg4);
    //preempt_enable();
}