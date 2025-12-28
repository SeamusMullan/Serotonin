/*
 * vfs.c
 * Virtual Filesystem for Serotonin
*/

#include "vfs.h"
#include <stdint.h>
#include "../string.h"
#include "../stdlib/stdlib.h"
#include "../stdio/stdio.h"

// Global VFS state
vfs_node_t *vfs_root = NULL;
filesystem_t *registered_filesystems = NULL;

/**
 * @brief Initializes the Virtual Filesystem (VFS).
 *
 * This function initializes the global VFS state.
 */
void vfs_init(void) {
    vfs_root = NULL;
    registered_filesystems = NULL;
}

/**
 * @brief Registers a filesystem with the VFS.
 *
 * @param fs Pointer to the filesystem to register.
 */
void vfs_register_fs(filesystem_t *fs) {
    fs->next = registered_filesystems;
    registered_filesystems = fs;
}

/**
 * @brief Mounts a filesystem at the specified mount point.
 *
 * @param device The device identifier.
 * @param mountpoint The mount point (e.g., "/").
 * @param fs_type The filesystem type to mount.
 * @return 0 on success, -1 if mount failed, -2 if non-root mounting not implemented, -3 if filesystem type not found.
 */
int vfs_mount(const char *device, const char *mountpoint, const char *fs_type) {
    filesystem_t *fs = registered_filesystems;

    while (fs != NULL) {
        if (strcmp(fs->name, fs_type) == 0) {
            vfs_node_t *root = fs->mount(device);
            if (root == NULL) return -1;

            if (strcmp(mountpoint, "/") == 0) {
                vfs_root = root;
                return 0;
            } else {
                // TODO: Non-root mounting not implemented
                return -2;
            }
        }
        fs = fs->next;
    }
    return -3; // Filesystem not found
}

/**
 * @brief Resolves a path to a VFS node.
 *
 * @param path The absolute path to resolve.
 * @return Pointer to the corresponding VFS node, or NULL if not found.
 */
vfs_node_t *vfs_resolve_path(const char *path) {
    if (!vfs_root || !path || path[0] != '/') return NULL;

    char temp[256];
    strncpy(temp, path, sizeof(temp));
    temp[sizeof(temp)-1] = '\0'; // Safety null-termination

    vfs_node_t *current = vfs_root;
    vfs_node_t *stack[64];
    size_t depth = 0;
    stack[depth++] = current;
    char *token = strtok(temp, "/");

    while (token != NULL && current != NULL) {
        if (strcmp(token, ".") == 0) {
            token = strtok(NULL, "/");
            continue;
        }
        if (strcmp(token, "..") == 0) {
            if (depth > 1) {
                depth--;
                current = stack[depth - 1];
            }
            token = strtok(NULL, "/");
            continue;
        }

        if (!(current->flags & VFS_FLAG_DIRECTORY)) {
            return NULL; // Can't descend into non-directory
        }

        if (current->ops && current->ops->finddir) {
            current = current->ops->finddir(current, token);
            if (!current) {
                return NULL;
            }
            if (depth < (sizeof(stack) / sizeof(stack[0]))) {
                stack[depth++] = current;
            }
        } else {
            return NULL;
        }

        token = strtok(NULL, "/");
    }

    return current;
}

/**
 * @brief Opens a file or directory at the specified path.
 *
 * @param path The absolute path to open.
 * @return Pointer to the opened VFS node, or NULL on failure.
 */
vfs_node_t *vfs_open(const char *path) {
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) return NULL;

    if (node->ops && node->ops->open) {
        if (node->ops->open(node) != 0) {
            return NULL; // open failed
        }
    }

    node->refcount++;
    return node;
}

/**
 * @brief Reads data from a VFS node.
 *
 * @param node Pointer to the VFS node to read from.
 * @param offset Offset in the file to start reading.
 * @param size Number of bytes to read.
 * @param buffer Buffer to store read data.
 * @return Number of bytes read, or -1 on failure.
 */
int vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    if (!node || !node->ops || !node->ops->read) return -1;
    return node->ops->read(node, offset, size, buffer);
}


/**
 * @brief Writes data to a VFS node.
 *
 * @param node Pointer to the VFS node to write to.
 * @param offset Offset in the file to start writing.
 * @param size Number of bytes to write.
 * @param buffer Buffer containing data to write.
 * @return Number of bytes written, or -1 on failure.
 */
int vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    if (!node || !node->ops || !node->ops->write) return -1;
    return node->ops->write(node, offset, size, buffer);
}

