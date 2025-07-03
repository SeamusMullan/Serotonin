#include "syscall.h"
#include "../stdio/stdio.h"
#include "../schedule/schedule.h"

void system_call(void) {
    //preempt_disable();
    printfs(PRINT_STATUS_INFO, "[SYSCALL] Hello from syscall handler!\n");
    //preempt_enable();
}