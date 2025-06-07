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

void vfs_init(void) {
    vfs_root = NULL;
    registered_filesystems = NULL;
}

void vfs_register_fs(filesystem_t *fs) {
    fs->next = registered_filesystems;
    registered_filesystems = fs;
}

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

vfs_node_t *vfs_resolve_path(const char *path) {
    if (!vfs_root || !path || path[0] != '/') return NULL;

    char temp[256];
    strncpy(temp, path, sizeof(temp));
    temp[sizeof(temp)-1] = '\0'; // Safety null-termination

    vfs_node_t *current = vfs_root;
    char *token = strtok(temp, "/");

    while (token != NULL && current != NULL) {
        if (!(current->flags & VFS_FLAG_DIRECTORY)) {
            return NULL; // Can't descend into non-directory
        }

        if (current->ops && current->ops->finddir) {
            current = current->ops->finddir(current, token);
        } else {
            return NULL;
        }

        token = strtok(NULL, "/");
    }

    return current;
}

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

int vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    if (!node || !node->ops || !node->ops->read) return -1;
    return node->ops->read(node, offset, size, buffer);
}

int vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    if (!node || !node->ops || !node->ops->write) return -1;
    return node->ops->write(node, offset, size, buffer);
}

void vfs_close(vfs_node_t *node) {
    if (!node) return;

    if (node->ops && node->ops->close) {
        node->ops->close(node);
    }

    if (node->refcount > 0) {
        node->refcount--;
    }
}