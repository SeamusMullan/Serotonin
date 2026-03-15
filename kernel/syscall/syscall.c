#include "syscall.h"
#include "../stdio/stdio.h"
#include "../schedule/schedule.h"
#include "../io/io.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../vmm/paging_init.h"
#include "../vmm/vmm.h"
#include "../string.h"
#include "../filesystem/vfs.h"
#include "../filesystem/vfs_perm.h"
#include "../filesystem/user_fs/user_fs.h"
#include "../video/vbe/vbe.h"
#include "../io/serial.h"
#include "sys/errno.h"
#include "sys/types.h"
#include "sys/timespec.h"
#include "sys/file.h"
#include "sys/lib5ht.h"
#include <stdint.h>


static uint32_t next_fd = FIRST_FD;
static int errno = 0;
#define PIPE_BUFFER_SIZE 4096
#define HOST_NAME_MAX 64

struct utsname {
    char sysname[65];
    char nodename[HOST_NAME_MAX + 1];
    char release[65];
    char version[65];
    char machine[65];
};

static char kernel_hostname[HOST_NAME_MAX + 1] = "serotonin";

typedef struct layer_state {
    uint8_t allocated;
    uint32_t owner_pid;
    fb_layer_config_t cfg;
    shm_object_t *fb_shm;
    shm_object_t *meta_shm;
    uint32_t fb_user_va;
    uint32_t meta_user_va;
    uint32_t fb_priv_va;
    uint32_t meta_priv_va;
    uint32_t fb_size;
    uint32_t meta_size;
    uint32_t priv_region_size;
} layer_state_t;

static layer_state_t layer_states[VBE_NUM_Z_LAYERS];

static uint32_t layer_priv_base(void) {
    return FB_VMA_BASE + align_up(fb_size_bytes, PAGE_SIZE);
}

static uint32_t layer_priv_end(void) {
    return KERNEL_STACK_VMA;
}

static uint32_t layer_priv_find_free(uint32_t size) {
    uint32_t addr = align_up(layer_priv_base(), PAGE_SIZE);
    uint32_t end = layer_priv_end();

    while (addr + size <= end) {
        uint32_t next_start = end;
        uint32_t next_size = 0;
        int found = 0;

        for (uint32_t i = 0; i < VBE_NUM_Z_LAYERS; i++) {
            if (!layer_states[i].allocated)
                continue;
            uint32_t start = layer_states[i].fb_priv_va;
            uint32_t stop = start + layer_states[i].priv_region_size;
            if (stop <= addr)
                continue;
            if (start <= addr && stop > addr) {
                addr = align_up(stop, PAGE_SIZE);
                found = 1;
                break;
            }
            if (start < next_start) {
                next_start = start;
                next_size = layer_states[i].priv_region_size;
                found = 1;
            }
        }

        if (!found || addr + size <= next_start)
            return addr;

        addr = align_up(next_start + next_size, PAGE_SIZE);
    }

    return 0;
}

static void layer_map_priv_shared(uint32_t vaddr, uint32_t *phys_pages, uint32_t npages) {
    address_space_t *as = current_task->address_space;
    for (uint32_t i = 0; i < npages; i++) {
        map_page(as, vaddr + i * PAGE_SIZE, phys_pages[i], PAGE_FLAGS, 1);
    }
}

static void layer_unmap_priv_shared(uint32_t vaddr, uint32_t npages) {
    address_space_t *as = current_task->address_space;
    for (uint32_t i = 0; i < npages; i++) {
        unmap_page(as, vaddr + i * PAGE_SIZE, 0);
    }
}

static void layer_remove_shmem_map(address_space_t *as, uint32_t start) {
    shmem_map_t **pp = &as->shmem_list;
    while (*pp) {
        if ((*pp)->start == start) {
            shmem_map_t *m = *pp;
            *pp = m->next;
            kernel_free(m);
            return;
        }
        pp = &(*pp)->next;
    }
}

static void layer_unmap_user(address_space_t *as, uint32_t vaddr, uint32_t size) {
    uint32_t sz = align_up(size, PAGE_SIZE);
    for (uint32_t off = 0; off < sz; off += PAGE_SIZE) {
        unmap_page(as, vaddr + off, 0);
    }
    layer_remove_shmem_map(as, vaddr);
}

static void layer_free_shm(shm_object_t *shm) {
    if (!shm)
        return;
    for (uint32_t i = 0; i < shm->npages; i++) {
        free_frame((void*)shm->phys_pages[i]);
    }
    kernel_free(shm->phys_pages);
    kernel_free(shm);
}

static void layer_release_state(uint16_t id, layer_state_t *state, address_space_t *as) {
    if (!state->allocated)
        return;

    if (state->fb_shm)
        layer_unmap_priv_shared(state->fb_priv_va, state->fb_shm->npages);
    if (state->meta_shm)
        layer_unmap_priv_shared(state->meta_priv_va, state->meta_shm->npages);

    if (as) {
        if (state->fb_user_va && state->fb_shm)
            layer_unmap_user(as, state->fb_user_va, state->fb_shm->size);
        if (state->meta_user_va && state->meta_shm)
            layer_unmap_user(as, state->meta_user_va, state->meta_shm->size);
    }

    layer_free_shm(state->fb_shm);
    layer_free_shm(state->meta_shm);
    memset(state, 0, sizeof(*state));
}

static int layer_config_valid(const fb_layer_config_t *cfg) {
    if (!cfg)
        return 0;
    if (cfg->size < sizeof(*cfg))
        return 0;
    if (cfg->x1 <= cfg->x0 || cfg->y1 <= cfg->y0)
        return 0;
    if (cfg->x1 > vbe_info.width || cfg->y1 > vbe_info.height)
        return 0;
    if ((cfg->stride & (sizeof(uint32_t) - 1)) != 0)
        return 0;
    if (cfg->stride < (uint32_t)(cfg->x1 - cfg->x0) * sizeof(uint32_t))
        return 0;
    return 1;
}

