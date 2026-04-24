#include "syscall.h"
#include "sys/lib5ht.h"
#include <kernel/syscall/syscall.h>
#include <kernel/stdio/stdio.h>
#include <kernel/schedule/schedule.h>
#include <kernel/io/io.h>
#include <kernel/kernel.h>
#include <kernel/stdlib/stdlib.h>
#include <kernel/vmm/paging_init.h>
#include <kernel/vmm/vmm.h>
#include <kernel/string.h>
#include <kernel/filesystem/vfs.h>
#include <kernel/filesystem/vfs_perm.h>
#include <kernel/filesystem/devfs/devfs.h>
#include <kernel/filesystem/user_fs/user_fs.h>
#include <kernel/video/vbe/vbe.h>
#include <kernel/io/serial.h>
#include <kernel/syscall/sys/errno.h>
#include <kernel/syscall/sys/types.h>
#include <kernel/syscall/sys/timespec.h>
#include <kernel/syscall/sys/file.h>
#include <kernel/syscall/sys/lib5ht.h>
#include <kernel/pty/pty.h>
#include <kernel/device/ide/ide_pci.h>
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
static poll_waiter_t *poll_waiters_head = NULL;

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
        int collision = 0;
        for (uint32_t i = 0; i < VBE_NUM_Z_LAYERS; i++) {
            if (!layer_states[i].allocated)
                continue;
            uint32_t start = layer_states[i].fb_priv_va;
            uint32_t stop = start + layer_states[i].priv_region_size;
            if (addr < stop && addr + size > start) {
                addr = align_up(stop, PAGE_SIZE);
                collision = 1;
                break;
            }
        }
        if (!collision)
            return addr;
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

void cleanup_layers(process_control_block_t *task) {
    for (uint16_t i = 1; i < VBE_NUM_Z_LAYERS; i++) {
        layer_state_t *state = &layer_states[i];
        if (state->allocated && state->owner_pid == task->pid) {
            vbe_layer_detach((uint8_t)i);
            layer_release_state(i, state, task->address_space);
        }
    }
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
    if (cfg->alpha > FB_LAYER_ALPHA_BLEND)
        return 0;
    if (cfg->hints & ~FB_LAYER_HINT_ALL_MASK)
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
            vfs_put(child);
            return 1;
        }

        vfs_put(child);
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

        // submit disk based file to worker
        if (handle->node->flags & VFS_FLAG_DISKIO) {
            char *kbuf = (char *)kernel_malloc(buf_size);
            if (!kbuf) {
                errno = -ENOMEM;
                return;
            }
            if (copy_from_user(current_task->address_space, kbuf, (uint32_t)write_ptr, buf_size) != 0) {
                kernel_free(kbuf);
                errno = -EFAULT;
                return;
            }
            ide_submit_disk_write(current_task, handle, current_task->address_space, (uint32_t)write_ptr, buf_size, kbuf, handle->flags);
            __builtin_unreachable();
        }

        if (handle->flags & O_APPEND) {
            handle->offset = handle->node->size;
        }

        char stack_buf[SYSCALL_STACK_BUF];
        char *kbuf = (buf_size <= SYSCALL_STACK_BUF) ? stack_buf : (char*)kernel_malloc(buf_size);
        if (!kbuf) {
            errno = -ENOMEM;
            return;
        }
        if (copy_from_user(current_task->address_space, kbuf, (uint32_t)write_ptr, buf_size) != 0) {
            if (kbuf != stack_buf) kernel_free(kbuf);
            errno = -EFAULT;
            return;
        }

        int written = vfs_write(handle->node, handle->offset, buf_size, kbuf);
        if (kbuf != stack_buf) kernel_free(kbuf);
        if (written < 0) {
            errno = (written == -1) ? -EIO : written;
            return;
        }

        handle->offset += written;
        errno = written;
        return;
    }

    // fd 1 (stdout) or fd 2 (stderr) without file handle, write thru PTY slave
    if (fd == WRITE_STDOUT || fd == WRITE_STDERR) {
        if (buf_size) {
            char stack_buf[SYSCALL_STACK_BUF];
            char *kbuf = (buf_size <= SYSCALL_STACK_BUF) ? stack_buf : (char*)kernel_malloc(buf_size);
            if (!kbuf) {
                errno = -ENOMEM;
                return;
            }
            if (copy_from_user(current_task->address_space, kbuf, (uint32_t)write_ptr, buf_size) != 0) {
                if (kbuf != stack_buf) kernel_free(kbuf);
                errno = -EFAULT;
                return;
            }
            // route thru PTY slave write
            pty_t *pty = &pty_table[active_vty];
            pty_slave_write(pty->slave_node, 0, buf_size, kbuf);
            if (kbuf != stack_buf) kernel_free(kbuf);
        }
        errno = (int)buf_size;
        return;
    }

    errno = -EBADF;
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

        // submit disk based file to worker
        if (handle->node->flags & VFS_FLAG_DISKIO) {
            ide_submit_disk_read(current_task, handle,current_task->address_space, (uint32_t)read_ptr, buf_size, handle->flags);
            __builtin_unreachable();
        }

        char stack_buf[SYSCALL_STACK_BUF];
        char *read_buf = (buf_size <= SYSCALL_STACK_BUF) ? stack_buf : (char*)kernel_malloc(buf_size);
        if (!read_buf) {
            errno = -ENOMEM;
            return;
        }

        current_task->current_fd_flags = handle->flags;
        current_task->current_user_buf = (uint32_t)read_ptr;
        int read_bytes = vfs_read(handle->node, handle->offset, buf_size, read_buf);
        if (read_bytes < 0) {
            if (read_buf != stack_buf) kernel_free(read_buf);
            errno = (read_bytes == -1) ? -EIO : read_bytes;
            return;
        }

        if (copy_to_user(current_task->address_space, (uint32_t)read_ptr, read_buf, (size_t)read_bytes) != 0) {
            if (read_buf != stack_buf) kernel_free(read_buf);
            errno = -EFAULT;
            return;
        }
        handle->offset += read_bytes;
        errno = read_bytes;

        if (read_buf != stack_buf) kernel_free(read_buf);
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

        // route thru PTY slave read
        pty_t *pty = &pty_table[active_vty];
        pty->foreground_pid = (int)current_task->pid;
        current_task->current_user_buf = (uint32_t)read_ptr;

        char *read_buf = kernel_malloc(buf_size);
        if (!read_buf) {
            errno = -ENOMEM;
            return;
        }
        int read_bytes = pty_slave_read(pty->slave_node, 0, buf_size, read_buf);
        if (read_bytes < 0) {
            kernel_free(read_buf);
            errno = read_bytes;
            return;
        }
        if (copy_to_user(current_task->address_space, (uint32_t)read_ptr, read_buf, (size_t)read_bytes) != 0) {
            kernel_free(read_buf);
            errno = -EFAULT;
            return;
        }
        kernel_free(read_buf);
        errno = read_bytes;
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
                vfs_put(parent_node);
                errno = -EACCES;
                return;
            }
            vfs_put(parent_node);
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

