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
#include "../io/serial.h"
#include "sys/errno.h"
#include "sys/types.h"
#include "sys/timespec.h"
#include "sys/file.h"
#include <stdint.h>


static uint32_t next_fd = FIRST_FD;
static int errno = 0;

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
            vbe_terminal_puts(write_ptr, buf_size);
            for (uint32_t i = 0; i < buf_size; i++) {
                serial_putchar(COM1_BASE, write_ptr[i]);
            }
            errno = buf_size;
            vbe_flip();
            break;
        case WRITE_STDERR:
            for (uint32_t i = 0; i < buf_size; i++) {
                vbe_terminal_putchar(write_ptr[i]);
                serial_putchar(COM1_BASE, write_ptr[i]);
            }
            errno = buf_size;
            break;
        default:
            if (fd >= FD_MAX || current_task->fd_table[fd] == NULL) {
                handle_illegal_call(arg2, arg3, arg4, ctx->eip);
                __builtin_unreachable();
            }

            file_handle_t *handle = current_task->fd_table[fd];

            int written = vfs_write(handle->node, handle->offset, buf_size, write_ptr);

            handle->offset += written;
            errno = written;
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
    errno = pcb->pid;
    pcb->processor_context->eax = 0;
}

/**
 * @brief Handle getpid system calls.
 *
 * @param ctx The processor context.
 */