static int layer_prepare_state(uint16_t id, const fb_layer_config_t *cfg, layer_state_t *state) {
    uint32_t width = (uint32_t)(cfg->x1 - cfg->x0);
    uint32_t height = (uint32_t)(cfg->y1 - cfg->y0);
    uint32_t fb_size = cfg->stride * height;
    uint32_t meta_size = sizeof(fb_layer_metadata_t);
    uint32_t fb_alloc = align_up(fb_size, PAGE_SIZE);
    uint32_t meta_alloc = align_up(meta_size, PAGE_SIZE);
    uint32_t region_size = fb_alloc + meta_alloc;

    uint32_t priv_base = layer_priv_find_free(region_size);
    if (!priv_base)
        return -ENOMEM;

    shm_object_t *fb_shm = shm_create(fb_alloc);
    if (!fb_shm)
        return -ENOMEM;

    shm_object_t *meta_shm = shm_create(meta_alloc);
    if (!meta_shm) {
        layer_free_shm(fb_shm);
        return -ENOMEM;
    }

    uint32_t fb_user_va = shm_map(current_task, fb_shm);
    uint32_t meta_user_va = shm_map(current_task, meta_shm);

    uint32_t fb_priv_va = priv_base;
    uint32_t meta_priv_va = priv_base + fb_alloc;

    layer_map_priv_shared(fb_priv_va, fb_shm->phys_pages, fb_shm->npages);
    layer_map_priv_shared(meta_priv_va, meta_shm->phys_pages, meta_shm->npages);

    memset((void*)fb_priv_va, 0, fb_size);
    memset((void*)meta_priv_va, 0, meta_alloc);

    memset(state, 0, sizeof(*state));
    state->allocated = 1;
    state->owner_pid = current_task->pid;
    state->cfg = *cfg;
    state->fb_shm = fb_shm;
    state->meta_shm = meta_shm;
    state->fb_user_va = fb_user_va;
    state->meta_user_va = meta_user_va;
    state->fb_priv_va = fb_priv_va;
    state->meta_priv_va = meta_priv_va;
    state->fb_size = fb_size;
    state->meta_size = meta_size;
    state->priv_region_size = region_size;

    return 0;
}

static void layer_fill_info(uint16_t id, fb_layer_info_t *info) {
    memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    info->layer_id = id;

    if (id < VBE_NUM_Z_LAYERS && layer_states[id].allocated) {
        layer_state_t *state = &layer_states[id];
        info->owned = 1;
        info->fb_user_va = state->fb_user_va;
        info->metadata_user_va = state->meta_user_va;
        info->fb_size = state->fb_size;
        info->metadata_size = state->meta_size;
        info->cfg = state->cfg;
    } else {
        info->owned = 0;
        info->cfg.size = sizeof(info->cfg);
    }
}

static void fill_stat_from_node(vfs_node_t *node, struct stat *k_statbuf) {
    memset(k_statbuf, 0, sizeof(*k_statbuf));

    if (node->mode != 0) {
        k_statbuf->st_mode = node->mode;
    } else if (node->flags & VFS_FLAG_PIPE) {
        k_statbuf->st_mode = S_IFIFO | 0666;
    } else if (node->flags & VFS_FLAG_DIRECTORY) {
        k_statbuf->st_mode = S_IFDIR | 0755;
    } else if (node->flags & VFS_FLAG_SYMLINK) {
        k_statbuf->st_mode = S_IFLNK | 0777;
    } else {
        k_statbuf->st_mode = S_IFREG | 0644;
    }

    k_statbuf->st_uid = node->uid;
    k_statbuf->st_gid = node->gid;
    k_statbuf->st_dev = (dev_t)(uintptr_t)node->fs;
    k_statbuf->st_ino = node->inode;
    k_statbuf->st_size = node->size;
    k_statbuf->st_blksize = 4096;
    k_statbuf->st_blocks = (node->size + 511) / 512;
}

static int dir_has_entries(vfs_node_t *node) {
    if (!node || !node->ops || !node->ops->readdir) return 0;

    for (uint32_t i = 0; ; i++) {
        vfs_node_t *child = node->ops->readdir(node, i);
        if (!child) break;

        if (strcmp(child->name, ".") != 0 && strcmp(child->name, "..") != 0) {
            vfs_close(child);
            return 1;
        }

        vfs_close(child);
    }

    return 0;
}

static int build_abs_path(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return -1;

    if (path[0] == '/') {
        size_t len = strlen(path);
        if (len >= out_size) return -1;
        strcpy(out, path);
        return 0;
    }

    const char *cwd = current_task->cwd;
    size_t cwd_len = strlen(cwd);
    size_t path_len = strlen(path);
    size_t extra = (cwd_len > 1) ? 1 : 0;

    if (cwd_len + extra + path_len + 1 > out_size) return -1;

    strcpy(out, cwd);
    if (extra) {
        strcat(out, "/");
    }
    strcat(out, path);
    return 0;
}

/**
 * @brief Handle illegal system calls.
 *
 * This function is called when a task attempts to make an illegal system call.
 */
void handle_illegal_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    printfs(PRINT_STATUS_WARNING,"Illegal system call from %s (pid=%d)!\n", current_task->name, current_task->pid);
    printfs(PRINT_STATUS_WARNING,"EIP: %p\n", current_task->processor_context->eip);
    printfs(PRINT_STATUS_WARNING,"Args: %p %p %p %p\n", arg1, arg2, arg3, arg4);
    task_exit(current_task,EXIT_SIGKILL);
}

/**
 * @brief Handle exit system calls.
 *
 * @param arg2 The exit status.
 */
