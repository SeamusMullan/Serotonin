#include "tmpfs.h"
#include "../vfs.h"
#include "../../stdlib/stdlib.h"
#include "../../stdio/stdio.h"
#include "../../string.h"
#include "../../kernel.h"

// Forward declarations
static vfs_node_t *tmpfs_mount(const char *device);
static int tmpfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
static int tmpfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
static int tmpfs_truncate(vfs_node_t *node, uint32_t size);
static int tmpfs_unlink(vfs_node_t *parent, const char *name);
static int tmpfs_rmdir(vfs_node_t *parent, const char *name);
static int tmpfs_open(vfs_node_t *node);
static int tmpfs_close(vfs_node_t *node);
static vfs_node_t *tmpfs_readdir(vfs_node_t *node, uint32_t index);
static vfs_node_t *tmpfs_finddir(vfs_node_t *node, const char *name);
vfs_node_t *tmpfs_create_file(vfs_node_t *parent, const char *name);
vfs_node_t *tmpfs_create_dir(vfs_node_t *parent, const char *name);

// VFS ops
static vfs_ops_t tmpfs_ops = {
    .read    = tmpfs_read,
    .write   = tmpfs_write,
    .truncate = tmpfs_truncate,
    .unlink = tmpfs_unlink,
    .rmdir = tmpfs_rmdir,
    .open    = tmpfs_open,
    .close   = tmpfs_close,
    .readdir = tmpfs_readdir,
    .finddir = tmpfs_finddir,
    .create  = tmpfs_create_file,
    .mkdir   = tmpfs_create_dir
};

// Filesystem registration
static filesystem_t tmpfs_fs = {
    .name = "tmpfs",
    .mount = tmpfs_mount,
    .next = NULL
};

/**
 * @brief Initializes the tmpfs filesystem.
 *
 */
void tmpfs_init(void) {
    vfs_register_fs(&tmpfs_fs);
}

/**
 * @brief Mounts a tmpfs filesystem.
 *
 * @param device The device to mount (unused).
 * @return vfs_node_t* The root directory of the mounted filesystem.
 */
static vfs_node_t *tmpfs_mount(const char *device) {
    (void) device;  // unused

    vfs_node_t *root = kernel_malloc(sizeof(vfs_node_t));
    memset(root, 0, sizeof(vfs_node_t));

    strcpy(root->name, "/");
    root->inode = 0;
    root->flags = VFS_FLAG_DIRECTORY;
    root->size = 0;
    root->refcount = 1;
    root->ops = &tmpfs_ops;

    tmpfs_dir_t *root_dir = kernel_malloc(sizeof(tmpfs_dir_t));
    memset(root_dir, 0, sizeof(tmpfs_dir_t));
    root->fs_data = root_dir;

    return root;
}

/**
 * @brief Opens a file in the tmpfs filesystem.
 *
 * @param node The VFS node representing the file to open.
 * @return int 0 on success, -1 on failure.
 */
static int tmpfs_open(vfs_node_t *node) {
    // No-op for tmpfs
    return 0;
}

/**
 * @brief Closes a file in the tmpfs filesystem.
 *
 * @param node The VFS node representing the file to close.
 * @return int 0 on success, -1 on failure.
 */
static int tmpfs_close(vfs_node_t *node) {
    // No-op for tmpfs
    return 0;
}

/**
 * @brief Reads data from a file in the tmpfs filesystem.
 *
 * @param node The VFS node representing the file to read from.
 * @param offset The offset to read from.
 * @param size The number of bytes to read.
 * @param buffer The buffer to read data into.
 * @return int The number of bytes read, or -1 on failure.
 */
static int tmpfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    if (!(node->flags & VFS_FLAG_FILE)) return -1;

    tmpfs_file_t *file = (tmpfs_file_t *) node->fs_data;
    if (offset >= file->size) return 0;

    uint32_t bytes_to_read = (offset + size > file->size) ? (file->size - offset) : size;
    memcpy(buffer, file->data + offset, bytes_to_read);

    return bytes_to_read;
}

/**
 * @brief Writes data to a file in the tmpfs filesystem.
 *
 * @param node The VFS node representing the file to write to.
 * @param offset The offset to write to.
 * @param size The number of bytes to write.
 * @param buffer The buffer containing the data to write.
 * @return int The number of bytes written, or -1 on failure.
 */
