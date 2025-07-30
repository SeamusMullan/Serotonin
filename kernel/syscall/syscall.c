#include "syscall.h"
#include "../stdio/stdio.h"
#include "../schedule/schedule.h"
#include "../io/io.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../paging.h"
#include "../string.h"
#include "../filesystem/vfs.h"
#include "../filesystem/user_fs/user_fs.h"
#include "../video/vbe/vbe.h"
#include <stdint.h>

static uint32_t next_fd = FIRST_FD;

void handle_illegal_call(void) {
    printfs(PRINT_STATUS_WARNING,"Illegal system call from %s (pid=%d)!\n", current_task->name, current_task->pid);
    task_exit(EXIT_SIGKILL);
}

static void handle_exit(uint32_t arg2) {
    task_exit(arg2);
}

static void frmbuf_write(uint32_t arg3, uint32_t arg4) {
    uint32_t zbuf = arg3;
    uint32_t* frmbufptr = (uint32_t*)arg4;

    uint32_t* krnl_frm_buf = (uint32_t *)kernel_malloc(fb_size_bytes);
    memcpy(krnl_frm_buf, frmbufptr, fb_size_bytes);
    memcpy(vbe_info.backbuffer, krnl_frm_buf, fb_size_bytes);
    vbe_flip_all();
    kernel_free(krnl_frm_buf);

    return;
}

static void handle_write(uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    switch (arg2) {
        case WRITE_STDOUT:
            printf("%s", (char*)arg3);
            break;
        case WRITE_STDERR:
            printfs(PRINT_STATUS_ERROR, "%s", (char*)arg3);
            break;
        case WRITE_FRMBUF:
            frmbuf_write(arg3, arg4);
            break;
        default:
            handle_illegal_call();
            __builtin_unreachable();
    }
}

static void handle_read(uint32_t arg2, uint32_t arg3, uint32_t arg4, processor_context_t *ctx) {
    uint32_t fd = arg2;
    char* read_ptr = (char*)arg3;
    uint32_t buf_size = arg4;
    if (read_ptr > USER_SPACE_END || fd >= FD_MAX) {
        handle_illegal_call();
        __builtin_unreachable();
    }

    if (fd == READ_STDIN) {
        memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
        stdio_lck_t *syscall_stdio = (stdio_lck_t *)kernel_malloc(sizeof(stdio_lck_t));
        syscall_stdio->stdin_ptr = read_ptr;
        syscall_stdio->stdin_buf_size = buf_size;
        current_task->lck_ptr = (void*)syscall_stdio;
        task_lock_acquire(stdin_lock);
        __builtin_unreachable();
    }

    if (current_task->fd_table[fd] == NULL) {
        handle_illegal_call();
        __builtin_unreachable();
    }

    file_handle_t *handle = current_task->fd_table[fd];

    char* read_buf = kernel_malloc(buf_size);

    int read_bytes = vfs_read(handle->node, handle->offset, buf_size, read_buf);
    if (read_bytes > 0) {
        handle->offset += read_bytes;
    }

    memcpy(read_ptr, read_buf, buf_size);

    kernel_free(read_buf);
}

static void handle_fork(processor_context_t *ctx) {
    memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
    process_control_block_t *pcb = task_fork(current_task);
    enqueue(pcb);
    ctx->eax = pcb->pid;
    pcb->processor_context->eax = 0;
}

static void handle_get_pid(processor_context_t *ctx) {
    ctx->eax = current_task->pid;
}

static void handle_open(uint32_t arg2, uint32_t arg3, uint32_t arg4, processor_context_t *ctx) {
    char *path = (char*)arg2;
    int flags = (int)arg3;
    int mode = (int)arg4;

    vfs_node_t *node = vfs_open(path);
    if (!node)
        ctx->eax = (uint32_t)-1;

    file_handle_t *handle = kernel_malloc(sizeof(file_handle_t));
    if (!handle) {
        vfs_close(node);
        ctx->eax = (uint32_t)-1;
    }

    handle->node = node;
    handle->flags = flags;
    handle->offset = 0;
    handle->refcount = 1;

    int fd = alloc_fd(current_task, handle);
    if (fd < 0) {
        vfs_close(node);
        kernel_free(handle);
    }

    ctx->eax = (uint32_t)fd;
}

void system_call(processor_context_t *ctx) {
    preempt_disable();

    uint32_t operation = ctx->eax;
    uint32_t arg2      = ctx->ebx;
    uint32_t arg3      = ctx->ecx;
    uint32_t arg4      = ctx->edx;

    printfs(PRINT_STATUS_DEBUG, "[SYSCALL] eip=%p Recieved system call from %s (pid=%d): operation:%d, arg2:%p, arg3:%p, arg4:%p\n",
            ctx->eip, current_task->name, current_task->pid, operation, arg2, arg3, arg4);

    switch (operation) {
        case SYSTEM_CALL_EXIT:
            handle_exit(arg2);
            break;
        case SYSTEM_CALL_WRITE:
            handle_write(arg2, arg3, arg4);
            return;
        case SYSTEM_CALL_READ:
            handle_read(arg2, arg3, arg4, ctx);
            return;
        case SYSTEM_CALL_FORK:
            handle_fork(ctx);
            break;
        case SYSTEM_CALL_GET_PID:
            handle_get_pid(ctx);
            break;
        case SYSTEM_CALL_OPEN:
            handle_open(arg2, arg3, arg4, ctx);
            break;
        default:
            handle_illegal_call();
            __builtin_unreachable();
    }

    preempt_enable();
}