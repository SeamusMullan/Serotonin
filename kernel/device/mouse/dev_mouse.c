#include "dev_mouse.h"
#include "../../filesystem/devfs/devfs.h"
#include "../../filesystem/vfs.h"
#include "../../io/io.h"
#include "../../stdio/stdio.h"
#include "../../video/vbe/vbe.h"
#include "../../stdlib/stdlib.h"
#include "../../string.h"
#include "../../syscall/sys/file.h"

static vfs_ops_t dev_mouse_read_pos_ops = {
    .read = dev_mouse_read_pos,
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

int dev_mouse_read_pos(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)node;
    char mouse_pos[32];
    snprintf(mouse_pos, 31,"%d,%d",mouse_x,mouse_y);
    mouse_pos[31] = '\0';

    uint32_t len = (uint32_t)strlen(mouse_pos);
    if (offset >= len) return 0;

    uint32_t remaining = len - offset;
    uint32_t to_copy = size < remaining ? size : remaining;
    memcpy(buffer, mouse_pos + offset, to_copy);
    return (int)to_copy;
}


void dev_mouse_init(void) {
    devfs_register_device("mouse/pos", S_IFREG | 0400, &dev_mouse_read_pos_ops);
}