static int tmpfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    if (!(node->flags & VFS_FLAG_FILE)) return -1;

    tmpfs_file_t *file = (tmpfs_file_t *) node->fs_data;
    uint32_t new_size = offset + size;

    if (new_size > file->size) {
        char *new_data = kernel_malloc(new_size);
        memset(new_data, 0, new_size);

        if (file->data) {
            memcpy(new_data, file->data, file->size);
            kernel_free(file->data);
        }

        file->data = new_data;
        file->size = new_size;
        node->size = new_size;
    }

    memcpy(file->data + offset, buffer, size);
    return size;
}

/**
 * @brief Truncates a file in the tmpfs filesystem.
 *
 * @param node The VFS node representing the file to truncate.
 * @param size The new size of the file.
 * @return int 0 on success, -1 on failure.
 */
static int tmpfs_truncate(vfs_node_t *node, uint32_t size) {
    if (!(node->flags & VFS_FLAG_FILE)) return -1;

    tmpfs_file_t *file = (tmpfs_file_t *) node->fs_data;
    if (!file) return -1;

    if (size == file->size) {
        node->size = size;
        return 0;
    }

    if (size == 0) {
        if (file->data) {
            kernel_free(file->data);
            file->data = NULL;
        }
        file->size = 0;
        node->size = 0;
        return 0;
    }

    char *new_data = kernel_malloc(size);
    if (!new_data) return -1;
    memset(new_data, 0, size);

    if (file->data) {
        uint32_t copy = (file->size < size) ? file->size : size;
        memcpy(new_data, file->data, copy);
        kernel_free(file->data);
    }

    file->data = new_data;
    file->size = size;
    node->size = size;
    return 0;
}

/**
 * @brief Unlinks a file from a tmpfs directory.
 *
 * @param parent The VFS node representing the parent directory.
 * @param name The name of the file to unlink.
 * @return int 0 on success, -1 on failure.
 */
static int tmpfs_unlink(vfs_node_t *parent, const char *name) {
    if (!(parent->flags & VFS_FLAG_DIRECTORY)) return -1;

    tmpfs_dir_t *dir = (tmpfs_dir_t *) parent->fs_data;
    tmpfs_dir_entry_t *prev = NULL;
    tmpfs_dir_entry_t *entry = dir->entries;

    while (entry) {
        if (strcmp(entry->node->name, name) == 0) {
            vfs_node_t *node = entry->node;
            if (node->flags & VFS_FLAG_DIRECTORY) return -1;
            if (node->refcount > 1) return -1;

            tmpfs_file_t *file = (tmpfs_file_t *) node->fs_data;
            if (file) {
                if (file->data) {
                    kernel_free(file->data);
                }
                kernel_free(file);
            }

            if (prev) {
                prev->next = entry->next;
            } else {
                dir->entries = entry->next;
            }
            kernel_free(entry);
            kernel_free(node);
            return 0;
        }

        prev = entry;
        entry = entry->next;
    }

    return -1;
}

/**
 * @brief Removes an empty directory from a tmpfs directory.
 *
 * @param parent The VFS node representing the parent directory.
 * @param name The name of the directory to remove.
 * @return int 0 on success, -1 on failure.
 */
static int tmpfs_rmdir(vfs_node_t *parent, const char *name) {
    if (!(parent->flags & VFS_FLAG_DIRECTORY)) return -1;

    tmpfs_dir_t *dir = (tmpfs_dir_t *) parent->fs_data;
    tmpfs_dir_entry_t *prev = NULL;
    tmpfs_dir_entry_t *entry = dir->entries;

    while (entry) {
        if (strcmp(entry->node->name, name) == 0) {
            vfs_node_t *node = entry->node;
            if (!(node->flags & VFS_FLAG_DIRECTORY)) return -1;
            if (node->refcount > 1) return -1;

            tmpfs_dir_t *child_dir = (tmpfs_dir_t *) node->fs_data;
            if (child_dir && child_dir->entries) return -1;

            if (child_dir) {
                kernel_free(child_dir);
            }

            if (prev) {
                prev->next = entry->next;
            } else {
                dir->entries = entry->next;
            }
            kernel_free(entry);
            kernel_free(node);
            return 0;
        }

        prev = entry;
        entry = entry->next;
    }

    return -1;
}

