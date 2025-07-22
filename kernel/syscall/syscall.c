#include "syscall.h"
#include "../stdio/stdio.h"
#include "../schedule/schedule.h"
#include "../io/io.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../paging.h"
#include <stdint.h>

char* stdin_ptr = 0;
int stdin_idx = 0;

void handle_illegal_call(void) {
    printfs(PRINT_STATUS_WARNING,"Illegal system call from %s (pid=%d)!\n", current_task->name, current_task->pid);
    task_exit(EXIT_SIGKILL);
}

void system_call(processor_context_t *ctx) {
    preempt_disable();

    uint32_t operation = ctx->eax;
    uint32_t arg2      = ctx->ebx;
    uint32_t arg3      = ctx->ecx;
    uint32_t arg4      = ctx->edx;

    printfs(PRINT_STATUS_DEBUG, "[SYSCALL] eip=%p Recieved system call from %s (pid=%d): operation:%d, arg2:%p, arg3:%p, arg4:%p\n",ctx->eip,current_task->name, current_task->pid, operation,arg2,arg3,arg4);
    switch (operation) {
        case SYSTEM_CALL_EXIT:
            // should do validation at some point lol
            task_exit(arg2);
            break;
        case SYSTEM_CALL_WRITE:
            switch (arg2) {
                case WRITE_STDOUT:
                    printf("%s",arg3);
                    break;
                case WRITE_STDERR:
                    printfs(PRINT_STATUS_ERROR,"%s",arg3);
                    break;
                default:
                    handle_illegal_call();
                    __builtin_unreachable();
            }
            return;
        case SYSTEM_CALL_READ:
            switch (arg2) {
                case READ_STDIN:
                    if (arg3 > USER_SPACE_END) {
                        handle_illegal_call();
                        __builtin_unreachable();
                    }

                    memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
                    stdio_lck_t *syscall_stdio = (stdio_lck_t *)kernel_malloc(sizeof(stdio_lck_t));
                    syscall_stdio->stdin_ptr = (char*)arg3;
                    syscall_stdio->stdin_buf_size = arg4;
                    current_task->lck_ptr = (void*)syscall_stdio;
                    task_lock_acquire(stdin_lock);
                    __builtin_unreachable();
                default:
                    handle_illegal_call();
                    __builtin_unreachable();
            }
        case SYSTEM_CALL_FORK:
            memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
            process_control_block_t *pcb = task_fork(current_task);
            enqueue(pcb);
            ctx->eax = pcb->pid;
            pcb->processor_context->eax = 0;
            break;
        default:
            handle_illegal_call();
            __builtin_unreachable();
    }
    preempt_enable();
}