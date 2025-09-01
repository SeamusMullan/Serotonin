#include "syscall.h"
#include "../stdio/stdio.h"
#include "../schedule/schedule.h"
#include "../io/io.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../vmm/paging_init.h"
#include "../string.h"
#include "../filesystem/vfs.h"
#include "../filesystem/user_fs/user_fs.h"
#include "../video/vbe/vbe.h"
#include <stdint.h>


static uint32_t next_fd = FIRST_FD;

/**
 * @brief Handle illegal system calls.
 *
 * This function is called when a task attempts to make an illegal system call.
 */
void handle_illegal_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    printfs(PRINT_STATUS_WARNING,"Illegal system call from %s (pid=%d)!\n", current_task->name, current_task->pid);
    printfs(PRINT_STATUS_WARNING,"EIP: %p\n", current_task->processor_context->eip);
    printfs(PRINT_STATUS_WARNING,"Args: %p %p %p %p\n", arg1, arg2, arg3, arg4);
    task_exit(EXIT_SIGKILL);
}

/**
 * @brief Handle exit system calls.
 *
 * @param arg2 The exit status.
 */
static void sys_exit(uint32_t arg2) {
    task_exit(arg2);
}

/**
 * @brief Write the framebuffer to a specific location.
 *
 * @param arg2 The framebuffer address.
 * @param arg3 The destination address.
 * @param arg4 The size of the framebuffer.
 * @param ctx The processor context.
 */
static void sys_write(uint32_t arg2, uint32_t arg3, uint32_t arg4, processor_context_t *ctx) {
    uint32_t fd = arg2;
    char* write_ptr = (char*)arg3;
    uint32_t buf_size = arg4;

    switch (arg2) {
        case WRITE_STDOUT:
            printf("%s", write_ptr);
            ctx->eax = buf_size;
            break;
        case WRITE_STDERR:
            printfs(PRINT_STATUS_ERROR, "%s", write_ptr);
            ctx->eax = buf_size;
            break;
        default:
            if (fd >= FD_MAX || current_task->fd_table[fd] == NULL) {
                handle_illegal_call(arg2, arg3, arg4, ctx->eip);
                __builtin_unreachable();
            }

            file_handle_t *handle = current_task->fd_table[fd];

            int written = vfs_write(handle->node, 0, buf_size, write_ptr);
            ctx->eax = written;
            break;
    }
}

/**
 * @brief Handle read system calls.
 *
 * @param arg2 The file descriptor.
 * @param arg3 The buffer address.
 * @param arg4 The size of the buffer.
 * @param ctx The processor context.
 */
static void sys_read(uint32_t arg2, uint32_t arg3, uint32_t arg4, processor_context_t *ctx) {
    uint32_t fd = arg2;
    char* read_ptr = (char*)arg3;
    uint32_t buf_size = arg4;
    if (read_ptr > USER_SPACE_END || fd >= FD_MAX) {
        handle_illegal_call(arg2, arg3, arg4, ctx->eip);
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
        handle_illegal_call(arg2, arg3, arg4, ctx->eip);
        __builtin_unreachable();
    }

    file_handle_t *handle = current_task->fd_table[fd];

    char* read_buf = kernel_malloc(buf_size);

    int read_bytes = vfs_read(handle->node, 0, buf_size, read_buf);

    memcpy(read_ptr, read_buf, buf_size);

    kernel_free(read_buf);
}

/**
 * @brief Handle fork system calls.
 *
 * @param ctx The processor context.
 */
static void sys_fork(processor_context_t *ctx) {
    memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
    process_control_block_t *pcb = task_fork(current_task);
    enqueue(pcb);
    ctx->eax = pcb->pid;
    pcb->processor_context->eax = 0;
}

/**
 * @brief Handle getpid system calls.
 *
 * @param ctx The processor context.
 */
static void sys_get_pid(processor_context_t *ctx) {
    ctx->eax = current_task->pid;
}

/**
 * @brief Handle open system calls.
 *
 * @param arg2 The file path.
 * @param arg3 The flags.
 * @param arg4 The mode.
 * @param ctx The processor context.
 */