static unix_socket_t *sock_alloc(uint8_t type) {
    unix_socket_t *sock = (unix_socket_t *)kernel_malloc(sizeof(*sock));
    if (!sock) return NULL;
    memset(sock, 0, sizeof(*sock));
    sock->type = type;
    sock->state = SOCK_STATE_UNCONNECTED;
    sock->buf_size = SOCK_BUFFER_SIZE_ALLOC;
    sock->buffer = (char *)kernel_malloc(SOCK_BUFFER_SIZE_ALLOC);
    if (!sock->buffer) {
        kernel_free(sock);
        return NULL;
    }
    sock->readers = 1;
    sock->writers = 1;
    return sock;
}

static int sock_make_fd_for(unix_socket_t *sock, uint32_t flags, process_control_block_t *task) {
    sock_endpoint_t *ep = (sock_endpoint_t *)kernel_malloc(sizeof(*ep));
    vfs_node_t *node = (vfs_node_t *)kernel_malloc(sizeof(*node));
    file_handle_t *handle = (file_handle_t *)kernel_malloc(sizeof(*handle));
    if (!ep || !node || !handle) {
        if (ep) kernel_free(ep);
        if (node) kernel_free(node);
        if (handle) kernel_free(handle);
        return -ENOMEM;
    }
    ep->sock = sock;
    memset(node, 0, sizeof(*node));
    node->flags = VFS_FLAG_FILE | VFS_FLAG_SOCKET;
    node->ops = &task_ipc_unix_socket_ops;
    node->fs_data = ep;
    node->uid = task->euid;
    node->gid = task->egid;
    node->mode = S_IFSOCK | 0600;

    memset(handle, 0, sizeof(*handle));
    handle->node = node;
    handle->flags = flags;
    handle->refcount = 0;

    int fd = alloc_fd(task, handle);
    if (fd < 0) {
        kernel_free(handle);
        kernel_free(node);
        kernel_free(ep);
        return -EMFILE;
    }
    return fd;
}

static int sock_make_fd(unix_socket_t *sock, uint32_t flags) {
    return sock_make_fd_for(sock, flags, current_task);
}

static void sys_socket(uint32_t domain, uint32_t type) {
    if (domain != AF_UNIX) {
        errno = -EAFNOSUPPORT;
        return;
    }
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        errno = -EPROTOTYPE;
        return;
    }

    unix_socket_t *sock = sock_alloc(type);
    if (!sock) {
        errno = -ENOMEM;
        return;
    }

    int fd = sock_make_fd(sock, O_RDWR);
    if (fd < 0) {
        kernel_free(sock->buffer);
        kernel_free(sock);
        errno = fd;
        return;
    }
    errno = fd;
}

static unix_socket_t *sock_from_fd(int fd) {
    if (fd < 0 || fd >= FD_MAX || !current_task->fd_table[fd])
        return NULL;
    file_handle_t *h = current_task->fd_table[fd];
    if (!h->node || !(h->node->flags & VFS_FLAG_SOCKET))
        return NULL;
    sock_endpoint_t *ep = (sock_endpoint_t *)h->node->fs_data;
    return ep ? ep->sock : NULL;
}

static void sys_bind(uint32_t fd_arg, uint32_t addr_arg, uint32_t len) {
    int fd = (int)fd_arg;
    unix_socket_t *sock = sock_from_fd(fd);
    if (!sock) {
        errno = -ENOTSOCK;
        return;
    }
    if (sock->bound) {
        errno = -EINVAL;
        return;
    }

    struct sockaddr_un addr;
    if (len > sizeof(addr)) len = sizeof(addr);
    if (copy_from_user(current_task->address_space, &addr, addr_arg, len) != 0) {
        errno = -EFAULT;
        return;
    }
    if (addr.sun_family != AF_UNIX) {
        errno = -EAFNOSUPPORT;
        return;
    }

    addr.sun_path[UNIX_PATH_MAX - 1] = '\0';

    if (unix_socket_lookup(addr.sun_path)) {
        errno = -EADDRINUSE;
        return;
    }

    strncpy(sock->path, addr.sun_path, UNIX_PATH_MAX);
    sock->bound = 1;
    sock->state = SOCK_STATE_BOUND;

    if (unix_socket_register(sock) != 0) {
        sock->bound = 0;
        sock->state = SOCK_STATE_UNCONNECTED;
        errno = -ENOSPC;
        return;
    }
    errno = 0;
}

static void sys_listen(uint32_t fd_arg, uint32_t backlog) {
    int fd = (int)fd_arg;
    unix_socket_t *sock = sock_from_fd(fd);
    if (!sock) {
        errno = -ENOTSOCK;
        return;
    }
    if (sock->type != SOCK_STREAM) {
        errno = -EOPNOTSUPP;
        return;
    }
    if (!sock->bound) {
        errno = -EINVAL;
        return;
    }

    sock->backlog_max = backlog < SOCK_BACKLOG_MAX ? backlog : SOCK_BACKLOG_MAX;
    if (sock->backlog_max == 0) sock->backlog_max = 1;
    sock->state = SOCK_STATE_LISTENING;
    errno = 0;
}

static void sock_accept_deliver(unix_socket_t *listener, process_control_block_t *task) {
    unix_socket_t *client_sock = listener->backlog[0];
    for (uint32_t i = 1; i < listener->backlog_count; i++)
        listener->backlog[i - 1] = listener->backlog[i];
    listener->backlog_count--;

    unix_socket_t *server_sock = sock_alloc(SOCK_STREAM);
    if (!server_sock) {
        sock_wake_one(&listener->connect_waiters_head, &listener->connect_waiters_tail);
        unlock_scheduler();
        task->processor_context->eax = (uint32_t)(-ENOMEM);
        return;
    }
    server_sock->state = SOCK_STATE_CONNECTED;
    server_sock->peer = client_sock;
    client_sock->peer = server_sock;
    client_sock->state = SOCK_STATE_CONNECTED;

    sock_wake_one(&listener->connect_waiters_head, &listener->connect_waiters_tail);
    unlock_scheduler();

    int new_fd = sock_make_fd_for(server_sock, O_RDWR, task);
    if (new_fd < 0) {
        server_sock->state = SOCK_STATE_CLOSED;
        client_sock->peer = NULL;
        client_sock->state = SOCK_STATE_UNCONNECTED;
        kernel_free(server_sock->buffer);
        kernel_free(server_sock);
        task->processor_context->eax = (uint32_t)new_fd;
        return;
    }

    task->processor_context->eax = (uint32_t)new_fd;
}

static void sys_accept(uint32_t fd_arg, uint32_t addr_arg, uint32_t len_arg) {
    (void)addr_arg; (void)len_arg;
    int fd = (int)fd_arg;
    unix_socket_t *listener = sock_from_fd(fd);
    if (!listener) {
        errno = -ENOTSOCK;
        return;
    }
    if (listener->state != SOCK_STATE_LISTENING) {
        errno = -EINVAL;
        return;
    }

    lock_scheduler();
    if (listener->backlog_count == 0) {
        sock_waiter_enqueue(&listener->accept_waiters_head, &listener->accept_waiters_tail, current_task, 0, 0);
        current_task->state = PROCESS_STATE_BLOCKED;
        unlock_scheduler();
        task_yield(1);
        __builtin_unreachable();
    }

    sock_accept_deliver(listener, current_task);
    errno = (int)current_task->processor_context->eax;
}

