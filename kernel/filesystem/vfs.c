/*
 * vfs.c
 * Virtual Filesystem for Serotonin
*/

#include <kernel/filesystem/vfs.h>
#include <stdint.h>
#include <kernel/string.h>
#include <kernel/stdlib/stdlib.h>
#include <kernel/stdio/stdio.h>
#include <kernel/kernel.h>

// Global VFS state
vfs_node_t *vfs_root = NULL;
filesystem_t *registered_filesystems = NULL;
vfs_mount_entry_t *vfs_mounts = NULL;

/**
 * @brief Attaches a mounted filesystem root onto an existing mountpoint node.
 *
 * This keeps the mountpoint node pointer stable (so parent->finddir still works),
 * while replacing its filesystem-specific fields with the mounted root's.
 */
void vfs_attach_mount(vfs_node_t *mountpoint, vfs_node_t *root) {
    char saved_name[256];
    vfs_node_t *saved_parent = mountpoint->parent;
    vfs_node_t *saved_next = mountpoint->next;
    uint32_t saved_refcount = mountpoint->refcount;

    strncpy(saved_name, mountpoint->name, sizeof(saved_name));
    saved_name[sizeof(saved_name) - 1] = '\0';

    *mountpoint = *root;

    strncpy(mountpoint->name, saved_name, sizeof(mountpoint->name));
    mountpoint->name[sizeof(mountpoint->name) - 1] = '\0';
    mountpoint->parent = saved_parent;
    mountpoint->next = saved_next;
    mountpoint->refcount = saved_refcount;

    for (vfs_node_t *child = mountpoint->children; child; child = child->next) {
        child->parent = mountpoint;
    }
}

/**
 * @brief Initializes the Virtual Filesystem (VFS).
 *
 * This function initializes the global VFS state.
 */
void vfs_init(void) {
    vfs_root = NULL;
    registered_filesystems = NULL;
    vfs_mounts = NULL;
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
 * @return 0 on success, -1 if mount failed, -3 if filesystem type not found.
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
                if (!vfs_root) return -1;
                vfs_node_t *mp = vfs_resolve_path(mountpoint);
                if (!mp) {
                    char parent_path[256], name[256];
                    split_path(mountpoint, parent_path, name);
                    vfs_node_t *parent = vfs_open(parent_path);
                    if (!parent) return -1;
                    if (!parent->ops || !parent->ops->mkdir) {
                        vfs_close(parent);
                        return -1;
                    }
                    vfs_node_t *newdir = parent->ops->mkdir(parent, name);
                    vfs_close(parent);
                    if (!newdir) return -1;
                    mp = newdir;
                }
                if (!(mp->flags & VFS_FLAG_DIRECTORY)) return -1;

                vfs_attach_mount(mp, root);
                vfs_register_mount(mountpoint, mp);
                return 0;
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
/**
 * @brief Frees intermediate DISKIO nodes allocated during path resolution,
 *        keeping only the result node alive.
 */
static void resolve_cleanup(vfs_node_t **allocs, size_t count, vfs_node_t *keep) {
    for (size_t i = 0; i < count; i++) {
        if (allocs[i] != keep) {
            vfs_put(allocs[i]);
        }
    }
}

vfs_node_t *vfs_resolve_path(const char *path) {
    if (!vfs_root || !path || path[0] != '/') return NULL;

    char temp[256];
    strncpy(temp, path, sizeof(temp));
    temp[sizeof(temp)-1] = '\0'; // Safety null-termination

    vfs_node_t *current = vfs_root;
    vfs_node_t *stack[64];
    size_t depth = 0;
    stack[depth++] = current;

    /* Track DISKIO nodes allocated by finddir so we can free intermediates */
    vfs_node_t *diskio_allocs[64];
    size_t diskio_count = 0;

    char resolved[256];
    size_t resolved_len = 1;
    resolved[0] = '/';
    resolved[1] = '\0';
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
                if (resolved_len > 1) {
                    char *last = strrchr(resolved, '/');
                    if (last == resolved) {
                        resolved_len = 1;
                        resolved[1] = '\0';
                    } else if (last) {
                        *last = '\0';
                        resolved_len = (size_t)(last - resolved);
                    }
                }
            }
            token = strtok(NULL, "/");
            continue;
        }

        if (!(current->flags & VFS_FLAG_DIRECTORY)) {
            resolve_cleanup(diskio_allocs, diskio_count, NULL);
            return NULL; // Can't descend into non-directory
        }

        char next_path[256];
        size_t next_len = resolved_len;
        if (next_len > 1) {
            if (next_len + 1 >= sizeof(next_path)) {
                resolve_cleanup(diskio_allocs, diskio_count, NULL);
                return NULL;
            }
            memcpy(next_path, resolved, next_len);
            next_path[next_len++] = '/';
        } else {
            next_path[0] = '/';
            next_len = 1;
        }
        size_t token_len = strlen(token);
        if (next_len + token_len >= sizeof(next_path)) {
            resolve_cleanup(diskio_allocs, diskio_count, NULL);
            return NULL;
        }
        memcpy(next_path + next_len, token, token_len);
        next_len += token_len;
        next_path[next_len] = '\0';

        vfs_node_t *mount_node = vfs_lookup_mount(next_path);
        if (mount_node) {
            current = mount_node;
        } else if (current->ops && current->ops->finddir) {
            current = current->ops->finddir(current, token);
            if (!current) {
                resolve_cleanup(diskio_allocs, diskio_count, NULL);
                return NULL;
            }
            if (current->flags & VFS_FLAG_DISKIO) {
                if (diskio_count < 64)
                    diskio_allocs[diskio_count++] = current;
            }
            if (!current->parent) {
                current->parent = stack[depth - 1];
            }
        } else {
            resolve_cleanup(diskio_allocs, diskio_count, NULL);
            return NULL;
        }

        if (depth < (sizeof(stack) / sizeof(stack[0]))) {
            stack[depth++] = current;
        }
        strncpy(resolved, next_path, sizeof(resolved));
        resolved[sizeof(resolved) - 1] = '\0';
        resolved_len = strlen(resolved);
        token = strtok(NULL, "/");
    }

    /* Free all intermediate DISKIO nodes except the result */
    resolve_cleanup(diskio_allocs, diskio_count, current);

    return current;
}