static void sys_open(uint32_t arg2, uint32_t arg3, uint32_t arg4, processor_context_t *ctx) {
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

/**
 * @brief Handle close system calls.
 *
 * @param arg2 The file descriptor.
 */
static void sys_close(uint32_t arg2) {
    int fd = arg2;

    if (fd >= FD_MAX) {
        handle_illegal_call(arg2, 0, 0, 0);
        __builtin_unreachable();
    }

    file_handle_t *handle = current_task->fd_table[fd];
    close_fd(current_task, fd);

    vfs_close(handle->node);

    kernel_free(handle);
}

static void sys_execve(uint32_t arg2, uint32_t arg3, uint32_t arg4, processor_context_t *ctx) {
    const char *path = (const char*)arg2;
    const char **argv_temp = (const char**)arg3;
    const char **envp_temp = (const char**)arg4;

    int argc = 0;
    while (argv_temp && argv_temp[argc]) argc++;
    int envc = 0;
    while (envp_temp && envp_temp[envc]) envc++;

    const char **argv = (const char**)kernel_malloc(sizeof(uint32_t)*argc);
    const char **envp = (const char**)kernel_malloc(sizeof(uint32_t)*envc);

    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv_temp[i]) + 1;
        char *kstr = (char*)kernel_malloc(len);
        memcpy(kstr, argv_temp[i], len);
        argv[i] = kstr;
    }
    argv[argc] = NULL;

    for (int i = 0; i < envc; i++) {
        size_t len = strlen(envp_temp[i]) + 1;
        char *kstr = (char*)kernel_malloc(len);
        memcpy(kstr, envp_temp[i], len);
        envp[i] = kstr;
    }
    envp[envc] = NULL;

    address_space_t *oldas = current_task->address_space;
    memset(ctx, 0, sizeof(*ctx));
    ctx->ds          = USER_MODE_SEGMENT;
    ctx->es          = USER_MODE_SEGMENT;
    ctx->fs          = USER_MODE_SEGMENT;
    ctx->gs          = USER_MODE_SEGMENT;
    ctx->ss          = USER_MODE_SEGMENT; 
    ctx->stub_eflags = INIT_EFLAGS;
    ctx->eflags      = INIT_EFLAGS;
    ctx->cs          = USER_MODE_CODE_SEGMENT; 

    int execve_stat = kernel_load_elf(current_task, path, path, argv, argc, envp, envc);
    if (execve_stat) {
        destroy_address_space(oldas);
        printfs(PRINT_STATUS_DEBUG, "execve: executing %s, pid=%d\n", path, current_task->pid);
        kernel_free(argv);
        kernel_free(envp);
        task_yield(0);
    } else {
        printfs(PRINT_STATUS_WARNING, "execve: failed to load elf %s, pid=%d\n", path, current_task->pid);
    }
}

static void sys_sbrk(uint32_t arg2, processor_context_t *ctx) {
    uint32_t increment = arg2;
    uint32_t brk_start = current_task->brk_start;
    uint32_t old_brk = current_task->brk_end;
    uint32_t new_brk = old_brk + arg2;

    if (new_brk < brk_start || new_brk >= USER_HEAP_MAX) {
        ctx->eax = -1;
    }

    if (increment > 0) {
        for (uint32_t va = old_brk; va < new_brk; va += PAGE_SIZE) {
            uint32_t frame = (uint32_t)alloc_frame();
            map_page(current_task->address_space, va, frame, USER_PAGE_FLAGS, 0);
        }
    } else if (increment < 0) {
        for (uint32_t va = new_brk; va < old_brk; va += PAGE_SIZE) {
            uint32_t phys = get_mapping(current_task->address_space, va);
            if (phys) {
                unmap_page(current_task->address_space, va, 1);
                free_frame((void*)phys);
            }
        }
    }

    current_task->brk_end = new_brk;
    ctx->eax = old_brk;
}

/**
 * @brief Handle system calls.
 *
 * @param ctx The processor context.
 */
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
            sys_exit(arg2);
            break;
        case SYSTEM_CALL_WRITE:
            sys_write(arg2, arg3, arg4, ctx);
            return;
        case SYSTEM_CALL_READ:
            sys_read(arg2, arg3, arg4, ctx);
            return;
        case SYSTEM_CALL_EXECVE:
            sys_execve(arg2, arg3, arg4, ctx);
            return;
        case SYSTEM_CALL_FORK:
            sys_fork(ctx);
            break;
        case SYSTEM_CALL_GET_PID:
            sys_get_pid(ctx);
            break;
        case SYSTEM_CALL_OPEN:
            sys_open(arg2, arg3, arg4, ctx);
            break;
        case SYSTEM_CALL_CLOSE: 
            sys_close(arg2);
            break;
        case SYSTEM_CALL_SBRK:
            sys_sbrk(arg2, ctx);
            break;
        default:
            handle_illegal_call(arg2, arg3, arg4, ctx->eip);
            __builtin_unreachable();
    }

    preempt_enable();
}