static void sys_connect(uint32_t fd_arg, uint32_t addr_arg, uint32_t len) {
    int fd = (int)fd_arg;
    unix_socket_t *sock = sock_from_fd(fd);
    if (!sock) {
        errno = -ENOTSOCK;
        return;
    }

    if (sock->state == SOCK_STATE_CONNECTED) {
        errno = 0;
        return;
    }

    struct sockaddr_un addr;
    if (len > sizeof(addr)) len = sizeof(addr);
    if (copy_from_user(current_task->address_space, &addr, addr_arg, len) != 0) {
        errno = -EFAULT;
        return;
    }
    if (addr.sun_family != AF_UNIX) {
        errno = -EAFNOSUPPORT;
        return;
    }
    addr.sun_path[UNIX_PATH_MAX - 1] = '\0';

    unix_socket_t *listener = unix_socket_lookup(addr.sun_path);
    if (!listener) {
        errno = -ECONNREFUSED;
        return;
    }

    if (sock->type == SOCK_STREAM) {
        if (listener->state != SOCK_STATE_LISTENING) {
            errno = -ECONNREFUSED;
            return;
        }

        lock_scheduler();

        if (listener->backlog_count >= listener->backlog_max) {
            unlock_scheduler();
            errno = -ECONNREFUSED;
            return;
        }

        listener->backlog[listener->backlog_count++] = sock;
        sock->state = SOCK_STATE_CONNECTING;

        if (listener->accept_waiters_head) {
            sock_waiter_t *w = sock_waiter_dequeue(&listener->accept_waiters_head, &listener->accept_waiters_tail);
            process_control_block_t *accepter = w->task;
            kernel_free(w);
            sock_accept_deliver(listener, accepter);
            task_unblock(accepter);
            errno = 0;
            return;
        }

        sock_waiter_enqueue(&listener->connect_waiters_head, &listener->connect_waiters_tail, current_task, 0, 0);
        current_task->state = PROCESS_STATE_BLOCKED;
        unlock_scheduler();
        task_yield(1);
        __builtin_unreachable();
    } else {
        sock->peer = listener;
        sock->state = SOCK_STATE_CONNECTED;
        errno = 0;
    }
}

static void sys_send(uint32_t fd_arg, uint32_t buf_arg, uint32_t len) {
    int fd = (int)fd_arg;
    if (fd < 0 || fd >= FD_MAX || !current_task->fd_table[fd]) {
        errno = -EBADF;
        return;
    }

    if (buf_arg < USER_SPACE_START || buf_arg + len - 1 > USER_SPACE_END) {
        errno = -EFAULT;
        return;
    }

    char *kbuf = (char *)kernel_malloc(len);
    if (!kbuf) {
        errno = -ENOMEM;
        return;
    }
    if (copy_from_user(current_task->address_space, kbuf, buf_arg, len) != 0) {
        kernel_free(kbuf);
        errno = -EFAULT;
        return;
    }
    errno = task_ipc_unix_socket_write(current_task->fd_table[fd]->node, 0, len, kbuf);
    kernel_free(kbuf);
}

static void sys_recv(uint32_t fd_arg, uint32_t buf_arg, uint32_t len) {
    int fd = (int)fd_arg;
    if (fd < 0 || fd >= FD_MAX || !current_task->fd_table[fd]) {
        errno = -EBADF;
        return;
    }

    if (buf_arg < USER_SPACE_START || buf_arg + len - 1 > USER_SPACE_END) {
        errno = -EFAULT;
        return;
    }
    current_task->current_user_buf = buf_arg;

    char *kbuf = (char *)kernel_malloc(len);
    if (!kbuf) {
        errno = -ENOMEM;
        return;
    }

    int n = task_ipc_unix_socket_read(current_task->fd_table[fd]->node, 0, len, kbuf);
    if (n > 0) {
        if (copy_to_user(current_task->address_space, buf_arg, kbuf, (size_t)n) != 0) {
            kernel_free(kbuf);
            errno = -EFAULT;
            return;
        }
    }
    kernel_free(kbuf);
    errno = n;
}

static void sys_shutdown(uint32_t fd_arg, uint32_t how) {
    (void)how;
    int fd = (int)fd_arg;
    unix_socket_t *sock = sock_from_fd(fd);
    if (!sock) {
        errno = -ENOTSOCK;
        return;
    }

    lock_scheduler();
    sock->state = SOCK_STATE_CLOSED;
    sock_wake_all(&sock->read_waiters_head, &sock->read_waiters_tail);
    sock_wake_all(&sock->write_waiters_head, &sock->write_waiters_tail);
    if (sock->peer) {
        sock_wake_all(&sock->peer->read_waiters_head, &sock->peer->read_waiters_tail);
        sock_wake_all(&sock->peer->write_waiters_head, &sock->peer->write_waiters_tail);
    }
    unlock_scheduler();
    errno = 0;
}

static void sys_socketpair(uint32_t type, uint32_t sv_addr) {
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        errno = -EPROTOTYPE;
        return;
    }
    if (sv_addr < USER_SPACE_START || sv_addr + sizeof(int) * 2 - 1 > USER_SPACE_END) {
        errno = -EFAULT;
        return;
    }

    unix_socket_t *s0 = sock_alloc(type);
    unix_socket_t *s1 = sock_alloc(type);
    if (!s0 || !s1) {
        if (s0) { kernel_free(s0->buffer); kernel_free(s0); }
        if (s1) { kernel_free(s1->buffer); kernel_free(s1); }
        errno = -ENOMEM;
        return;
    }

    s0->peer = s1;
    s1->peer = s0;
    s0->state = SOCK_STATE_CONNECTED;
    s1->state = SOCK_STATE_CONNECTED;

    int fd0 = sock_make_fd(s0, O_RDWR);
    if (fd0 < 0) {
        kernel_free(s0->buffer); kernel_free(s0);
        kernel_free(s1->buffer); kernel_free(s1);
        errno = fd0;
        return;
    }
    int fd1 = sock_make_fd(s1, O_RDWR);
    if (fd1 < 0) {
        close_fd(current_task, fd0);
        kernel_free(s1->buffer); kernel_free(s1);
        errno = fd1;
        return;
    }

    int sv[2] = { fd0, fd1 };
    if (copy_to_user(current_task->address_space, sv_addr, sv, sizeof(sv)) != 0) {
        close_fd(current_task, fd0);
        close_fd(current_task, fd1);
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

static void free_string_array(const char **arr, int count) {
    for (int i = 0; i < count; i++)
        kernel_free((void*)arr[i]);
    kernel_free(arr);
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
        vfs_put(node);
        kernel_free(path);
        errno = -EACCES;
        return;
    }

    uint32_t exec_mode = node->mode;
    uint16_t exec_uid = node->uid;
    uint16_t exec_gid = node->gid;
    char pname[256];
    strncpy(pname, node->name, sizeof(pname));
    pname[sizeof(pname) - 1] = '\0';
    vfs_put(node);

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

    int execve_stat = kernel_load_elf(current_task, path, pname, argv, argc, envp, envc);
    if (!execve_stat) {
        destroy_address_space(oldas);
        if (exec_mode & S_ISUID) current_task->euid = exec_uid;
        if (exec_mode & S_ISGID) current_task->egid = exec_gid;
        /* reset caught signal handlers — old addresses are stale in the new image */
        for (int i = 0; i < 16; i++)
            current_task->signal_handlers[i] = 0;
        current_task->signal_bitmask = 0;
        current_task->in_signal_handler = 0;
        printfs(PRINT_STATUS_DEBUG, "execve: executing %s, pid=%d\n", path, current_task->pid);
        free_string_array(argv, argc);
        free_string_array(envp, envc);
        kernel_free(path);
        task_yield(0);
    } else {
        printfs(PRINT_STATUS_WARNING, "execve: failed to load elf %s, pid=%d\n", path, current_task->pid);
        free_string_array(argv, argc);
        free_string_array(envp, envc);
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
            unmap_page(current_task->address_space, va, 1);
        }
    }

    current_task->brk_end = new_brk;
    errno = old_brk;
}

