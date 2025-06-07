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
static int tmpfs_open(vfs_node_t *node);
static int tmpfs_close(vfs_node_t *node);
static vfs_node_t *tmpfs_readdir(vfs_node_t *node, uint32_t index);
static vfs_node_t *tmpfs_finddir(vfs_node_t *node, const char *name);

// VFS ops
static vfs_ops_t tmpfs_ops = {
    .read = tmpfs_read,
    .write = tmpfs_write,
    .open = tmpfs_open,
    .close = tmpfs_close,
    .readdir = tmpfs_readdir,
    .finddir = tmpfs_finddir
};

// Filesystem registration
static filesystem_t tmpfs_fs = {
    .name = "tmpfs",
    .mount = tmpfs_mount,
    .next = NULL
};

void tmpfs_init(void) {
    vfs_register_fs(&tmpfs_fs);
}

// tmpfs_mount
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

// tmpfs_open
static int tmpfs_open(vfs_node_t *node) {
    // No-op for tmpfs
    return 0;
}

// tmpfs_close
static int tmpfs_close(vfs_node_t *node) {
    // No-op for tmpfs
    return 0;
}

// tmpfs_read
static int tmpfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    if (!(node->flags & VFS_FLAG_FILE)) return -1;

    tmpfs_file_t *file = (tmpfs_file_t *) node->fs_data;
    if (offset >= file->size) return 0;

    uint32_t bytes_to_read = (offset + size > file->size) ? (file->size - offset) : size;
    memcpy(buffer, file->data + offset, bytes_to_read);

    return bytes_to_read;
}

// tmpfs_write
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

// tmpfs_readdir
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

// tmpfs_finddir
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

// Helper: create a file in a tmpfs directory
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

// Helper: create a subdirectory in a tmpfs directory
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
