#include "syscall.h"
#include "../stdio/stdio.h"
#include "../schedule/schedule.h"
#include "../io/io.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include <stdint.h>

char* stdin_ptr = 0;
int stdin_lock = 0;
int stdin_idx = 0;
process_control_block_t* stdin_pcb = 0;

void handle_illegal_call(void) {
    printfs(PRINT_STATUS_WARNING,"Illegal system call from %s (pid=%d)!\n", current_task->name, current_task->pid);
    task_exit(EXIT_SIGKILL);
}

void system_call(processor_context_t *ctx) {
    stdin_ptr = kernel_malloc(STDIN_BUFFER_SIZE);
    uint32_t operation = ctx->eax;
    uint32_t arg2      = ctx->ebx;
    uint32_t arg3      = ctx->ecx;
    uint32_t arg4      = ctx->edx;

    printfs(PRINT_STATUS_DEBUG, "[SYSCALL] Recieved system call from %s (pid=%d): operation:%d, arg2:%d, arg3:%d, arg4:%d\n",current_task->name, current_task->pid, operation,arg2,arg3,arg4);
    switch (operation) {
        case SYSTEM_CALL_EXIT:
            // should do validation at some point lol
            task_exit(arg2);
            return;
        case SYSTEM_CALL_WRITE:
            switch (arg2) {
                case WRITE_STDOUT:
                    printf("%s",arg3);
                    return;
                case WRITE_STDERR:
                    printfs(PRINT_STATUS_ERROR,"%s",arg3);
                    return;
                default:
                    handle_illegal_call();
                    __builtin_unreachable();
            }
            return;
        case SYSTEM_CALL_READ:
            switch (arg2) {
                case READ_STDIN:
                    stdin_lock = 1;
                    stdin_pcb = current_task;
                    memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
                    stdin_idx = 0;
                    stdin_ptr = (char*)arg3;
                    task_block();
                    __builtin_unreachable();
                default:
                    handle_illegal_call();
                    __builtin_unreachable();
            }

        default:
            handle_illegal_call();
            __builtin_unreachable();
    }
}