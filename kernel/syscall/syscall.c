#include "syscall.h"
#include "../stdio/stdio.h"
#include "../schedule/schedule.h"
#include <stdint.h>

void system_call(uint32_t operation, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    printfs(PRINT_STATUS_DEBUG, "[SYSCALL] Recieved system call from %s (pid=%d): operation:%d, arg2:%d, arg3:%d, arg4:%d\n",current_task->name, current_task->pid, operation,arg2,arg3,arg4);
    switch (operation) {
        case SYSTEM_CALL_EXIT:
            // should do validation at some point lol
            task_exit(arg2);
            return;
        default:
            printfs(PRINT_STATUS_WARNING,"Illegal system call from %s (pid=%d)!\n", current_task->name, current_task->pid);
            task_exit(EXIT_SIGKILL);
            return;
    }
}