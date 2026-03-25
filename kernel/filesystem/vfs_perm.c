#include <kernel/filesystem/vfs_perm.h>
#include <kernel/syscall/sys/file.h>

int proc_in_group(process_control_block_t *proc, uint16_t gid) {
    if (proc->egid == gid) return 1;
    for (uint8_t i = 0; i < proc->ngroups; i++) {
        if (proc->groups[i] == gid) return 1;
    }
    return 0;
}

int vfs_check_permission(vfs_node_t *node, process_control_block_t *proc, int want) {
    if (!node || !proc) return -1;

    // root bypasses all checks except exec requires at least one x bit
    if (proc->euid == 0) {
        /*
        if (want & PERM_EXEC) {
            uint32_t m = node->mode;
            if (m == 0) return 0; // uninitialized node, allow
            if ((node->flags & VFS_FLAG_DIRECTORY)) return 0; // dirs always searchable by root
            if (m & (S_IXUSR | S_IXGRP | S_IXOTH)) return 0;
            return -1; // no x bit set anywhere
        }
        */
        return 0;
    }

    // uninitialized mode -> allow
    if (node->mode == 0) return 0;

    uint32_t perm;
    if (proc->euid == node->uid) {
        perm = (node->mode >> 6) & 0x7;
    } else if (proc_in_group(proc, node->gid)) {
        perm = (node->mode >> 3) & 0x7;
    } else {
        perm = node->mode & 0x7;
    }

    if ((perm & want) == (uint32_t)want) return 0;
    return -1;
}

int vfs_check_dir_write(vfs_node_t *dir, process_control_block_t *proc) {
    return vfs_check_permission(dir, proc, PERM_WRITE | PERM_EXEC);
}
