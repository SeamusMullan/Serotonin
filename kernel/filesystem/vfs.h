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
#define VFS_FLAG_PIPE      0x8

typedef struct vfs_node vfs_node_t;
typedef struct vfs_ops vfs_ops_t;
typedef struct filesystem filesystem_t;

/**
 * @brief Virtual filesystem node.
 *
 * This structure represents a node in the virtual filesystem.
 */
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

/**
 * @brief Virtual filesystem operations.
 *
 * This structure contains the operations that can be performed on a virtual filesystem node.
 */
typedef struct vfs_ops {
    int (*read)(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
    int (*write)(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
    int (*truncate)(vfs_node_t *node, uint32_t size);
    int (*unlink)(vfs_node_t *parent, const char *name);
    int (*rmdir)(vfs_node_t *parent, const char *name);
    int (*open)(vfs_node_t *node);
    int (*close)(vfs_node_t *node);
    struct vfs_node *(*readdir)(vfs_node_t *node, uint32_t index);
    vfs_node_t *(*finddir)(vfs_node_t *node, const char *name);
    vfs_node_t *(*create)(vfs_node_t *parent, const char *name);
    vfs_node_t *(*mkdir)(vfs_node_t *parent, const char *name);
} vfs_ops_t;

/**
 * @brief Virtual filesystem structure.
 *
 * This structure represents a virtual filesystem.
 */
typedef struct filesystem {
    char name[16];                           // Filesystem name
    struct vfs_node *(*mount)(const char *device);
    struct filesystem *next;                 // Linked list of registered FSes
} filesystem_t;

typedef struct vfs_mount_entry {
    char path[256];
    vfs_node_t *node;
    struct vfs_mount_entry *next;
} vfs_mount_entry_t;

void vfs_init(void);
void vfs_register_fs(filesystem_t *fs);
int vfs_mount(const char *device, const char *mountpoint, const char *fs_type);
vfs_node_t *vfs_open(const char *path);
int vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
int vfs_truncate(vfs_node_t *node, uint32_t size);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
void vfs_close(vfs_node_t *node);
void vfs_list_dir(const char *path);
vfs_node_t *vfs_create(const char *path);
int vfs_mkdir(const char *path);
void split_path(const char *path, char *parent, char *name);
vfs_node_t *vfs_lookup_mount(const char *path);
void vfs_register_mount(const char *path, vfs_node_t *node);
void vfs_normalize_mount_path(const char *path, char *out, size_t out_size);

vfs_node_t *vfs_resolve_path(const char *path);

#endif