/**
 * @brief Truncates a VFS node to a specific size.
 *
 * @param node Pointer to the VFS node to truncate.
 * @param size The new size of the node.
 * @return 0 on success, -1 on failure.
 */
int vfs_truncate(vfs_node_t *node, uint32_t size) {
    if (!node || !node->ops || !node->ops->truncate) return -1;
    return node->ops->truncate(node, size);
}

/**
 * @brief Closes a previously opened VFS node.
 *
 * @param node Pointer to the VFS node to close.
 */
void vfs_close(vfs_node_t *node) {
    if (!node) return;

    if (node->ops && node->ops->close) {
        node->ops->close(node);
    }

    if (node->refcount > 0) {
        node->refcount--;
    }
}

/**
 * @brief Lists the contents of a directory.
 *
 * @param path Path to the directory to list.
 */
void vfs_list_dir(const char *path) {
    vfs_node_t *dir = vfs_open(path);
    if (!dir) {
        printf("vfs_list_dir: cannot open directory %s\n", path);
        return;
    }

    if (!(dir->flags & VFS_FLAG_DIRECTORY)) {
        printf("vfs_list_dir: %s is not a directory\n", path);
        vfs_close(dir);
        return;
    }

    printf("Listing directory: %s\n", path);

    for (uint32_t i = 0;; i++) {
        vfs_node_t *child = dir->ops->readdir(dir, i);
        if (!child) break;

        printf("  %s %s (%u bytes)\n",
            (child->flags & VFS_FLAG_DIRECTORY) ? "[DIR] " : "[FILE]",
            child->name,
            child->size);

        vfs_close(child);
    }

    vfs_close(dir);
}

/**
 * @brief Splits a path into parent directory and name.
 *
 * @param path The full path to split.
 * @param parent The buffer to store the parent directory.
 * @param name The buffer to store the name.
 */
static void split_path(const char *path, char *parent, char *name) {
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp));
    tmp[255] = 0;
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) {
        // root or immediate child of /
        strcpy(parent, "/");
        strcpy(name, slash ? slash+1 : tmp);
    } else {
        *slash = 0;
        strcpy(parent, tmp);
        strcpy(name, slash+1);
    }
}

/**
 * @brief Removes a directory at the specified path.
 *
 * @param path The absolute path to remove.
 * @return 0 on success, -1 on failure.
 */
int vfs_rmdir(const char *path) {
    if (!path || path[0] != '/') return -1;

    char parent_path[256], name[256];
    split_path(path, parent_path, name);

    vfs_node_t *parent = vfs_open(parent_path);
    if (!parent) return -1;

    if (!parent->ops || !parent->ops->rmdir) {
        vfs_close(parent);
        return -1;
    }

    int result = parent->ops->rmdir(parent, name);
    vfs_close(parent);
    return result;
}

/**
 * @brief Unlinks a file at the specified path.
 *
 * @param path The absolute path to unlink.
 * @return 0 on success, -1 on failure.
 */
int vfs_unlink(const char *path) {
    if (!path || path[0] != '/') return -1;

    char parent_path[256], name[256];
    split_path(path, parent_path, name);

    vfs_node_t *parent = vfs_open(parent_path);
    if (!parent) return -1;

    if (!parent->ops || !parent->ops->unlink) {
        vfs_close(parent);
        return -1;
    }

    int result = parent->ops->unlink(parent, name);
    vfs_close(parent);
    return result;
}

/**
 * @brief Creates a file at the specified path.
 *
 * @param path The path where the file will be created.
 * @return Pointer to the newly created VFS node, or NULL on failure.
 */
vfs_node_t *vfs_create(const char *path) {
    char parent_path[256], name[256];
    split_path(path, parent_path, name);

    vfs_node_t *dir = vfs_open(parent_path);
    if (!dir || !dir->ops->create) {
        if (dir) vfs_close(dir);
        return NULL;
    }

    vfs_node_t *newnode = dir->ops->create(dir, name);
    vfs_close(dir);
    return newnode;
}

/**
 * @brief Creates a directory at the specified path.
 *
 * @param path The path where the directory will be created.
 * @return 0 on success, -1 on failure.
 */
int vfs_mkdir(const char *path) {
    char parent_path[256], name[256];
    split_path(path, parent_path, name);

    vfs_node_t *dir = vfs_open(parent_path);
    if (!dir || !dir->ops->mkdir) {
        if (dir) vfs_close(dir);
        return -1;
    }

    vfs_node_t *newdir = dir->ops->mkdir(dir, name);
    vfs_close(dir);
    return newdir ? 0 : -1;
}
