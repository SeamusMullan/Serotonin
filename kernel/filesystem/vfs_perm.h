#ifndef _VFS_PERM_H
#define _VFS_PERM_H

#include "vfs.h"
#include "../schedule/schedule.h"

#define PERM_READ  0x04
#define PERM_WRITE 0x02
#define PERM_EXEC  0x01

int vfs_check_permission(vfs_node_t *node, process_control_block_t *proc, int want);
int vfs_check_dir_write(vfs_node_t *dir, process_control_block_t *proc);
int proc_in_group(process_control_block_t *proc, uint16_t gid);

#endif
