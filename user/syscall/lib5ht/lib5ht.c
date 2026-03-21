/**
 * @file lib5ht.c
 * @brief 5HT library implementation
 *
 * Implements the Serotonin-specific system call wrappers for
 * process listing and framebuffer layer management.
 */

#include "lib5ht.h"
#include "../syscall_table.h"
#include <unistd.h>

/** @copydoc sys_5ht_list_processes */
int sys_5ht_list_processes(proc_5ht_t *buf, size_t max) {
    return do_syscall(SYSTEM_CALL_5HT_LIST_PROC, (uint32_t)buf, (uint32_t)max, 0);
}

/** @copydoc sys_5ht_req_buf */
int sys_5ht_req_buf(uint16_t id, const fb_layer_config_t *cfg, fb_layer_info_t *out) {
    return do_syscall(SYSTEM_CALL_5HT_REQ_BUF, (uint32_t)id, (uint32_t)cfg, (uint32_t)out);
}

/** @copydoc sys_5ht_rel_buf */
int sys_5ht_rel_buf(uint16_t id) {
    return do_syscall(SYSTEM_CALL_5HT_REL_BUF, (uint32_t)id, 0, 0);
}

/** @copydoc sys_5ht_rcfg_layer */
int sys_5ht_rcfg_layer(uint16_t id, const fb_layer_config_t *cfg, fb_layer_info_t *out) {
    return do_syscall(SYSTEM_CALL_5HT_RCFG_LAYER, (uint32_t)id, (uint32_t)cfg, (uint32_t)out);
}

/** @copydoc sys_5ht_query_info */
int sys_5ht_query_info(fb_info_t *out) {
    return do_syscall(SYSTEM_CALL_5HT_QUERY_INFO, (uint32_t)out, 0, 0);
}

/** @copydoc sys_5ht_query_layer */
int sys_5ht_query_layer(uint16_t id, fb_layer_info_t *out) {
    return do_syscall(SYSTEM_CALL_5HT_QUERY_LAYER, (uint32_t)id, (uint32_t)out, 0);
}

int sys_5ht_set_fid(pid_t pid) {
    return do_syscall(SYSTEM_CALL_5HT_SET_FID, (uint32_t)pid, 0, 0);
}

int sys_5ht_pty_open(int fds[2]) {
    return do_syscall(SYSTEM_CALL_5HT_PTY_OPEN, (uint32_t)fds, 0, 0);
}

int sys_5ht_pty_setattr(int fd, const pty_attr_t *attr) {
    return do_syscall(SYSTEM_CALL_5HT_PTY_SETATTR, (uint32_t)fd, (uint32_t)attr, 0);
}

int sys_5ht_pty_getattr(int fd, pty_attr_t *attr) {
    return do_syscall(SYSTEM_CALL_5HT_PTY_GETATTR, (uint32_t)fd, (uint32_t)attr, 0);
}

int sys_5ht_pty_winsize(int fd, pty_winsize_t *ws, int get) {
    return do_syscall(SYSTEM_CALL_5HT_PTY_WINSIZE, (uint32_t)fd, (uint32_t)ws, (uint32_t)get);
}

int sys_5ht_pty_setpgrp(int fd) {
    return do_syscall(SYSTEM_CALL_5HT_PTY_SETPGRP, (uint32_t)fd, 0, 0);
}

int sys_5ht_grab_input(int grab) {
    return do_syscall(SYSTEM_CALL_5HT_GRAB_INPUT, (uint32_t)grab, 0, 0);
}

int ioctl(int fd, unsigned long request, void *arg) {
    switch (request) {
        case TCGETS:
            return sys_5ht_pty_getattr(fd, (pty_attr_t *)arg);
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            return sys_5ht_pty_setattr(fd, (const pty_attr_t *)arg);
        case TIOCGWINSZ:
            return sys_5ht_pty_winsize(fd, (pty_winsize_t *)arg, 1);
        case TIOCSWINSZ:
            return sys_5ht_pty_winsize(fd, (pty_winsize_t *)arg, 0);
        case TIOCSPGRP:
            return sys_5ht_pty_setpgrp(fd);
        case TIOCGPGRP:
            /* Not directly supported — return ENOSYS */
            errno = ENOSYS;
            return -1;
        default:
            errno = ENOSYS;
            return -1;
    }
}