vfs_node_t *vfs_lookup_mount(const char *path) {
    if (!path) return NULL;
    for (vfs_mount_entry_t *entry = vfs_mounts; entry; entry = entry->next) {
        if (strcmp(entry->path, path) == 0) {
            return entry->node;
        }
    }
    return NULL;
}

void vfs_register_mount(const char *path, vfs_node_t *node) {
    if (!path || !node) return;

    char normalized[256];
    vfs_normalize_mount_path(path, normalized, sizeof(normalized));
    if (strcmp(normalized, "/") == 0) {
        return;
    }

    for (vfs_mount_entry_t *entry = vfs_mounts; entry; entry = entry->next) {
        if (strcmp(entry->path, normalized) == 0) {
            entry->node = node;
            return;
        }
    }

    vfs_mount_entry_t *entry = kernel_malloc(sizeof(*entry));
    if (!entry) return;
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->path, normalized, sizeof(entry->path));
    entry->path[sizeof(entry->path) - 1] = '\0';
    entry->node = node;
    entry->next = vfs_mounts;
    vfs_mounts = entry;
}

void vfs_normalize_mount_path(const char *path, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!path || path[0] == '\0') {
        out[0] = '\0';
        return;
    }

    strncpy(out, path, out_size);
    out[out_size - 1] = '\0';

    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/') {
        out[len - 1] = '\0';
        len--;
    }
    // Mountpoints are expected to be absolute; leave as-is if not.
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
            vfs_put(node);
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
 * @brief Releases an ephemeral DISKIO node returned by vfs_resolve_path or readdir.
 *
 * Only frees nodes that were dynamically allocated by finddir/readdir (refcount == 0).
 * Persistent nodes (root, created files/dirs with refcount >= 1) are left alone.
 */
void vfs_put(vfs_node_t *node) {
    if (!node) return;
    if ((node->flags & VFS_FLAG_DISKIO) && node->refcount == 0) {
        if (node->fs_data) kernel_free(node->fs_data);
        kernel_free(node);
    }
}

/**
 * @brief Closes a previously opened VFS node (from vfs_open).
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

    /* Free dynamically-allocated disk-backed nodes when no longer referenced */
    if (node->refcount == 0 && (node->flags & VFS_FLAG_DISKIO)) {
        if (node->fs_data) kernel_free(node->fs_data);
        kernel_free(node);
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

        vfs_put(child);
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
void split_path(const char *path, char *parent, char *name) {
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