static void sys_waitpid(uint32_t arg2, uint32_t arg3, processor_context_t *ctx) {
    lock_scheduler();
    int pid = (int)arg2;
    int* status_ptr = (int*)arg3;

    if (pid == -1) {
        current_task->waiting_on = pid;
        current_task->status_ptr = status_ptr;
        task_block();
        return;
    }

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
    /* fd 0/1/2 without file handle = connected to PTY = is a tty */
    if (fd < 3 && current_task->fd_table[fd] == NULL) {
        errno = 1;
        return;
    }
    /* Check if fd's node is a PTY (direct or via devfs) */
    if (fd < FD_MAX && current_task->fd_table[fd] != NULL) {
        file_handle_t *handle = current_task->fd_table[fd];
        if (handle->node && pty_from_node(handle->node)) {
            errno = 1;
            return;
        }
    }
    errno = 0;
}

static void sys_gettimeofday(uint32_t arg2) {
    struct timeval *timestr = (struct timeval*)arg2;
    uint32_t ms = (uint32_t)timer_ticks; /* low 32 bits — wraps ~49 days, fine for diffs */
    timestr->tv_sec  = (long)(ms / 1000u);
    timestr->tv_usec = (long)((ms % 1000u) * 1000u);
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

static void sys_alarm(uint32_t seconds) {
    lock_scheduler();

    uint32_t old = current_task->alarm_ticks;
    /* convert remaining ticks back to seconds (round up) for return value */
    uint32_t old_seconds = old ? (old + 999) / 1000 : 0;

    if (seconds == 0)
        current_task->alarm_ticks = 0;   /* cancel pending alarm */
    else
        current_task->alarm_ticks = seconds * 1000;  /* PIT is 1000 Hz */

    unlock_scheduler();

    errno = (int)old_seconds;
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
    uint32_t vaddr = arg2;
    if (vaddr < SHMEM_START || vaddr >= SHMEM_END) {
        errno = -EINVAL;
        return;
    }

    shm_unmap(current_task->address_space, vaddr);
    errno = 0;
}

static void sys_mkdir(uint32_t arg2) {
    char *path = (char*)arg2;
    char abs_path[256];

    if (build_abs_path(path, abs_path, sizeof(abs_path)) != 0) {
        errno = -ENAMETOOLONG;
        return;
    }

    vfs_node_t *existing = vfs_resolve_path(abs_path);
    if (existing) {
        vfs_put(existing);
        errno = -EEXIST;
        return;
    }

    char parent_path[256], child_name[256];
    split_path(abs_path, parent_path, child_name);
    vfs_node_t *parent_node = vfs_resolve_path(parent_path);
    if (parent_node && vfs_check_dir_write(parent_node, current_task) != 0) {
        vfs_put(parent_node);
        errno = -EACCES;
        return;
    }
    vfs_put(parent_node);

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
        vfs_put(node);
        errno = -EISDIR;
        return;
    }

    char parent_path[256], child_name[256];
    split_path(abs_path, parent_path, child_name);
    vfs_node_t *parent_node = vfs_resolve_path(parent_path);
    if (parent_node) {
        if (vfs_check_dir_write(parent_node, current_task) != 0) {
            vfs_put(node);
            vfs_put(parent_node);
            errno = -EACCES;
            return;
        }
        if ((parent_node->mode & S_ISVTX) && current_task->euid != 0 &&
            current_task->euid != node->uid && current_task->euid != parent_node->uid) {
            vfs_put(node);
            vfs_put(parent_node);
            errno = -EACCES;
            return;
        }
    }
    vfs_put(node);
    vfs_put(parent_node);

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
        vfs_put(node);
        errno = -ENOTDIR;
        return;
    }

    if (!node->ops || !node->ops->rmdir) {
        vfs_put(node);
        errno = -ENOSYS;
        return;
    }

    char parent_path[256], child_name[256];
    split_path(abs_path, parent_path, child_name);
    vfs_node_t *parent_node = vfs_resolve_path(parent_path);
    if (parent_node) {
        if (vfs_check_dir_write(parent_node, current_task) != 0) {
            vfs_put(node);
            vfs_put(parent_node);
            errno = -EACCES;
            return;
        }
        if ((parent_node->mode & S_ISVTX) && current_task->euid != 0 &&
            current_task->euid != node->uid && current_task->euid != parent_node->uid) {
            vfs_put(node);
            vfs_put(parent_node);
            errno = -EACCES;
            return;
        }
    }
    vfs_put(parent_node);

    if (dir_has_entries(node)) {
        vfs_put(node);
        errno = -ENOTEMPTY;
        return;
    }
    vfs_put(node);

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
        vfs_put(node);
        errno = -ENOTDIR;
        return;
    }

    if (vfs_check_permission(node, current_task, PERM_EXEC) != 0) {
        vfs_put(node);
        errno = -EACCES;
        return;
    }
    vfs_put(node);

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
        vfs_put(node);
        errno = -ENOTDIR;
        return;
    }
    if (!node->ops || !node->ops->readdir) {
        vfs_put(node);
        errno = -ENOSYS;
        return;
    }

    size_t off = 0;
    for (uint32_t i = 0; ; i++) {
        vfs_node_t *child = node->ops->readdir(node, i);
        if (!child) break;

        size_t len = strlen(child->name);
        int done = (off + len + 1 >= size);
        int fail = 0;

        if (!done) {
            if (copy_to_user(current_task->address_space, (uint32_t)(buf + off), child->name, len) != 0) {
                fail = 1;
            } else {
                off += len;
                if (copy_to_user(current_task->address_space, (uint32_t)(buf + off), "\n", 1) != 0)
                    fail = 1;
                else
                    off += 1;
            }
        }

        vfs_put(child);

        if (done) { vfs_put(node); errno = off; return; }
        if (fail) { vfs_put(node); errno = -EFAULT; return; }
    }

    if (off < size) {
        if (copy_to_user(current_task->address_space, (uint32_t)(buf + off), "\0", 1) != 0) {
            vfs_put(node);
            errno = -EFAULT;
            return;
        }
    }
    vfs_put(node);
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
        k_buf.cpu_user_ticks   = task->cpu_user_ticks;
        k_buf.cpu_kernel_ticks = task->cpu_kernel_ticks;
        k_buf.disk_bytes       = task->disk_bytes;
        k_buf.uid              = task->uid;
        k_buf.gid              = task->gid;
        if (task->priv == CPU_USER_MODE) {
            k_buf.mem_bytes = (task->brk_end >= task->brk_start)
                              ? (task->brk_end - task->brk_start)
                              : 0;
        } else {
            k_buf.mem_bytes = KERNEL_STACK_SIZE;
        }

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

    /* Only reallocate if the layer dimensions (buffer size) changed,
       not when just the position changed */
    uint16_t old_w = state->cfg.x1 - state->cfg.x0;
    uint16_t old_h = state->cfg.y1 - state->cfg.y0;
    uint16_t new_w = cfg.x1 - cfg.x0;
    uint16_t new_h = cfg.y1 - cfg.y0;
    int needs_realloc = (new_w != old_w) || (new_h != old_h) ||
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