static void sys_get_pid(processor_context_t *ctx) {
    errno = current_task->pid;
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
    if (!node) {
        if (flags & O_CREAT) {
            node = vfs_create(path);
            goto nodeCreated;
        }
        errno = -ENOENT;
        return;
    }

nodeCreated:

    file_handle_t *handle = kernel_malloc(sizeof(file_handle_t));
    if (!handle) {
        vfs_close(node);
        errno = -EIO;
        return;
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

    errno = fd;

    return;
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
    size_t path_size = strlen((char*)arg2)+1;
    char *path = (char*)kernel_malloc(path_size);
    strncpy(path, (char*)arg2, path_size);
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
    current_task->brk_start   = USER_HEAP_START;
    current_task->brk_end     = USER_HEAP_START;

    int execve_stat = kernel_load_elf(current_task, path, path, argv, argc, envp, envc);
    if (execve_stat) {
        destroy_address_space(oldas);
        printfs(PRINT_STATUS_DEBUG, "execve: executing %s, pid=%d\n", path, current_task->pid);
        kernel_free(argv);
        kernel_free(envp);
        kernel_free(path);
        task_yield(0);
    } else {
        printfs(PRINT_STATUS_WARNING, "execve: failed to load elf %s, pid=%d\n", path, current_task->pid);
        kernel_free(argv);
        kernel_free(envp);
        kernel_free(path);
        errno = -EIO;
    }
}

static void sys_sbrk(uint32_t arg2, processor_context_t *ctx) {
    int increment = (int)arg2;
    uint32_t brk_start = current_task->brk_start;
    uint32_t old_brk = current_task->brk_end;
    uint32_t new_brk = old_brk + arg2;

    if (new_brk < brk_start || new_brk >= USER_HEAP_MAX) {
        errno = -ENOMEM;
    }

    if (increment > 0) {
        for (uint32_t va = old_brk; va < new_brk; va += PAGE_SIZE) {
            uint32_t frame = (uint32_t)alloc_frame();
            map_page(current_task->address_space, va, frame, USER_PAGE_FLAGS, 1);
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
    errno = old_brk;
}

static void sys_waitpid(uint32_t arg2, uint32_t arg3, processor_context_t *ctx) {
    lock_scheduler();
    int pid = arg2;
    int* status_ptr = (int*)arg3;
    process_control_block_t *target = task_list;
    while (target && target->pid != pid)
        target = target->next;

    if (!target) {
        errno = -ESRCH;
        return;
    }

    if (target->state != PROCESS_STATE_TERMINATED) {
        current_task->waiting_on = pid;
        current_task->status_ptr = status_ptr;
        memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
        task_block();
        __builtin_unreachable();
    }

    unlock_scheduler();
    return;
}

static void sys_lseek(uint32_t arg2, uint32_t arg3, uint32_t arg4, processor_context_t *ctx) {
    int fd = arg2;
    int offset = (uint32_t)arg3;
    int whence = (uint32_t)arg4;

    if (fd >= FD_MAX || current_task->fd_table[fd] == NULL) {
        handle_illegal_call(arg2, arg3, arg4, ctx->eip);
        __builtin_unreachable();
    }

    file_handle_t *handle = current_task->fd_table[fd];

    int new_offset;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        default:
            handle_illegal_call(arg2, arg3, arg4, ctx->eip);
            __builtin_unreachable();
    }

    handle->offset = new_offset;
    errno = new_offset;
}

static void sys_fstat(uint32_t arg2, uint32_t arg3, processor_context_t *ctx) {
    int fd = arg2;
    struct stat *statbuf = (struct stat*)arg3;

    if ((fd >= FD_MAX || current_task->fd_table[fd] == NULL) && fd > 2) {
        errno = -EBADF;
        return;
    }

    struct stat *k_statbuf = (struct stat*)kernel_malloc_align(sizeof(struct stat), 16);
    memset(k_statbuf, 0, sizeof(struct stat));

    if (fd < 3) {
        k_statbuf->st_mode = S_IFCHR;
        k_statbuf->st_blksize = 1024;
    } else {
        file_handle_t *handle = current_task->fd_table[fd];
        vfs_node_t *node = handle->node;

        k_statbuf->st_dev = (dev_t)(uintptr_t)node->fs;
        k_statbuf->st_ino = node->inode;

        if (node->flags & VFS_FLAG_DIRECTORY) {
            k_statbuf->st_mode = S_IFDIR | 0755;
        } else if (node->flags & VFS_FLAG_SYMLINK) {
            k_statbuf->st_mode = S_IFLNK | 0777;
        } else {
            k_statbuf->st_mode = S_IFREG | 0644;
        }

        k_statbuf->st_size = node->size;
        k_statbuf->st_blksize = 4096; // some reasonable value lol
        k_statbuf->st_blocks = (node->size + 511) / 512;
    }

    memcpy(statbuf, k_statbuf, sizeof(struct stat));
    kernel_free(k_statbuf);

    errno = 0;
}

static void sys_isatty(uint32_t arg2) {
    uint32_t fd = arg2;
    if (fd < 3) {
        errno = 1;
    } else {
        errno = 0;
    }
}

/**
 * @brief Handle system calls.
 *
 * @param ctx The processor context.
 */
void system_call(processor_context_t *ctx) {
    preempt_disable();

    errno = 0;

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
            break;
        case SYSTEM_CALL_READ:
            sys_read(arg2, arg3, arg4, ctx);
            break;
        case SYSTEM_CALL_EXECVE:
            sys_execve(arg2, arg3, arg4, ctx);
            break;
        case SYSTEM_CALL_FORK:
            sys_fork(ctx);
            break;
        case SYSTEM_CALL_GETPID:
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
        case SYSTEM_CALL_WAITPID:
            sys_waitpid(arg2, arg3, ctx);
            break;
        case SYSTEM_CALL_LSEEK:
            sys_lseek(arg2, arg3, arg4, ctx);
            break;
        case SYSTEM_CALL_FSTAT:
            sys_fstat(arg2, arg3, ctx);
            break;
        case SYSTEM_CALL_TTY:
            sys_isatty(arg2);
            break;
        default:
            handle_illegal_call(arg2, arg3, arg4, ctx->eip);
            __builtin_unreachable();
    }

    ctx->eax = errno;

    printfs(PRINT_STATUS_DEBUG, "[SYSCALL] exiting kernel\n");

    preempt_enable();
}