/**
 * @brief Reads the contents of a directory in the tmpfs filesystem.
 *
 * @param node The VFS node representing the directory to read.
 * @param index The index of the entry to read.
 * @return vfs_node_t* The VFS node representing the directory entry, or NULL on failure.
 */
static vfs_node_t *tmpfs_readdir(vfs_node_t *node, uint32_t index) {
    if (!(node->flags & VFS_FLAG_DIRECTORY)) return NULL;

    tmpfs_dir_t *dir = (tmpfs_dir_t *) node->fs_data;
    tmpfs_dir_entry_t *entry = dir->entries;

    uint32_t i = 0;
    while (entry != NULL) {
        if (i == index) {
            return entry->node;
        }
        entry = entry->next;
        i++;
    }

    return NULL;
}

/**
 * @brief Finds a directory entry by name in a tmpfs directory.
 *
 * @param node The VFS node representing the directory to search.
 * @param name The name of the directory entry to find.
 * @return vfs_node_t* The VFS node representing the found directory entry, or NULL on failure.
 */
static vfs_node_t *tmpfs_finddir(vfs_node_t *node, const char *name) {
    if (!(node->flags & VFS_FLAG_DIRECTORY)) return NULL;

    tmpfs_dir_t *dir = (tmpfs_dir_t *) node->fs_data;
    tmpfs_dir_entry_t *entry = dir->entries;

    while (entry != NULL) {
        if (strcmp(entry->node->name, name) == 0) {
            return entry->node;
        }
        entry = entry->next;
    }

    return NULL;
}

/**
 * @brief Creates a file in a tmpfs directory.
 *
 * @param parent The VFS node representing the parent directory.
 * @param name The name of the file to create.
 * @return vfs_node_t* The VFS node representing the created file, or NULL on failure.
 */
vfs_node_t *tmpfs_create_file(vfs_node_t *parent, const char *name) {
    if (!(parent->flags & VFS_FLAG_DIRECTORY)) return NULL;

    vfs_node_t *file = kernel_malloc(sizeof(vfs_node_t));
    memset(file, 0, sizeof(vfs_node_t));

    strncpy(file->name, name, sizeof(file->name));
    file->inode = (uint32_t) file; // unique ptr-based id
    file->flags = VFS_FLAG_FILE;
    file->size = 0;
    file->refcount = 1;
    file->ops = &tmpfs_ops;

    tmpfs_file_t *file_data = kernel_malloc(sizeof(tmpfs_file_t));
    memset(file_data, 0, sizeof(tmpfs_file_t));
    file->fs_data = file_data;

    // Link to parent directory
    tmpfs_dir_t *dir = (tmpfs_dir_t *) parent->fs_data;
    tmpfs_dir_entry_t *entry = kernel_malloc(sizeof(tmpfs_dir_entry_t));
    entry->node = file;
    entry->next = dir->entries;
    dir->entries = entry;

    file->parent = parent;

    return file;
}

/**
 * @brief Creates a directory in a tmpfs filesystem.
 *
 * @param parent The VFS node representing the parent directory.
 * @param name The name of the directory to create.
 * @return vfs_node_t* The VFS node representing the created directory, or NULL on failure.
 */
vfs_node_t *tmpfs_create_dir(vfs_node_t *parent, const char *name) {
    if (!(parent->flags & VFS_FLAG_DIRECTORY)) return NULL;

    vfs_node_t *dir_node = kernel_malloc(sizeof(vfs_node_t));
    memset(dir_node, 0, sizeof(vfs_node_t));

    strncpy(dir_node->name, name, sizeof(dir_node->name));
    dir_node->inode = (uint32_t) dir_node;
    dir_node->flags = VFS_FLAG_DIRECTORY;
    dir_node->size = 0;
    dir_node->refcount = 1;
    dir_node->ops = &tmpfs_ops;

    tmpfs_dir_t *dir_data = kernel_malloc(sizeof(tmpfs_dir_t));
    memset(dir_data, 0, sizeof(tmpfs_dir_t));
    dir_node->fs_data = dir_data;

    // Link to parent directory
    tmpfs_dir_t *parent_dir = (tmpfs_dir_t *) parent->fs_data;
    tmpfs_dir_entry_t *entry = kernel_malloc(sizeof(tmpfs_dir_entry_t));
    entry->node = dir_node;
    entry->next = parent_dir->entries;
    parent_dir->entries = entry;

    dir_node->parent = parent;

    return dir_node;
}