/* Window layer z indices reserved for WM + GUI clients (see user/wm/wm.h). */
#define WM_APP_LAYER_FIRST 2u
#define WM_APP_LAYER_LAST  12u

static int current_task_is_wm(void) {
    return current_task && strcmp(current_task->name, "wm") == 0;
}

/**
 * @brief Exchange two compositor layer slots (full `layer_state_t` swap).
 *
 * Only the window manager may call this: it reorders cross-process surfaces
 * while keeping each task's SHM mappings valid.
 */
static void sys_5ht_swap_layers(uint32_t arg2, uint32_t arg3) {
    uint16_t za = (uint16_t)arg2;
    uint16_t zb = (uint16_t)arg3;

    if (!current_task_is_wm()) {
        errno = -EPERM;
        return;
    }
    if (za < WM_APP_LAYER_FIRST || za > WM_APP_LAYER_LAST ||
        zb < WM_APP_LAYER_FIRST || zb > WM_APP_LAYER_LAST || za == zb) {
        errno = -EINVAL;
        return;
    }

    layer_state_t *sa = &layer_states[za];
    layer_state_t *sb = &layer_states[zb];
    if (!sa->allocated || !sb->allocated) {
        errno = -ENOENT;
        return;
    }

    vbe_layer_detach((uint8_t)za);
    vbe_layer_detach((uint8_t)zb);

    layer_state_t tmp = *sa;
    *sa = *sb;
    *sb = tmp;

    vbe_layer_attach((uint8_t)za, (uint32_t *)sa->fb_priv_va, &sa->cfg,
                     (fb_layer_metadata_t *)sa->meta_priv_va);
    vbe_layer_attach((uint8_t)zb, (uint32_t *)sb->fb_priv_va, &sb->cfg,
                     (fb_layer_metadata_t *)sb->meta_priv_va);
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
        vfs_put(node);
        errno = -EPERM;
        return;
    }

    node->mode = (node->mode & S_IFMT) | (new_mode & ~S_IFMT);
    vfs_put(node);
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
    vfs_put(node);
}

static void sys_umask(uint32_t arg2) {
    uint32_t old = current_task->umask;
    current_task->umask = arg2 & 0777;
    errno = old;
}

