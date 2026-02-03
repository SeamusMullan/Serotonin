#include "devfs_example.h"
#include "../filesystem/devfs/devfs.h"
#include "../filesystem/vfs.h"
#include "../kernel.h"
#include "../stdlib/stdlib.h"
#include "../string.h"
#include "../stdio/stdio.h"
#include "../syscall/sys/file.h"

static const char devfs_number_value[] = "67\n";

static int devfs_example_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)node;
    uint32_t len = (uint32_t)strlen(devfs_number_value);
    if (offset >= len) return 0;

    uint32_t remaining = len - offset;
    uint32_t to_copy = size < remaining ? size : remaining;
    memcpy(buffer, devfs_number_value + offset, to_copy);
    return (int)to_copy;
}

static int devfs_example_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    (void)node;
    (void)offset;
    if (!buffer || size == 0) return 0;

    char *tmp = kernel_malloc(size + 1);
    if (!tmp) return -1;
    memcpy(tmp, buffer, size);
    tmp[size] = '\0';

    printfs(PRINT_STATUS_INFO, "devfs_example write: %s\n", tmp);
    kernel_free(tmp);
    return (int)size;
}

static vfs_ops_t devfs_example_number_ops = {
    .read = devfs_example_read,
    .write = NULL,
    .truncate = NULL,
    .unlink = NULL,
    .rmdir = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .mkdir = NULL
};

static vfs_ops_t devfs_example_write_ops = {
    .read = NULL,
    .write = devfs_example_write,
    .truncate = NULL,
    .unlink = NULL,
    .rmdir = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .mkdir = NULL
};

void devfs_example_init(void) {
    devfs_register_device("example/number", S_IFREG | 0400, &devfs_example_number_ops);
    devfs_register_device("example/write", S_IFREG | 0200, &devfs_example_write_ops);
}