static void sys_exit(uint32_t arg2) {
    task_exit(current_task,arg2);
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

    if (fd >= FD_MAX) {
        errno = -EBADF;
        return;
    }

    if (current_task->fd_table[fd] != NULL) {
        file_handle_t *handle = current_task->fd_table[fd];
        int access = handle->flags & 0x3;
        if (access == O_RDONLY) {
            errno = -EBADF;
            return;
        }

        if (handle->flags & O_APPEND) {
            handle->offset = handle->node->size;
        }

        char *kbuf = (char*)kernel_malloc(buf_size);
        if (!kbuf) {
            errno = -ENOMEM;
            return;
        }
        if (copy_from_user(current_task->address_space, kbuf, (uint32_t)write_ptr, buf_size) != 0) {
            kernel_free(kbuf);
            errno = -EFAULT;
            return;
        }

        int written = vfs_write(handle->node, handle->offset, buf_size, kbuf);
        kernel_free(kbuf);
        if (written < 0) {
            errno = (written == -1) ? -EIO : written;
            return;
        }

        handle->offset += written;
        errno = written;
        return;
    }

    switch (arg2) {
        case WRITE_STDOUT:
            if (buf_size) {
                char *kbuf = (char*)kernel_malloc(buf_size);
                if (!kbuf) {
                    errno = -ENOMEM;
                    return;
                }
                if (copy_from_user(current_task->address_space, kbuf, (uint32_t)write_ptr, buf_size) != 0) {
                    kernel_free(kbuf);
                    errno = -EFAULT;
                    return;
                }
                vbe_terminal_puts(kbuf, buf_size);
                for (uint32_t i = 0; i < buf_size; i++) {
                    serial_putchar(COM1_BASE, kbuf[i]);
                }
                kernel_free(kbuf);
            }
            errno = (int)buf_size;
            break;
        case WRITE_STDERR:
            if (buf_size) {
                char *kbuf = (char*)kernel_malloc(buf_size);
                if (!kbuf) {
                    errno = -ENOMEM;
                    return;
                }
                if (copy_from_user(current_task->address_space, kbuf, (uint32_t)write_ptr, buf_size) != 0) {
                    kernel_free(kbuf);
                    errno = -EFAULT;
                    return;
                }
                for (uint32_t i = 0; i < buf_size; i++) {
                    vbe_terminal_putchar(kbuf[i]);
                    serial_putchar(COM1_BASE, kbuf[i]);
                }
                kernel_free(kbuf);
            }
            errno = (int)buf_size;
            break;
        default:
            errno = -EBADF;
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

    if (fd >= FD_MAX) {
        errno = -EBADF;
        return;
    }

    if (current_task->fd_table[fd] != NULL) {
        file_handle_t *handle = current_task->fd_table[fd];
        int access = handle->flags & 0x3;
        if (access == O_WRONLY) {
            errno = -EBADF;
            return;
        }

        char* read_buf = kernel_malloc(buf_size);
        if (!read_buf) {
            errno = -ENOMEM;
            return;
        }

    current_task->current_fd_flags = handle->flags;
    current_task->current_user_buf = (uint32_t)read_ptr;
    int read_bytes = vfs_read(handle->node, handle->offset, buf_size, read_buf);
    if (read_bytes < 0) {
        kernel_free(read_buf);
        errno = (read_bytes == -1) ? -EIO : read_bytes;
        return;
    }

        if (copy_to_user(current_task->address_space, (uint32_t)read_ptr, read_buf, (size_t)read_bytes) != 0) {
            kernel_free(read_buf);
            errno = -EFAULT;
            return;
        }
        handle->offset += read_bytes;
        errno = read_bytes;

        kernel_free(read_buf);
        return;
    }

    if (fd == READ_STDIN) {
        if (!read_ptr || buf_size == 0) {
            errno = -EINVAL;
            return;
        }
        uint32_t start = (uint32_t)read_ptr;
        uint32_t end = start + buf_size - 1;
        if (end < start || start < USER_SPACE_START || end > USER_SPACE_END) {
            errno = -EFAULT;
            return;
        }
        stdio_lck_t *syscall_stdio = (stdio_lck_t *)kernel_malloc(sizeof(stdio_lck_t));
        syscall_stdio->stdin_ptr = read_ptr;
        syscall_stdio->stdin_buf_size = buf_size;
        current_task->lck_ptr = (void*)syscall_stdio;
        task_lock_acquire(stdin_lock);
        return;
    }

    errno = -EBADF;
}

/**
 * @brief Handle fork system calls.
 *
 * @param ctx The processor context.
 */
static void sys_fork(processor_context_t *ctx) {
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
    char abs_path[256];

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    vfs_node_t *node = vfs_open(abs_path);
    if (!node) {
        if (flags & O_CREAT) {
            char parent_path[256], child_name[256];
            split_path(abs_path, parent_path, child_name);
            vfs_node_t *parent_node = vfs_resolve_path(parent_path);
            if (parent_node && vfs_check_dir_write(parent_node, current_task) != 0) {
                errno = -EACCES;
                return;
            }
            node = vfs_create(abs_path);
            goto nodeCreated;
        }
        errno = -ENOENT;
        return;
    }
    if ((flags & O_CREAT) && (flags & O_EXCL)) {
        vfs_close(node);
        errno = -EEXIST;
        return;
    }

    {
        int want = 0;
        int acc = flags & 0x3;
        if (acc == O_RDONLY || acc == O_RDWR) want |= PERM_READ;
        if (acc == O_WRONLY || acc == O_RDWR) want |= PERM_WRITE;
        if (want && vfs_check_permission(node, current_task, want) != 0) {
            vfs_close(node);
            errno = -EACCES;
            return;
        }
    }

nodeCreated:

    if ((flags & O_TRUNC) && ((flags & 0x3) != O_RDONLY) && (node->flags & VFS_FLAG_FILE)) {
        if (vfs_truncate(node, 0) != 0) {
            vfs_close(node);
            errno = -ENOSYS;
            return;
        }
    }

    file_handle_t *handle = kernel_malloc(sizeof(file_handle_t));
    if (!handle) {
        vfs_close(node);
        errno = -EIO;
        return;
    }

    handle->node = node;
    handle->flags = flags;
    handle->offset = (flags & O_APPEND) ? node->size : 0;
    handle->refcount = 0;

    int fd = alloc_fd(current_task, handle);

    if (fd < 0) {
        vfs_close(node);
        kernel_free(handle);
        errno = -EIO;
        return;
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

    if (fd >= FD_MAX || current_task->fd_table[fd] == NULL) {
        errno = -EBADF;
        return;
    }

    close_fd(current_task, fd);
}

static void sys_pipe(uint32_t arg2) {
    uint32_t pipe_addr = arg2;
    if (pipe_addr < USER_SPACE_START ||
        pipe_addr + (sizeof(int) * 2) - 1 > USER_SPACE_END) {
        errno = -EFAULT;
        return;
    }

    pipe_state_t *pipe = (pipe_state_t*)kernel_malloc(sizeof(*pipe));
    if (!pipe) {
        errno = -ENOMEM;
        return;
    }
    memset(pipe, 0, sizeof(*pipe));
    pipe->size = PIPE_BUFFER_SIZE;
    pipe->buffer = (char*)kernel_malloc(pipe->size);
    if (!pipe->buffer) {
        kernel_free(pipe);
        errno = -ENOMEM;
        return;
    }

    pipe_endpoint_t *read_ep = (pipe_endpoint_t*)kernel_malloc(sizeof(*read_ep));
    pipe_endpoint_t *write_ep = (pipe_endpoint_t*)kernel_malloc(sizeof(*write_ep));
    vfs_node_t *read_node = (vfs_node_t*)kernel_malloc(sizeof(*read_node));
    vfs_node_t *write_node = (vfs_node_t*)kernel_malloc(sizeof(*write_node));
    file_handle_t *read_handle = (file_handle_t*)kernel_malloc(sizeof(*read_handle));
    file_handle_t *write_handle = (file_handle_t*)kernel_malloc(sizeof(*write_handle));

    if (!read_ep || !write_ep || !read_node || !write_node || !read_handle || !write_handle) {
        if (read_handle) kernel_free(read_handle);
        if (write_handle) kernel_free(write_handle);
        if (read_node) kernel_free(read_node);
        if (write_node) kernel_free(write_node);
        if (read_ep) kernel_free(read_ep);
        if (write_ep) kernel_free(write_ep);
        kernel_free(pipe->buffer);
        kernel_free(pipe);
        errno = -ENOMEM;
        return;
    }

    read_ep->pipe = pipe;
    read_ep->is_read_end = 1;
    write_ep->pipe = pipe;
    write_ep->is_read_end = 0;

    memset(read_node, 0, sizeof(*read_node));
    memset(write_node, 0, sizeof(*write_node));
    read_node->flags = VFS_FLAG_FILE | VFS_FLAG_PIPE;
    write_node->flags = VFS_FLAG_FILE | VFS_FLAG_PIPE;
    read_node->ops = &task_ipc_pipe_ops;
    write_node->ops = &task_ipc_pipe_ops;
    read_node->fs_data = read_ep;
    write_node->fs_data = write_ep;
    read_node->uid = current_task->euid;
    read_node->gid = current_task->egid;
    read_node->mode = S_IFIFO | 0600;
    write_node->uid = current_task->euid;
    write_node->gid = current_task->egid;
    write_node->mode = S_IFIFO | 0600;

    memset(read_handle, 0, sizeof(*read_handle));
    memset(write_handle, 0, sizeof(*write_handle));
    read_handle->node = read_node;
    read_handle->flags = O_RDONLY;
    read_handle->offset = 0;
    read_handle->refcount = 0;
    write_handle->node = write_node;
    write_handle->flags = O_WRONLY;
    write_handle->offset = 0;
    write_handle->refcount = 0;

    int read_fd = alloc_fd(current_task, read_handle);
    if (read_fd < 0) {
        kernel_free(read_handle);
        kernel_free(write_handle);
        kernel_free(read_node);
        kernel_free(write_node);
        kernel_free(read_ep);
        kernel_free(write_ep);
        kernel_free(pipe->buffer);
        kernel_free(pipe);
        errno = -EMFILE;
        return;
    }
    pipe->readers = 1;

    int write_fd = alloc_fd(current_task, write_handle);
    if (write_fd < 0) {
        close_fd(current_task, read_fd);
        kernel_free(write_handle);
        kernel_free(write_node);
        kernel_free(write_ep);
        errno = -EMFILE;
        return;
    }
    pipe->writers = 1;

    int pipefd[2] = { read_fd, write_fd };
    if (copy_to_user(current_task->address_space, pipe_addr, pipefd, sizeof(pipefd)) != 0) {
        close_fd(current_task, read_fd);
        close_fd(current_task, write_fd);
        errno = -EFAULT;
        return;
    }

    errno = 0;
}

static void sys_dup(uint32_t arg2, uint32_t arg3) {
    int oldfd = (int)arg2;
    int newfd = (int)arg3;

    if (oldfd < 0 || oldfd >= FD_MAX || current_task->fd_table[oldfd] == NULL) {
        errno = -EBADF;
        return;
    }

    if (newfd < 0) {
        int allocated = alloc_fd(current_task, current_task->fd_table[oldfd]);
        errno = allocated;
        return;
    }

    if (newfd >= FD_MAX) {
        errno = -EBADF;
        return;
    }

    if (newfd == oldfd) {
        errno = newfd;
        return;
    }

    if (current_task->fd_table[newfd] != NULL) {
        close_fd(current_task, newfd);
    }

    current_task->fd_table[newfd] = current_task->fd_table[oldfd];
    current_task->fd_table[newfd]->refcount++;
    errno = newfd;
}

static void sys_execve(uint32_t arg2, uint32_t arg3, uint32_t arg4, processor_context_t *ctx) {
    char abs_path[256];
    if (build_abs_path((char*)arg2, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    size_t path_size = strlen(abs_path) + 1;
    char *path = (char*)kernel_malloc(path_size);
    strncpy(path, abs_path, path_size);

    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) {
        kernel_free(path);
        errno = -ENOENT;
        return;
    }

    if (vfs_check_permission(node, current_task, PERM_EXEC) != 0) {
        kernel_free(path);
        errno = -EACCES;
        return;
    }

    uint32_t exec_mode = node->mode;
    uint16_t exec_uid = node->uid;
    uint16_t exec_gid = node->gid;

    const char **argv_temp = (const char**)arg3;
    const char **envp_temp = (const char**)arg4;

    int argc = 0;
    while (argv_temp && argv_temp[argc]) argc++;
    int envc = 0;
    while (envp_temp && envp_temp[envc]) envc++;

    const char **argv = (const char**)kernel_malloc(sizeof(uint32_t)*(argc+1));
    const char **envp = (const char**)kernel_malloc(sizeof(uint32_t)*(envc+1));

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
    if (!execve_stat) {
        destroy_address_space(oldas);
        if (exec_mode & S_ISUID) current_task->euid = exec_uid;
        if (exec_mode & S_ISGID) current_task->egid = exec_gid;
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
        printf("lol:%p\n",current_task->processor_context->eip);
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
        return;
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
    process_control_block_t *target = task_lookup_by_pid(pid);

    if (!target) {
        errno = -ESRCH;
        unlock_scheduler();
        return;
    }

    if (target->state != PROCESS_STATE_TERMINATED) {
        current_task->waiting_on = pid;
        current_task->status_ptr = status_ptr;
        task_block();
        return;
    }

    unlock_scheduler();
    errno = 0;
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

    int64_t new_offset;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = (int64_t)handle->offset + offset;
            break;
        case SEEK_END:
            new_offset = (int64_t)handle->node->size + offset;
            break;
        default:
            handle_illegal_call(arg2, arg3, arg4, ctx->eip);
            __builtin_unreachable();
    }

    if (new_offset < 0) {
        errno = -EINVAL;
        return;
    }

    handle->offset = (uint32_t)new_offset;
    errno = handle->offset;
}

static void sys_fstat(uint32_t arg2, uint32_t arg3, processor_context_t *ctx) {
    int fd = arg2;
    struct stat *statbuf = (struct stat*)arg3;
    uint32_t stat_addr = (uint32_t)statbuf;

    if (fd >= FD_MAX || (current_task->fd_table[fd] == NULL && fd > 2)) {
        errno = -EBADF;
        return;
    }
    if (stat_addr < USER_SPACE_START ||
        stat_addr + sizeof(struct stat) - 1 > USER_SPACE_END) {
        errno = -EFAULT;
        return;
    }

    struct stat *k_statbuf = (struct stat*)kernel_malloc_align(16, sizeof(struct stat));

    if (fd < 3 && current_task->fd_table[fd] == NULL) {
        memset(k_statbuf, 0, sizeof(struct stat));
        k_statbuf->st_mode = S_IFCHR;
        k_statbuf->st_blksize = 1024;
    } else {
        file_handle_t *handle = current_task->fd_table[fd];
        vfs_node_t *node = handle->node;
        fill_stat_from_node(node, k_statbuf);
    }

    if (copy_to_user(current_task->address_space, stat_addr, k_statbuf, sizeof(struct stat)) != 0) {
        kernel_free_align(k_statbuf);
        errno = -EFAULT;
        return;
    }
    kernel_free_align(k_statbuf);

    errno = 0;
}

static void sys_stat(uint32_t arg2, uint32_t arg3) {
    char *path = (char*)arg2;
    struct stat *statbuf = (struct stat*)arg3;
    char abs_path[256];
    uint32_t stat_addr = (uint32_t)statbuf;

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }
    if (stat_addr < USER_SPACE_START ||
        stat_addr + sizeof(struct stat) - 1 > USER_SPACE_END) {
        errno = -EFAULT;
        return;
    }

    vfs_node_t *node = vfs_open(abs_path);
    if (!node) {
        errno = -ENOENT;
        return;
    }

    struct stat *k_statbuf = (struct stat*)kernel_malloc_align(16, sizeof(struct stat));
    fill_stat_from_node(node, k_statbuf);
    if (copy_to_user(current_task->address_space, stat_addr, k_statbuf, sizeof(struct stat)) != 0) {
        kernel_free_align(k_statbuf);
        vfs_close(node);
        errno = -EFAULT;
        return;
    }
    kernel_free_align(k_statbuf);
    vfs_close(node);

    errno = 0;
}

static void sys_isatty(uint32_t arg2) {
    uint32_t fd = arg2;
    if (fd < 3 && current_task->fd_table[fd] == NULL) {
        errno = 1;
    } else {
        errno = 0;
    }
}

static void sys_gettimeofday(uint32_t arg2) {
    struct timeval *timestr = (struct timeval*)arg2;
    timestr->tv_sec = unix_timestamp;
    timestr->tv_usec = 0;
    errno = 0;
}

static void sys_kill(uint32_t arg2, uint32_t arg3) {
    int pid = (int)arg2;
    int sig = (int)arg3;

    process_control_block_t *task = task_lookup_by_pid(pid);
    if (!task) {
        errno = -ESRCH;
        return;
    }

    if (sig >= 16) {
        errno = -EINVAL;
        return;
    }

    if (current_task->euid != 0 &&
        current_task->euid != task->uid &&
        current_task->uid != task->uid) {
        errno = -EPERM;
        return;
    }

    task_ipc_signal_raise(task, sig);
    return;
}

static void sys_signal(uint32_t arg2, uint32_t arg3) {
    int sig = (int)arg2;
    uint32_t handler = arg3;

    task_ipc_register_signal_handler(current_task, sig, handler);

    return;
}

static void sys_sigret(processor_context_t *ctx) {
    process_control_block_t *task = current_task;

    memcpy(task->processor_context, task->signal_processor_context, sizeof(processor_context_t));
    memcpy(ctx, task->signal_processor_context, sizeof(processor_context_t));
    memcpy(&task->fpu_fx, &task->signal_fpu_fx, sizeof(fpu_fxsave_area_t));

    errno = (int)ctx->eax;

    task->in_signal_handler = 0;

    return;
}

static void sys_pause(void) {
    task_yield(0);
    return;
}

static void sys_shm_create(uint32_t arg2) {
    uint32_t size = arg2;
    int shm_id = shm_alloc_id();
    if (shm_id < 0) {
        errno = -ENOMEM;
        return;
    }

    shm_object_t *shm = shm_create(size);
    shm_table[shm_id] = shm;

    errno = shm_id;
}

static void sys_shm_map(uint32_t arg2) {
    int shm_id = (int)arg2;
    if (shm_id < 0 || shm_id >= MAX_SHM_OBJECTS || shm_table[shm_id] == NULL) {
        errno = -EINVAL;
        return;
    }

    shm_object_t *shm = shm_table[shm_id];
    uint32_t va = shm_map(current_task, shm);

    errno = va;
}

static void sys_shm_unmap(uint32_t arg2) {
    //shm_unmap(current_task, arg2);
    errno = -ENOSYS;
}

static void sys_mkdir(uint32_t arg2) {
    char *path = (char*)arg2;
    char abs_path[256];

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    if (vfs_resolve_path(abs_path)) {
        errno = -EEXIST;
        return;
    }

    char parent_path[256], child_name[256];
    split_path(abs_path, parent_path, child_name);
    vfs_node_t *parent_node = vfs_resolve_path(parent_path);
    if (parent_node && vfs_check_dir_write(parent_node, current_task) != 0) {
        errno = -EACCES;
        return;
    }

    if (vfs_mkdir(abs_path) != 0) {
        errno = -EIO;
        return;
    }

    errno = 0;
}

static void sys_unlink(uint32_t arg2) {
    char *path = (char*)arg2;
    char abs_path[256];

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    vfs_node_t *node = vfs_resolve_path(abs_path);
    if (!node) {
        errno = -ENOENT;
        return;
    }
    if (node->flags & VFS_FLAG_DIRECTORY) {
        errno = -EISDIR;
        return;
    }

    char parent_path[256], child_name[256];
    split_path(abs_path, parent_path, child_name);
    vfs_node_t *parent_node = vfs_resolve_path(parent_path);
    if (parent_node) {
        if (vfs_check_dir_write(parent_node, current_task) != 0) {
            errno = -EACCES;
            return;
        }
        if ((parent_node->mode & S_ISVTX) && current_task->euid != 0 &&
            current_task->euid != node->uid && current_task->euid != parent_node->uid) {
            errno = -EACCES;
            return;
        }
    }

    if (vfs_unlink(abs_path) != 0) {
        errno = -EIO;
        return;
    }

    errno = 0;
}

static void sys_rmdir(uint32_t arg2) {
    char *path = (char*)arg2;
    char abs_path[256];

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    vfs_node_t *node = vfs_resolve_path(abs_path);
    if (!node) {
        errno = -ENOENT;
        return;
    }
    if (!(node->flags & VFS_FLAG_DIRECTORY)) {
        errno = -ENOTDIR;
        return;
    }

    if (!node->ops || !node->ops->rmdir) {
        errno = -ENOSYS;
        return;
    }

    char parent_path[256], child_name[256];
    split_path(abs_path, parent_path, child_name);
    vfs_node_t *parent_node = vfs_resolve_path(parent_path);
    if (parent_node) {
        if (vfs_check_dir_write(parent_node, current_task) != 0) {
            errno = -EACCES;
            return;
        }
        if ((parent_node->mode & S_ISVTX) && current_task->euid != 0 &&
            current_task->euid != node->uid && current_task->euid != parent_node->uid) {
            errno = -EACCES;
            return;
        }
    }

    if (dir_has_entries(node)) {
        errno = -ENOTEMPTY;
        return;
    }

    if (vfs_rmdir(abs_path) != 0) {
        errno = -EIO;
        return;
    }

    errno = 0;
}

static void sys_chdir(uint32_t arg2) {
    char *path = (char*)arg2;
    char abs_path[256];

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    vfs_node_t *node = vfs_resolve_path(abs_path);
    if (!node) {
        errno = -ENOENT;
        return;
    }
    if (!(node->flags & VFS_FLAG_DIRECTORY)) {
        errno = -ENOTDIR;
        return;
    }

    if (vfs_check_permission(node, current_task, PERM_EXEC) != 0) {
        errno = -EACCES;
        return;
    }

    char temp[256];
    strncpy(temp, abs_path, sizeof(temp));
    temp[sizeof(temp) - 1] = '\0';

    char *segments[64];
    size_t seg_count = 0;
    char *p = temp;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (!*p) break;

        char *start = p;
        while (*p && *p != '/') {
            p++;
        }
        if (*p) {
            *p = '\0';
            p++;
        }

        if (strcmp(start, ".") == 0) {
            continue;
        }
        if (strcmp(start, "..") == 0) {
            if (seg_count > 0) {
                seg_count--;
            }
            continue;
        }
        segments[seg_count++] = start;
    }

    char *dst = current_task->cwd;
    size_t remaining = sizeof(current_task->cwd);
    if (seg_count == 0) {
        if (remaining < 2) {
            errno = -ENAMETOOLONG;
            return;
        }
        *dst++ = '/';
        *dst = '\0';
    } else {
        for (size_t i = 0; i < seg_count; i++) {
            size_t len = strlen(segments[i]);
            if (remaining < len + 2) {
                errno = -ENAMETOOLONG;
                return;
            }
            *dst++ = '/';
            memcpy(dst, segments[i], len);
            dst += len;
            remaining -= len + 1;
        }
        *dst = '\0';
    }

    errno = 0;
}

static void sys_getcwd(uint32_t arg2, uint32_t arg3) {
    char *buffer = (char*)arg2;
    size_t size = (size_t)arg3;
    size_t len = strlen(current_task->cwd) + 1;

    if (!buffer || size == 0) {
        errno = -EINVAL;
        return;
    }
    if (len > size) {
        errno = -ERANGE;
        return;
    }

    if (copy_to_user(current_task->address_space, (uint32_t)buffer, current_task->cwd, len) != 0) {
        errno = -EFAULT;
        return;
    }
    errno = 0;
}

static void sys_listdir(uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    char *path = (char*)arg2;
    char *buf = (char*)arg3;
    size_t size = (size_t)arg4;
    char abs_path[256];

    if (!buf || size == 0) {
        errno = -EINVAL;
        return;
    }

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    vfs_node_t *node = vfs_resolve_path(abs_path);
    if (!node) {
        errno = -ENOENT;
        return;
    }
    if (!(node->flags & VFS_FLAG_DIRECTORY)) {
        errno = -ENOTDIR;
        return;
    }
    if (!node->ops || !node->ops->readdir) {
        errno = -ENOSYS;
        return;
    }

    size_t off = 0;
    for (uint32_t i = 0; ; i++) {
        vfs_node_t *child = node->ops->readdir(node, i);
        if (!child) break;

        size_t len = strlen(child->name);
        if (off + len + 1 >= size) {
            vfs_close(child);
            errno = off;
            return;
        }

        if (copy_to_user(current_task->address_space, (uint32_t)(buf + off), child->name, len) != 0) {
            vfs_close(child);
            errno = -EFAULT;
            return;
        }
        off += len;
        if (copy_to_user(current_task->address_space, (uint32_t)(buf + off), "\n", 1) != 0) {
            vfs_close(child);
            errno = -EFAULT;
            return;
        }
        off += 1;
        vfs_close(child);
    }

    if (off < size) {
        if (copy_to_user(current_task->address_space, (uint32_t)(buf + off), "\0", 1) != 0) {
            errno = -EFAULT;
            return;
        }
    }
    errno = off;
}

static void sys_5ht_list_proc(uint32_t arg2, uint32_t arg3) {
    proc_5ht_t *buf = (proc_5ht_t*)arg2;
    int count = 0;
    size_t max = (size_t)arg3;

    proc_5ht_t k_buf = {0};

    process_control_block_t *task = task_list;
    while (task) {
        if (count >= max)
            break;
        k_buf.pid = task->pid;
        k_buf.priv = task->priv;
        k_buf.priority = task->priority;
        strncpy(k_buf.name, task->name, 32);
        k_buf.name[31] = '\0';

        if (copy_to_user(current_task->address_space, (uint32_t)(&buf[count]), &k_buf, sizeof(k_buf)) != 0) {
            errno = -EFAULT;
            return;
        }

        count++;
        task = task->next;
    }

    errno = count;
}

static void sys_5ht_query_info(uint32_t arg2) {
    if (!arg2) {
        errno = -EINVAL;
        return;
    }

    fb_info_t info = {0};
    info.size = sizeof(info);
    info.fb_size = fb_size_bytes;
    info.layer_window_size = layer_priv_end() - layer_priv_base();
    info.metadata_size = sizeof(fb_layer_metadata_t);
    info.alignment = PAGE_SIZE;

    if (copy_to_user(current_task->address_space, arg2, &info, sizeof(info)) != 0) {
        errno = -EFAULT;
        return;
    }

    errno = 0;
}

static void sys_5ht_query_layer(uint32_t arg2, uint32_t arg3) {
    uint16_t id = (uint16_t)arg2;
    if (id == 0 || id >= VBE_NUM_Z_LAYERS || !arg3) {
        errno = -EINVAL;
        return;
    }

    fb_layer_info_t info;
    layer_fill_info(id, &info);
    if (copy_to_user(current_task->address_space, arg3, &info, sizeof(info)) != 0) {
        errno = -EFAULT;
        return;
    }

    errno = 0;
}

static void sys_5ht_req_buf(uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    uint16_t id = (uint16_t)arg2;
    if (id == 0 || id >= VBE_NUM_Z_LAYERS || !arg3 || !arg4) {
        errno = -EINVAL;
        return;
    }

    layer_state_t *state = &layer_states[id];
    if (state->allocated) {
        if (state->owner_pid != current_task->pid) {
            errno = -EBUSY;
            return;
        }
        fb_layer_info_t info;
        layer_fill_info(id, &info);
        if (copy_to_user(current_task->address_space, arg4, &info, sizeof(info)) != 0) {
            errno = -EFAULT;
            return;
        }
        errno = 0;
        return;
    }

    fb_layer_config_t cfg = {0};
    if (copy_from_user(current_task->address_space, &cfg, arg3, sizeof(cfg)) != 0) {
        errno = -EFAULT;
        return;
    }
    if (!layer_config_valid(&cfg)) {
        errno = -EINVAL;
        return;
    }

    int prep_rc = layer_prepare_state(id, &cfg, state);
    if (prep_rc != 0) {
        errno = prep_rc;
        return;
    }

    vbe_layer_attach((uint8_t)id, (uint32_t*)state->fb_priv_va, &state->cfg,
                     (fb_layer_metadata_t*)state->meta_priv_va);

    fb_layer_info_t info;
    layer_fill_info(id, &info);
    if (copy_to_user(current_task->address_space, arg4, &info, sizeof(info)) != 0) {
        vbe_layer_detach((uint8_t)id);
        layer_release_state(id, state, current_task->address_space);
        errno = -EFAULT;
        return;
    }

    errno = 0;
}

static void sys_5ht_rel_buf(uint32_t arg2) {
    uint16_t id = (uint16_t)arg2;
    if (id == 0 || id >= VBE_NUM_Z_LAYERS) {
        errno = -EINVAL;
        return;
    }

    layer_state_t *state = &layer_states[id];
    if (!state->allocated) {
        errno = -ENOENT;
        return;
    }
    if (state->owner_pid != current_task->pid) {
        errno = -EPERM;
        return;
    }

    vbe_layer_detach((uint8_t)id);
    layer_release_state(id, state, current_task->address_space);
    errno = 0;
}

static void sys_5ht_rcfg_layer(uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    uint16_t id = (uint16_t)arg2;
    if (id == 0 || id >= VBE_NUM_Z_LAYERS || !arg3 || !arg4) {
        errno = -EINVAL;
        return;
    }

    layer_state_t *state = &layer_states[id];
    if (!state->allocated) {
        errno = -ENOENT;
        return;
    }
    if (state->owner_pid != current_task->pid) {
        errno = -EPERM;
        return;
    }

    fb_layer_config_t cfg = {0};
    if (copy_from_user(current_task->address_space, &cfg, arg3, sizeof(cfg)) != 0) {
        errno = -EFAULT;
        return;
    }
    if (!layer_config_valid(&cfg)) {
        errno = -EINVAL;
        return;
    }

    int needs_realloc = (cfg.x0 != state->cfg.x0) || (cfg.y0 != state->cfg.y0) ||
                        (cfg.x1 != state->cfg.x1) || (cfg.y1 != state->cfg.y1) ||
                        (cfg.stride != state->cfg.stride);
    if (needs_realloc) {
        layer_state_t new_state = {0};
        int prep_rc = layer_prepare_state(id, &cfg, &new_state);
        if (prep_rc != 0) {
            errno = prep_rc;
            return;
        }

        vbe_layer_detach((uint8_t)id);
        layer_release_state(id, state, current_task->address_space);
        *state = new_state;
    } else {
        state->cfg = cfg;
    }

    vbe_layer_attach((uint8_t)id, (uint32_t*)state->fb_priv_va, &state->cfg,
                     (fb_layer_metadata_t*)state->meta_priv_va);

    fb_layer_info_t info;
    layer_fill_info(id, &info);
    if (copy_to_user(current_task->address_space, arg4, &info, sizeof(info)) != 0) {
        errno = -EFAULT;
        return;
    }

    errno = 0;
}

static void sys_getuid(void) {
    errno = current_task->uid;
}

static void sys_getgid(void) {
    errno = current_task->gid;
}

static void sys_geteuid(void) {
    errno = current_task->euid;
}

static void sys_getegid(void) {
    errno = current_task->egid;
}

static void sys_setuid(uint32_t arg2) {
    uint16_t uid = (uint16_t)arg2;
    if (current_task->euid == 0) {
        current_task->uid = uid;
        current_task->euid = uid;
        errno = 0;
    } else if (uid == current_task->uid) {
        current_task->euid = uid;
        errno = 0;
    } else {
        errno = -EPERM;
    }
}

static void sys_setgid(uint32_t arg2) {
    uint16_t gid = (uint16_t)arg2;
    if (current_task->euid == 0) {
        current_task->gid = gid;
        current_task->egid = gid;
        errno = 0;
    } else if (gid == current_task->gid) {
        current_task->egid = gid;
        errno = 0;
    } else {
        errno = -EPERM;
    }
}

static void sys_seteuid(uint32_t arg2) {
    uint16_t euid = (uint16_t)arg2;
    if (current_task->euid == 0 || euid == current_task->uid) {
        current_task->euid = euid;
        errno = 0;
    } else {
        errno = -EPERM;
    }
}

static void sys_setegid(uint32_t arg2) {
    uint16_t egid = (uint16_t)arg2;
    if (current_task->euid == 0 || egid == current_task->gid) {
        current_task->egid = egid;
        errno = 0;
    } else {
        errno = -EPERM;
    }
}

static void sys_getgroups(uint32_t arg2, uint32_t arg3) {
    int size = (int)arg2;
    uint16_t *list = (uint16_t *)arg3;

    if (size == 0) {
        errno = current_task->ngroups;
        return;
    }
    if (size < current_task->ngroups) {
        errno = -EINVAL;
        return;
    }
    for (uint8_t i = 0; i < current_task->ngroups; i++) {
        list[i] = current_task->groups[i];
    }
    errno = current_task->ngroups;
}

static void sys_setgroups(uint32_t arg2, uint32_t arg3) {
    int size = (int)arg2;
    uint16_t *list = (uint16_t *)arg3;

    if (current_task->euid != 0) {
        errno = -EPERM;
        return;
    }
    if (size < 0 || size > NGROUPS_MAX) {
        errno = -EINVAL;
        return;
    }
    for (int i = 0; i < size; i++) {
        current_task->groups[i] = list[i];
    }
    current_task->ngroups = (uint8_t)size;
    errno = 0;
}

static void sys_chmod(uint32_t arg2, uint32_t arg3) {
    char *path = (char *)arg2;
    uint32_t new_mode = arg3;
    char abs_path[256];

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    vfs_node_t *node = vfs_resolve_path(abs_path);
    if (!node) {
        errno = -ENOENT;
        return;
    }

    if (current_task->euid != 0 && current_task->euid != node->uid) {
        errno = -EPERM;
        return;
    }

    node->mode = (node->mode & S_IFMT) | (new_mode & ~S_IFMT);
    errno = 0;
}

static void sys_chown(uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    char *path = (char *)arg2;
    uint16_t new_uid = (uint16_t)arg3;
    uint16_t new_gid = (uint16_t)arg4;
    char abs_path[256];

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    vfs_node_t *node = vfs_resolve_path(abs_path);
    if (!node) {
        errno = -ENOENT;
        return;
    }

    if (current_task->euid == 0) {
        if (new_uid != (uint16_t)-1) node->uid = new_uid;
        if (new_gid != (uint16_t)-1) node->gid = new_gid;
        node->mode &= ~(S_ISUID | S_ISGID);
        errno = 0;
    } else if (current_task->euid == node->uid && new_uid == (uint16_t)-1 &&
               new_gid != (uint16_t)-1 && proc_in_group(current_task, new_gid)) {
        node->gid = new_gid;
        node->mode &= ~(S_ISUID | S_ISGID);
        errno = 0;
    } else {
        errno = -EPERM;
    }
}

static void sys_umask(uint32_t arg2) {
    uint32_t old = current_task->umask;
    current_task->umask = arg2 & 0777;
    errno = old;
}

static void sys_uname(uint32_t arg2) {
    struct utsname buf;
    char version[16];
    snprintf(version, sizeof(version), "%d.%d.%d",KERNEL_VERSION_LOW, KERNEL_VERSION_MID, KERNEL_VERSION_HIGH);
    strncpy(buf.sysname, "Serotonin", sizeof(buf.sysname));
    strncpy(buf.nodename, kernel_hostname, sizeof(buf.nodename));
    strncpy(buf.release, (const char *)version, sizeof(buf.release));
    strncpy(buf.version, __DATE__ " " __TIME__, sizeof(buf.version));
    strncpy(buf.machine, "i686", sizeof(buf.machine));

    if (copy_to_user(current_task->address_space, arg2, &buf, sizeof(buf)) != 0) {
        errno = -EFAULT;
        return;
    }
    errno = 0;
}

static void sys_sethostname(uint32_t arg2, uint32_t arg3) {
    const char *name = (const char *)arg2;
    size_t len = (size_t)arg3;

    if (current_task->euid != 0) {
        errno = -EPERM;
        return;
    }
    if (len > HOST_NAME_MAX) {
        errno = -EINVAL;
        return;
    }

    char kbuf[HOST_NAME_MAX + 1];
    if (copy_from_user(current_task->address_space, kbuf, arg2, len) != 0) {
        errno = -EFAULT;
        return;
    }
    kbuf[len] = '\0';
    strncpy(kernel_hostname, kbuf, HOST_NAME_MAX + 1);
    errno = 0;
}

void sys_5ht_set_fid(uint32_t arg2) {
    process_control_block_t *fid_task = task_lookup_by_pid((int)arg2);

    if (fid_task->priv == CPU_KERNEL_MODE) {
        printfs(PRINT_STATUS_WARNING,"Attempted to set illegal foreground task id\n");
        errno = -EFAULT;
        return;
    }

    foreground_pid = (int)arg2;
    errno = 0;
}

/**
 * @brief Handle system calls.
 *
 * @param ctx The processor context.
 */
void system_call(processor_context_t *ctx) {
    preempt_disable();

    memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));

    errno = 0;

    uint32_t operation = ctx->eax;
    uint32_t arg2      = ctx->ebx;
    uint32_t arg3      = ctx->ecx;
    uint32_t arg4      = ctx->edx;

    printfs(PRINT_STATUS_DEBUG, "[SYSCALL] (eip=%p, esp=%p,pid=%d(%s)): op:%d, arg2:%p, arg3:%p, arg4:%p\n",
            ctx->eip, ctx->esp_at_trap, current_task->pid, current_task->name, operation, arg2, arg3, arg4);

    // bypass the irq_disabled counter, the int 0x80 gate cleared IF but irq_disabled was never incremented
    asm volatile("sti");

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
        case SYSTEM_CALL_STAT:
            sys_stat(arg2, arg3);
            break;
        case SYSTEM_CALL_TTY:
            sys_isatty(arg2);
            break;
        case SYSTEM_CALL_TOD:
            sys_gettimeofday(arg2);
            break;
        case SYSTEM_CALL_KILL:
            sys_kill(arg2, arg3);
            break;
        case SYSTEM_CALL_SIGNAL:
            sys_signal(arg2, arg3);
            break;
        case SYSTEM_CALL_SIGRET:
            sys_sigret(ctx);
            break;
        case SYSTEM_CALL_PAUSE:
            sys_pause();
            break;
        case SYSTEM_CALL_SHM_CREATE:
            sys_shm_create(arg2);
            break;
        case SYSTEM_CALL_SHM_MAP:
            sys_shm_map(arg2);
            break;
        case SYSTEM_CALL_SHM_UNMAP:
            sys_shm_unmap(arg2);
            break;
        case SYSTEM_CALL_MKDIR:
            sys_mkdir(arg2);
            break;
        case SYSTEM_CALL_RMDIR:
            sys_rmdir(arg2);
            break;
        case SYSTEM_CALL_CHDIR:
            sys_chdir(arg2);
            break;
        case SYSTEM_CALL_GETCWD:
            sys_getcwd(arg2, arg3);
            break;
        case SYSTEM_CALL_UNLINK:
            sys_unlink(arg2);
            break;
        case SYSTEM_CALL_LISTDIR:
            sys_listdir(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_5HT_LIST_PROC:
            sys_5ht_list_proc(arg2, arg3);
            break;
        case SYSTEM_CALL_5HT_REQ_BUF:
            sys_5ht_req_buf(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_5HT_REL_BUF:
            sys_5ht_rel_buf(arg2);
            break;
        case SYSTEM_CALL_5HT_RCFG_LAYER:
            sys_5ht_rcfg_layer(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_5HT_QUERY_INFO:
            sys_5ht_query_info(arg2);
            break;
        case SYSTEM_CALL_5HT_QUERY_LAYER:
            sys_5ht_query_layer(arg2, arg3);
            break;
        case SYSTEM_CALL_5HT_SET_FID:
            sys_5ht_set_fid(arg2);
            break;
        case SYSTEM_CALL_DUP:
            sys_dup(arg2, arg3);
            break;
        case SYSTEM_CALL_PIPE:
            sys_pipe(arg2);
            break;
        case SYSTEM_CALL_GETUID:
            sys_getuid();
            break;
        case SYSTEM_CALL_GETGID:
            sys_getgid();
            break;
        case SYSTEM_CALL_GETEUID:
            sys_geteuid();
            break;
        case SYSTEM_CALL_GETEGID:
            sys_getegid();
            break;
        case SYSTEM_CALL_SETUID:
            sys_setuid(arg2);
            break;
        case SYSTEM_CALL_SETGID:
            sys_setgid(arg2);
            break;
        case SYSTEM_CALL_SETEUID:
            sys_seteuid(arg2);
            break;
        case SYSTEM_CALL_SETEGID:
            sys_setegid(arg2);
            break;
        case SYSTEM_CALL_GETGROUPS:
            sys_getgroups(arg2, arg3);
            break;
        case SYSTEM_CALL_SETGROUPS:
            sys_setgroups(arg2, arg3);
            break;
        case SYSTEM_CALL_CHMOD:
            sys_chmod(arg2, arg3);
            break;
        case SYSTEM_CALL_CHOWN:
            sys_chown(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_UMASK:
            sys_umask(arg2);
            break;
        case SYSTEM_CALL_UNAME:
            sys_uname(arg2);
            break;
        case SYSTEM_CALL_SETHOSTNAME:
            sys_sethostname(arg2, arg3);
            break;
        default:
            handle_illegal_call(arg2, arg3, arg4, ctx->eip);
            __builtin_unreachable();
    }

    ctx->eax = errno;

    task_ipc_deliver_signals(current_task, ctx);

    printfs(PRINT_STATUS_DEBUG, "[SYSCALL] exiting kernel\n");

    // irqs must be disabled before dropping the preempt guard
    asm volatile("cli");
    preempt_enable();
}