static void sys_uname(uint32_t arg2) {
    struct utsname buf;
    char version[16];
    snprintf(version, sizeof(version), "%d.%d.%d",KERNEL_VERSION_HIGH, KERNEL_VERSION_MID, KERNEL_VERSION_LOW);
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

static void sys_5ht_pty_open(uint32_t arg2) {
    uint32_t out_ptr = arg2;
    if (out_ptr < USER_SPACE_START || out_ptr + sizeof(int) * 2 - 1 > USER_SPACE_END) {
        errno = -EFAULT;
        return;
    }

    pty_t *pty = pty_alloc();
    if (!pty) {
        errno = -ENOMEM;
        return;
    }

    file_handle_t *master_handle = (file_handle_t *)kernel_malloc(sizeof(file_handle_t));
    file_handle_t *slave_handle  = (file_handle_t *)kernel_malloc(sizeof(file_handle_t));
    if (!master_handle || !slave_handle) {
        if (master_handle) kernel_free(master_handle);
        if (slave_handle)  kernel_free(slave_handle);
        pty_free(pty);
        errno = -ENOMEM;
        return;
    }

    memset(master_handle, 0, sizeof(*master_handle));
    master_handle->node  = pty->master_node;
    master_handle->flags = O_RDWR;

    memset(slave_handle, 0, sizeof(*slave_handle));
    slave_handle->node  = pty->slave_node;
    slave_handle->flags = O_RDWR;

    int master_fd = alloc_fd(current_task, master_handle);
    if (master_fd < 0) {
        kernel_free(master_handle);
        kernel_free(slave_handle);
        pty_free(pty);
        errno = -EMFILE;
        return;
    }

    int slave_fd = alloc_fd(current_task, slave_handle);
    if (slave_fd < 0) {
        close_fd(current_task, master_fd);
        kernel_free(slave_handle);
        pty_free(pty);
        errno = -EMFILE;
        return;
    }

    int fds[2] = { master_fd, slave_fd };
    if (copy_to_user(current_task->address_space, out_ptr, (char *)fds, sizeof(fds)) != 0) {
        close_fd(current_task, master_fd);
        close_fd(current_task, slave_fd);
        errno = -EFAULT;
        return;
    }

    errno = 0;
}

static void sys_5ht_pty_setattr(uint32_t arg2, uint32_t arg3) {
    uint32_t fd = arg2;
    uint32_t attr_ptr = arg3;

    pty_t *pty = NULL;
    if (fd < 3 && current_task->fd_table[fd] == NULL) {
        pty = &pty_table[active_vty];
    } else if (fd < FD_MAX && current_task->fd_table[fd]) {
        vfs_node_t *node = current_task->fd_table[fd]->node;
        if (node) pty = pty_from_node(node);
    }

    if (!pty) { errno = -ENOTTY; return; }

    pty_attr_t attr;
    if (copy_from_user(current_task->address_space, (char *)&attr, attr_ptr, sizeof(attr)) != 0) {
        errno = -EFAULT;
        return;
    }
    pty->attr = attr;
    errno = 0;
}

static void sys_5ht_pty_getattr(uint32_t arg2, uint32_t arg3) {
    uint32_t fd = arg2;
    uint32_t attr_ptr = arg3;

    pty_t *pty = NULL;
    if (fd < 3 && current_task->fd_table[fd] == NULL) {
        pty = &pty_table[active_vty];
    } else if (fd < FD_MAX && current_task->fd_table[fd]) {
        vfs_node_t *node = current_task->fd_table[fd]->node;
        if (node) pty = pty_from_node(node);
    }

    if (!pty) { errno = -ENOTTY; return; }

    if (copy_to_user(current_task->address_space, attr_ptr, (char *)&pty->attr, sizeof(pty->attr)) != 0) {
        errno = -EFAULT;
        return;
    }
    errno = 0;
}

static void sys_5ht_pty_winsize(uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    uint32_t fd = arg2;
    uint32_t ws_ptr = arg3;
    uint32_t get_flag = arg4; // 1 = get, 0 = set

    pty_t *pty = NULL;
    if (fd < 3 && current_task->fd_table[fd] == NULL) {
        pty = &pty_table[active_vty];
    } else if (fd < FD_MAX && current_task->fd_table[fd]) {
        vfs_node_t *node = current_task->fd_table[fd]->node;
        if (node) pty = pty_from_node(node);
    }

    if (!pty) { errno = -ENOTTY; return; }

    if (get_flag) {
        if (copy_to_user(current_task->address_space, ws_ptr, (char *)&pty->winsize, sizeof(pty->winsize)) != 0) {
            errno = -EFAULT;
            return;
        }
    } else {
        if (copy_from_user(current_task->address_space, (char *)&pty->winsize, ws_ptr, sizeof(pty->winsize)) != 0) {
            errno = -EFAULT;
            return;
        }
    }
    errno = 0;
}

static void sys_5ht_pty_setpgrp(uint32_t arg2) {
    uint32_t fd = arg2;

    pty_t *pty = NULL;
    if (fd < 3 && current_task->fd_table[fd] == NULL) {
        pty = &pty_table[active_vty];
    } else if (fd < FD_MAX && current_task->fd_table[fd]) {
        vfs_node_t *node = current_task->fd_table[fd]->node;
        if (node) pty = pty_from_node(node);
    }

    if (!pty) { errno = -ENOTTY; return; }

    pty->foreground_pid = (int)current_task->pid;
    errno = 0;
}

static int fd_poll_check_task(process_control_block_t *task, int fd) {
    int revents = 0;

    if (fd == 0 && task->fd_table[0] == NULL) {
        pty_t *pty = &pty_table[active_vty];
        if (!pty->attr.icanon) {
            if (pty->input_ring.data_len > 0)
                revents |= POLLIN;
        } else {
            for (uint32_t i = 0; i < pty->input_ring.data_len; i++) {
                uint32_t pos = (pty->input_ring.read_pos + i) % PTY_RING_SIZE;
                if (pty->input_ring.buf[pos] == '\n') {
                    revents |= POLLIN;
                    break;
                }
            }
        }
        revents |= POLLOUT;
        return revents;
    }

    if ((fd == 1 || fd == 2) && task->fd_table[fd] == NULL) {
        revents |= POLLOUT;
        return revents;
    }

    if (fd < 0 || fd >= FD_MAX || task->fd_table[fd] == NULL)
        return POLLNVAL;

    file_handle_t *handle = task->fd_table[fd];
    vfs_node_t *node = handle->node;
    if (!node)
        return POLLNVAL;

    if (node->flags & VFS_FLAG_PIPE) {
        pipe_endpoint_t *ep = (pipe_endpoint_t*)node->fs_data;
        if (ep && ep->pipe) {
            pipe_state_t *p = ep->pipe;
            if (ep->is_read_end) {
                if (p->data_len > 0)   revents |= POLLIN;
                if (p->writers == 0)   revents |= POLLHUP;
            } else {
                if (p->data_len < p->size) revents |= POLLOUT;
                if (p->readers == 0)       revents |= POLLERR;
            }
        }
        return revents;
    }

    if (node->flags & VFS_FLAG_SOCKET) {
        sock_endpoint_t *sep = (sock_endpoint_t*)node->fs_data;
        if (sep && sep->sock) {
            unix_socket_t *s = sep->sock;

            if (s->state == SOCK_STATE_LISTENING) {
                if (s->backlog_count > 0) revents |= POLLIN;
                return revents;
            }

            if (s->data_len > 0)
                revents |= POLLIN;

            if (s->type == SOCK_DGRAM) {
                revents |= POLLOUT;
            } else if (s->peer) {
                if (s->peer->data_len < s->peer->buf_size)
                    revents |= POLLOUT;
            }

            if (s->state == SOCK_STATE_CLOSED ||
                (s->type == SOCK_STREAM && !s->peer))
                revents |= POLLHUP;
        }
        return revents;
    }

    pty_t *pty = pty_from_node(node);
    if (pty) {
        int is_master = (node == pty->master_node) ||
                        (node->ops != NULL && node->ops == pty->master_node->ops &&
                         node->fs_data == pty->master_node->fs_data);
        if (is_master) {
            if (pty->output_ring.data_len > 0) revents |= POLLIN;
            if (pty->slave_refcount == 0) revents |= POLLHUP;
            revents |= POLLOUT;
        } else {
            if (pty->input_ring.data_len > 0) revents |= POLLIN;
            revents |= POLLOUT;
        }
        return revents;
    }

    if ((node->flags & VFS_FLAG_FILE) && node->fs_data) {
        devfs_file_t *devfile = (devfs_file_t *)node->fs_data;
        if (devfile->ops && devfile->ops->poll) {
            revents |= devfile->ops->poll(node);
            return revents;
        }
    }

    revents |= POLLIN | POLLOUT;
    return revents;
}

static int fd_poll_check(int fd) {
    return fd_poll_check_task(current_task, fd);
}

static void poll_waiter_add(poll_waiter_t *w) {
    w->next = poll_waiters_head;
    poll_waiters_head = w;
}

static void poll_waiter_remove(poll_waiter_t *w) {
    poll_waiter_t **pp = &poll_waiters_head;
    while (*pp) {
        if (*pp == w) { *pp = w->next; return; }
        pp = &(*pp)->next;
    }
}

static int poll_waiter_try_select(poll_waiter_t *w) {
    kernel_fd_set res_r, res_w, res_e;
    K_FD_ZERO(&res_r);
    K_FD_ZERO(&res_w);
    K_FD_ZERO(&res_e);

    int ready = 0;
    for (int fd = 0; fd < w->nfds; fd++) {
        int want_r = w->readfds_ptr   && K_FD_ISSET(fd, &w->readfds);
        int want_w = w->writefds_ptr  && K_FD_ISSET(fd, &w->writefds);
        int want_e = w->exceptfds_ptr && K_FD_ISSET(fd, &w->exceptfds);
        if (!want_r && !want_w && !want_e)
            continue;

        int events = fd_poll_check_task(w->task, fd);
        if (want_r && (events & (POLLIN | POLLHUP | POLLERR))) {
            K_FD_SET(fd, &res_r); ready++;
        }
        if (want_w && (events & POLLOUT)) {
            K_FD_SET(fd, &res_w); ready++;
        }
        if (want_e && (events & POLLERR)) {
            K_FD_SET(fd, &res_e); ready++;
        }
    }

    if (ready > 0 || (w->has_timeout && timer_ticks >= w->deadline)) {
        if (w->readfds_ptr)
            copy_to_user(w->task->address_space, w->readfds_ptr, &res_r, sizeof(kernel_fd_set));
        if (w->writefds_ptr)
            copy_to_user(w->task->address_space, w->writefds_ptr, &res_w, sizeof(kernel_fd_set));
        if (w->exceptfds_ptr)
            copy_to_user(w->task->address_space, w->exceptfds_ptr, &res_e, sizeof(kernel_fd_set));
        return ready;
    }
    return -1;
}

static int poll_waiter_try_poll(poll_waiter_t *w) {
    int ready = 0;
    for (uint32_t i = 0; i < w->poll_nfds; i++) {
        w->pfds[i].revents = 0;
        if (w->pfds[i].fd < 0) continue;

        int events = fd_poll_check_task(w->task, w->pfds[i].fd);
        int16_t rev = 0;
        if (events & POLLNVAL) { rev = POLLNVAL; }
        else {
            if ((w->pfds[i].events & POLLIN)  && (events & POLLIN))  rev |= POLLIN;
            if ((w->pfds[i].events & POLLOUT) && (events & POLLOUT)) rev |= POLLOUT;
            if (events & POLLERR) rev |= POLLERR;
            if (events & POLLHUP) rev |= POLLHUP;
        }
        w->pfds[i].revents = rev;
        if (rev) ready++;
    }

    if (ready > 0 || (w->has_timeout && timer_ticks >= w->deadline)) {
        uint32_t pfd_size = w->poll_nfds * sizeof(struct kernel_pollfd);
        copy_to_user(w->task->address_space, w->poll_fds_ptr, w->pfds, pfd_size);
        return ready;
    }
    return -1;
}

static void sys_usleep(uint32_t us) {
    if (us == 0) {
        errno = 0;
        return;
    }

    uint64_t timeout_ms = ((uint64_t)us + 999) / 1000;
    uint64_t deadline   = timer_ticks + timeout_ms;

    poll_waiter_t *w = kernel_malloc(sizeof(poll_waiter_t));
    if (!w) { errno = -ENOMEM; return; }

    memset(w, 0, sizeof(*w));
    w->task        = current_task;
    w->type        = POLL_WAITER_SELECT;
    w->has_timeout = 1;
    w->deadline    = deadline;

    lock_scheduler();
    poll_waiter_add(w);
    current_task->state = PROCESS_STATE_BLOCKED;
    unlock_scheduler();
    task_yield(1);
    __builtin_unreachable();
}

void poll_waiter_tick(void) {
    poll_waiter_t *w = poll_waiters_head;
    while (w) {
        poll_waiter_t *next = w->next;

        int rc;
        if (w->type == POLL_WAITER_SELECT)
            rc = poll_waiter_try_select(w);
        else
            rc = poll_waiter_try_poll(w);

        if (rc >= 0) {
            w->task->processor_context->eax = (uint32_t)rc;
            poll_waiter_remove(w);
            if (w->type == POLL_WAITER_POLL && w->pfds)
                kernel_free(w->pfds);
            task_unblock(w->task);
            kernel_free(w);
        }
        w = next;
    }
}

static void sys_select(uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    int nfds = (int)arg2;
    uint32_t args_ptr = arg3;

    if (nfds < 0 || nfds > FD_SETSIZE) {
        errno = -EINVAL;
        return;
    }

    uint32_t args[4];
    if (copy_from_user(current_task->address_space, args, args_ptr, sizeof(args)) != 0) {
        errno = -EFAULT;
        return;
    }

    uint32_t readfds_ptr   = args[0];
    uint32_t writefds_ptr  = args[1];
    uint32_t exceptfds_ptr = args[2];
    uint32_t timeout_ptr   = args[3];

    kernel_fd_set readfds, writefds, exceptfds;
    K_FD_ZERO(&readfds);
    K_FD_ZERO(&writefds);
    K_FD_ZERO(&exceptfds);

    if (readfds_ptr) {
        if (copy_from_user(current_task->address_space, &readfds, readfds_ptr, sizeof(kernel_fd_set)) != 0) {
            errno = -EFAULT; return;
        }
    }
    if (writefds_ptr) {
        if (copy_from_user(current_task->address_space, &writefds, writefds_ptr, sizeof(kernel_fd_set)) != 0) {
            errno = -EFAULT; return;
        }
    }
    if (exceptfds_ptr) {
        if (copy_from_user(current_task->address_space, &exceptfds, exceptfds_ptr, sizeof(kernel_fd_set)) != 0) {
            errno = -EFAULT; return;
        }
    }

    int has_timeout = 0;
    uint64_t deadline = 0;
    if (timeout_ptr) {
        struct kernel_timeval tv;
        if (copy_from_user(current_task->address_space, &tv, timeout_ptr, sizeof(tv)) != 0) {
            errno = -EFAULT; return;
        }
        has_timeout = 1;
        uint64_t timeout_ms = (uint64_t)tv.tv_sec * 1000 + (uint64_t)(tv.tv_usec / 1000);
        if (timeout_ms == 0) has_timeout = 2;
        deadline = timer_ticks + timeout_ms;
    }

    kernel_fd_set res_r, res_w, res_e;
    K_FD_ZERO(&res_r);
    K_FD_ZERO(&res_w);
    K_FD_ZERO(&res_e);

    int ready = 0;
    for (int fd = 0; fd < nfds; fd++) {
        int want_r = readfds_ptr   && K_FD_ISSET(fd, &readfds);
        int want_w = writefds_ptr  && K_FD_ISSET(fd, &writefds);
        int want_e = exceptfds_ptr && K_FD_ISSET(fd, &exceptfds);
        if (!want_r && !want_w && !want_e)
            continue;

        int events = fd_poll_check(fd);
        if (events & POLLNVAL) { errno = -EBADF; return; }
        if (want_r && (events & (POLLIN | POLLHUP | POLLERR))) {
            K_FD_SET(fd, &res_r); ready++;
        }
        if (want_w && (events & POLLOUT)) {
            K_FD_SET(fd, &res_w); ready++;
        }
        if (want_e && (events & POLLERR)) {
            K_FD_SET(fd, &res_e); ready++;
        }
    }

    if (ready > 0 || has_timeout == 2) {
        if (readfds_ptr)
            copy_to_user(current_task->address_space, readfds_ptr, &res_r, sizeof(kernel_fd_set));
        if (writefds_ptr)
            copy_to_user(current_task->address_space, writefds_ptr, &res_w, sizeof(kernel_fd_set));
        if (exceptfds_ptr)
            copy_to_user(current_task->address_space, exceptfds_ptr, &res_e, sizeof(kernel_fd_set));
        errno = ready;
        return;
    }

    poll_waiter_t *w = kernel_malloc(sizeof(poll_waiter_t));
    if (!w) { errno = -ENOMEM; return; }

    w->task          = current_task;
    w->type          = POLL_WAITER_SELECT;
    w->has_timeout   = has_timeout;
    w->deadline      = deadline;
    w->nfds          = nfds;
    w->readfds       = readfds;
    w->writefds      = writefds;
    w->exceptfds     = exceptfds;
    w->readfds_ptr   = readfds_ptr;
    w->writefds_ptr  = writefds_ptr;
    w->exceptfds_ptr = exceptfds_ptr;
    w->pfds          = NULL;
    w->poll_nfds     = 0;
    w->poll_fds_ptr  = 0;

    lock_scheduler();
    poll_waiter_add(w);
    current_task->state = PROCESS_STATE_BLOCKED;
    unlock_scheduler();
    task_yield(1);
    __builtin_unreachable();
}

static void sys_poll(uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    uint32_t fds_ptr = arg2;
    uint32_t nfds    = arg3;
    int32_t  timeout = (int32_t)arg4;

    if (nfds > FD_SETSIZE) {
        errno = -EINVAL;
        return;
    }

    if (nfds == 0) {
        errno = 0;
        return;
    }

    uint32_t pfd_size = nfds * sizeof(struct kernel_pollfd);

    struct kernel_pollfd stack_pfds[FD_SETSIZE];
    struct kernel_pollfd *pfds = stack_pfds;

    if (copy_from_user(current_task->address_space, pfds, fds_ptr, pfd_size) != 0) {
        errno = -EFAULT;
        return;
    }

    int ready = 0;
    for (uint32_t i = 0; i < nfds; i++) {
        pfds[i].revents = 0;
        if (pfds[i].fd < 0) continue;

        int events = fd_poll_check(pfds[i].fd);
        int16_t rev = 0;
        if (events & POLLNVAL) { rev = POLLNVAL; }
        else {
            if ((pfds[i].events & POLLIN)  && (events & POLLIN))  rev |= POLLIN;
            if ((pfds[i].events & POLLOUT) && (events & POLLOUT)) rev |= POLLOUT;
            if (events & POLLERR) rev |= POLLERR;
            if (events & POLLHUP) rev |= POLLHUP;
        }
        pfds[i].revents = rev;
        if (rev) ready++;
    }

    if (ready > 0 || timeout == 0) {
        copy_to_user(current_task->address_space, fds_ptr, pfds, pfd_size);
        errno = ready;
        return;
    }

    struct kernel_pollfd *heap_pfds = kernel_malloc(pfd_size);
    if (!heap_pfds) { errno = -ENOMEM; return; }
    __builtin_memcpy(heap_pfds, pfds, pfd_size);

    poll_waiter_t *w = kernel_malloc(sizeof(poll_waiter_t));
    if (!w) { kernel_free(heap_pfds); errno = -ENOMEM; return; }

    w->task          = current_task;
    w->type          = POLL_WAITER_POLL;
    w->has_timeout   = (timeout >= 0);
    w->deadline      = (timeout >= 0) ? timer_ticks + (uint64_t)timeout : 0;
    w->pfds          = heap_pfds;
    w->poll_nfds     = nfds;
    w->poll_fds_ptr  = fds_ptr;
    w->nfds          = 0;
    K_FD_ZERO(&w->readfds);
    K_FD_ZERO(&w->writefds);
    K_FD_ZERO(&w->exceptfds);
    w->readfds_ptr   = 0;
    w->writefds_ptr  = 0;
    w->exceptfds_ptr = 0;

    lock_scheduler();
    poll_waiter_add(w);
    current_task->state = PROCESS_STATE_BLOCKED;
    unlock_scheduler();
    task_yield(1);
    __builtin_unreachable();
}

static void sys_5ht_sysinfo(uint32_t arg2) {
    sysinfo_5ht_t sysinfo = {0};
    sysinfo.mem_free = (buddy_free_pages() *4000)/1000000;
    sysinfo.mem_total = (buddy_total_pages() *4000)/1000000;

    uint32_t cpu_kernel = 0;
    uint32_t cpu_user = 0;
    for (process_control_block_t *t = task_list; t; t = t->next) {
        cpu_user   += t->cpu_user_ticks;
        cpu_kernel += t->cpu_kernel_ticks;
    }
    sysinfo.cpu_kernel_total = cpu_kernel;
    sysinfo.cpu_user_total   = cpu_user;

    if (copy_to_user(current_task->address_space, (uint32_t)arg2, &sysinfo, sizeof(sysinfo)) != 0) {
        errno = -EFAULT;
        return;
    }
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
        case SYSTEM_CALL_5HT_SWAP_LAYERS:
            sys_5ht_swap_layers(arg2, arg3);
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
        case SYSTEM_CALL_5HT_PTY_OPEN:
            sys_5ht_pty_open(arg2);
            break;
        case SYSTEM_CALL_5HT_PTY_SETATTR:
            sys_5ht_pty_setattr(arg2, arg3);
            break;
        case SYSTEM_CALL_5HT_PTY_GETATTR:
            sys_5ht_pty_getattr(arg2, arg3);
            break;
        case SYSTEM_CALL_5HT_PTY_WINSIZE:
            sys_5ht_pty_winsize(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_5HT_PTY_SETPGRP:
            sys_5ht_pty_setpgrp(arg2);
            break;
        case SYSTEM_CALL_ALARM:
            sys_alarm(arg2);
            break;
        case SYSTEM_CALL_SOCKET:
            sys_socket(arg2, arg3);
            break;
        case SYSTEM_CALL_BIND:
            sys_bind(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_LISTEN:
            sys_listen(arg2, arg3);
            break;
        case SYSTEM_CALL_ACCEPT:
            sys_accept(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_CONNECT:
            sys_connect(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_SEND:
            sys_send(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_RECV:
            sys_recv(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_SHUTDOWN:
            sys_shutdown(arg2, arg3);
            break;
        case SYSTEM_CALL_SOCKETPAIR:
            sys_socketpair(arg2, arg3);
            break;
        case SYSTEM_CALL_SELECT:
            sys_select(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_POLL:
            sys_poll(arg2, arg3, arg4);
            break;
        case SYSTEM_CALL_5HT_GRAB_INPUT:
            keyboard_grab_active = arg2 ? 1 : 0;
            ctx->eax = 0;
            break;
        case SYSTEM_CALL_5HT_SYSINFO:
            sys_5ht_sysinfo(arg2);
            break;
        case SYSTEM_CALL_USLEEP:
            sys_usleep(arg2);
            break;
        default:
            handle_illegal_call(arg2, arg3, arg4, ctx->eip);
            __builtin_unreachable();
    }

    ctx->eax = errno;

    /* lock scheduler so the PIT alarm loop cannot race with
       signal_bitmask read-modify-write or with task_exit's
       task_list manipulation if a default-action signal kills
       the task here.  switch_task resets lock_count if we
       never return (task_exit path). */
    lock_scheduler();
    task_ipc_deliver_signals(current_task, ctx);
    unlock_scheduler();

    printfs(PRINT_STATUS_DEBUG, "[SYSCALL] exiting kernel\n");

    // irqs must be disabled before dropping the preempt guard
    asm volatile("cli");
    preempt_enable();
}
