#include "syscall.h"
#include "../stdio/stdio.h"

void system_call(void) {
    printfs(PRINT_STATUS_INFO, "[SYSCALL] Hello from syscall handler!\n");
}