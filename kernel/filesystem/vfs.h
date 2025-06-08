#ifndef _VFS_H
#define _VFS_H
/*
 * vfs.h
 * Virtual Filesystem for Serotonin
*/

#include <stdint.h>

#define VFS_FLAG_FILE      0x1
#define VFS_FLAG_DIRECTORY 0x2
#define VFS_FLAG_SYMLINK   0x4

typedef struct vfs_node vfs_node_t;
typedef struct vfs_ops vfs_ops_t;
typedef struct filesystem filesystem_t;


typedef struct vfs_node {
    char name[256];              // Name of this node
    uint32_t inode;              // Inode number (if applicable)
    uint32_t size;               // File size
    uint32_t flags;              // File / directory / device / symlink
    uint32_t refcount;           // For resource tracking
    
    struct filesystem *fs;       // Filesystem driver owning this node
    void *fs_data;               // Filesystem-private data (inode, etc.)

    struct vfs_ops *ops;         // Per-node ops
    struct vfs_node *parent;     // Parent directory
    struct vfs_node *children;   // First child (for directories)
    struct vfs_node *next;       // Next sibling (for directories)
} vfs_node_t;

typedef struct vfs_ops {
    int (*read)(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
    int (*write)(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
    int (*open)(vfs_node_t *node);
    int (*close)(vfs_node_t *node);
    struct vfs_node *(*readdir)(vfs_node_t *node, uint32_t index);
    vfs_node_t *(*finddir)(vfs_node_t *node, const char *name);
} vfs_ops_t;

typedef struct filesystem {
    char name[16];                           // Filesystem name
    struct vfs_node *(*mount)(const char *device);
    struct filesystem *next;                 // Linked list of registered FSes
} filesystem_t;

void vfs_init(void);
void vfs_register_fs(filesystem_t *fs);
int vfs_mount(const char *device, const char *mountpoint, const char *fs_type);
vfs_node_t *vfs_open(const char *path);
int vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
void vfs_close(vfs_node_t *node);
void vfs_list_dir(const char *path);

vfs_node_t *vfs_resolve_path(const char *path);

#endif