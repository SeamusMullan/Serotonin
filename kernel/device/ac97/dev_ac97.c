#include "dev_ac97.h"
#include "ac97.h"
#include "../../kernel.h"
#include "../../filesystem/devfs/devfs.h"
#include "../../filesystem/vfs.h"
#include "../../io/io.h"
#include "../../stdio/stdio.h"
#include "../../stdlib/stdlib.h"
#include "../../string.h"
#include "../../syscall/sys/file.h"
#include "../../syscall/sys/errno.h"

static int dev_ac97_write_audio(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    (void)node;
    (void)offset;

    if (!buffer || size == 0) return -EINVAL;
    if (!ac97_is_up()) return -ENODEV;

    int ret = ac97_write_pcm(buffer, size);
    if (ret < 0) return -EIO;
    return ret;
}

static int dev_ac97_poll_audio(vfs_node_t *node) {
    (void)node;
    return POLLOUT;
}

static int dev_ac97_read_config(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)node;

    if (!ac97_is_up()) return -ENODEV;

    ac97_config_t cfg;
    ac97_read_config(&cfg);

    uint32_t cfg_size = sizeof(ac97_config_t);
    if (offset >= cfg_size) return 0;

    uint32_t remaining = cfg_size - offset;
    uint32_t count = size < remaining ? size : remaining;
    memcpy(buffer, (const char *)&cfg + offset, count);
    return (int)count;
}

static int dev_ac97_write_config(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    (void)node;
    (void)offset;

    if (!buffer) return -EINVAL;
    if (!ac97_is_up()) return -ENODEV;
    if (size < sizeof(ac97_config_t)) return -EINVAL;

    const ac97_config_t *cfg = (const ac97_config_t *)buffer;
    int ret = ac97_apply_config(cfg);
    if (ret < 0) return -EIO;

    return (int)sizeof(ac97_config_t);
}

static vfs_ops_t dev_ac97_audio_ops = {
    .read    = NULL,
    .write   = dev_ac97_write_audio,
    .truncate = NULL, .unlink = NULL, .rmdir = NULL,
    .open = NULL, .close = NULL,
    .readdir = NULL, .finddir = NULL,
    .create = NULL, .mkdir = NULL,
    .poll = dev_ac97_poll_audio
};

static vfs_ops_t dev_ac97_config_ops = {
    .read    = dev_ac97_read_config,
    .write   = dev_ac97_write_config,
    .truncate = NULL, .unlink = NULL, .rmdir = NULL,
    .open = NULL, .close = NULL,
    .readdir = NULL, .finddir = NULL,
    .create = NULL, .mkdir = NULL,
    .poll = NULL
};

void dev_ac97_init(void) {
    devfs_register_device("ac97/audio", S_IFREG | 0666, &dev_ac97_audio_ops, NULL);
    devfs_register_device("ac97/config", S_IFREG | 0666, &dev_ac97_config_ops, NULL);
}
