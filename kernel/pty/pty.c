/*
    pty.c
    PTY (pseudo-terminal) subsystem for Serotonin.

    Provides kernel VTYs (PTY 0-3) with screen buffers and VTY switching, plus dynamically allocated userspace PTYs (PTY 4+).
*/

#include <kernel/pty/pty.h>
#include <kernel/kernel.h>
#include <kernel/stdlib/stdlib.h>
#include <kernel/stdio/stdio.h>
#include <kernel/string.h>
#include <kernel/video/vbe/vbe.h>
#include <kernel/io/io.h>
#include <kernel/filesystem/devfs/devfs.h>
#include <kernel/syscall/sys/file.h>
#include <kernel/syscall/sys/errno.h>

extern uint32_t ansi_fg;
extern uint32_t ansi_bg;
extern uint8_t  ansi_bold;
extern uint32_t scroll_region_top;
extern uint32_t scroll_region_bottom;
extern uint32_t saved_cursor_col;
extern uint32_t saved_cursor_row;
extern uint8_t  cursor_visible;
extern uint8_t  in_alt_screen;
extern uint32_t alt_cursor_col;
extern uint32_t alt_cursor_row;

pty_t pty_table[PTY_MAX];
uint32_t active_vty = 0;

vfs_ops_t pty_slave_ops = {
    .read    = pty_slave_read,
    .write   = pty_slave_write,
    .close   = pty_slave_close,
    .truncate = NULL,
    .unlink  = NULL,
    .rmdir   = NULL,
    .open    = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create  = NULL,
    .mkdir   = NULL
};

vfs_ops_t pty_master_ops = {
    .read    = pty_master_read,
    .write   = pty_master_write,
    .close   = pty_master_close,
    .truncate = NULL,
    .unlink  = NULL,
    .rmdir   = NULL,
    .open    = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create  = NULL,
    .mkdir   = NULL
};

uint32_t pty_ring_write(pty_ring_t *ring, const char *data, uint32_t len) {
    uint32_t space = PTY_RING_SIZE - ring->data_len;
    if (len > space)
        len = space;
    for (uint32_t i = 0; i < len; i++) {
        ring->buf[ring->write_pos] = data[i];
        ring->write_pos = (ring->write_pos + 1) % PTY_RING_SIZE;
    }
    ring->data_len += len;
    return len;
}

uint32_t pty_ring_read(pty_ring_t *ring, char *data, uint32_t len) {
    if (len > ring->data_len)
        len = ring->data_len;
    for (uint32_t i = 0; i < len; i++) {
        data[i] = ring->buf[ring->read_pos];
        ring->read_pos = (ring->read_pos + 1) % PTY_RING_SIZE;
    }
    ring->data_len -= len;
    return len;
}

static int pty_input_waiter_enqueue(pty_input_waiter_t **head, pty_input_waiter_t **tail,
                                     process_control_block_t *task,
                                     uint32_t user_buf, uint32_t buf_size) {
    pty_input_waiter_t *w = (pty_input_waiter_t *)kernel_malloc(sizeof(pty_input_waiter_t));
    if (!w) return -1;
    w->task = task;
    w->next = NULL;
    w->user_buf = user_buf;
    w->buf_size = buf_size;
    if (*tail) {
        (*tail)->next = w;
        *tail = w;
    } else {
        *head = *tail = w;
    }
    return 0;
}

static void pty_input_wake_one(pty_t *pty) {
    pty_input_waiter_t *w = pty->input_waiters_head;
    if (!w) return;

    pty->input_waiters_head = w->next;
    if (!pty->input_waiters_head)
        pty->input_waiters_tail = NULL;

    uint32_t avail = pty->input_ring.data_len;
    uint32_t to_read = w->buf_size < avail ? w->buf_size : avail;

    if (to_read > 0 && w->user_buf) {
        char tmp[256];
        uint32_t done = 0;
        while (done < to_read) {
            uint32_t chunk = to_read - done;
            if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
            pty_ring_read(&pty->input_ring, tmp, chunk);
            copy_to_user(w->task->address_space, w->user_buf + done, tmp, chunk);
            done += chunk;
        }
        w->task->processor_context->eax = to_read;
    } else {
        // woken with no data
        w->task->processor_context->eax = 0;
    }

    task_unblock(w->task);
    kernel_free(w);
}

static void pty_input_wake_all(pty_t *pty) {
    while (pty->input_waiters_head) {
        pty_input_wake_one(pty);
    }
}

static int pty_output_waiter_enqueue(pipe_waiter_t **head, pipe_waiter_t **tail,
                                      process_control_block_t *task) {
    pipe_waiter_t *w = (pipe_waiter_t *)kernel_malloc(sizeof(pipe_waiter_t));
    if (!w) return -1;
    w->task = task;
    w->next = NULL;
    if (*tail) {
        (*tail)->next = w;
        *tail = w;
    } else {
        *head = *tail = w;
    }
    return 0;
}

static void pty_output_wake_one(pipe_waiter_t **head, pipe_waiter_t **tail) {
    if (!*head) return;
    pipe_waiter_t *w = *head;
    *head = w->next;
    if (!*head) *tail = NULL;
    task_unblock(w->task);
    kernel_free(w);
}

static void pty_output_wake_all(pipe_waiter_t **head, pipe_waiter_t **tail) {
    while (*head) {
        pty_output_wake_one(head, tail);
    }
}

void pty_save_term_state(term_state_t *ts) {
    ts->cursor_col          = term_cursor_col;
    ts->cursor_row          = term_cursor_row;
    ts->fg_color            = term_fg_color;
    ts->bg_color            = term_bg_color;
    ts->ansi_fg             = ansi_fg;
    ts->ansi_bg             = ansi_bg;
    ts->ansi_bold           = ansi_bold;
    ts->scroll_region_top   = scroll_region_top;
    ts->scroll_region_bottom = scroll_region_bottom;
    ts->saved_cursor_col    = saved_cursor_col;
    ts->saved_cursor_row    = saved_cursor_row;
    ts->cursor_visible      = cursor_visible;
    ts->in_alt_screen       = in_alt_screen;
    ts->alt_cursor_col      = alt_cursor_col;
    ts->alt_cursor_row      = alt_cursor_row;
}

void pty_restore_term_state(const term_state_t *ts) {
    term_cursor_col         = ts->cursor_col;
    term_cursor_row         = ts->cursor_row;
    term_fg_color           = ts->fg_color;
    term_bg_color           = ts->bg_color;
    ansi_fg                 = ts->ansi_fg;
    ansi_bg                 = ts->ansi_bg;
    ansi_bold               = ts->ansi_bold;
    scroll_region_top       = ts->scroll_region_top;
    scroll_region_bottom    = ts->scroll_region_bottom;
    saved_cursor_col        = ts->saved_cursor_col;
    saved_cursor_row        = ts->saved_cursor_row;
    cursor_visible          = ts->cursor_visible;
    in_alt_screen           = ts->in_alt_screen;
    alt_cursor_col          = ts->alt_cursor_col;
    alt_cursor_row          = ts->alt_cursor_row;
}

static void pty_default_attr(pty_attr_t *attr) {
    attr->echo     = 1;
    attr->icanon   = 1;
    attr->isig     = 1;
    attr->onlcr    = 1;
    attr->cc_vintr  = 3;   // ctrl+c
    attr->cc_veof   = 4;   // dtrl+d
    attr->cc_verase = 8;   // backspace
    attr->cc_vkill  = 21;  // ctrl+u
}

static void pty_default_term_state(term_state_t *ts) {
    memset(ts, 0, sizeof(*ts));
    ts->fg_color       = 0xFFFFFFFF;
    ts->bg_color       = 0xFF000000;
    ts->ansi_fg        = 0xFFFFFFFF;
    ts->ansi_bg        = 0xFF000000;
    ts->cursor_visible = 1;
}

pty_t *pty_from_node(vfs_node_t *node) {
    if (!node || !node->fs_data) return NULL;

    if (node->ops == &pty_slave_ops || node->ops == &pty_master_ops) {
        return (pty_t *)node->fs_data;
    }

    devfs_file_t *file = (devfs_file_t *)node->fs_data;
    return (pty_t *)file->device_data;
}

static void pty_register_devfs(pty_t *pty) {
    char path[32];
    snprintf(path, sizeof(path), "pts/%d", pty->id);
    devfs_register_device(path, S_IFCHR | 0620, &pty_slave_ops, pty);
}

static vfs_node_t *pty_create_slave_node(pty_t *pty) {
    vfs_node_t *node = (vfs_node_t *)kernel_malloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(*node));
    snprintf(node->name, sizeof(node->name), "pts%d", pty->id);
    node->flags   = VFS_FLAG_FILE;
    node->ops     = &pty_slave_ops;
    node->fs_data = pty;
    node->mode    = S_IFCHR | 0620;
    return node;
}

static vfs_node_t *pty_create_master_node(pty_t *pty) {
    vfs_node_t *node = (vfs_node_t *)kernel_malloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(*node));
    snprintf(node->name, sizeof(node->name), "ptm%d", pty->id);
    node->flags   = VFS_FLAG_FILE;
    node->ops     = &pty_master_ops;
    node->fs_data = pty;
    node->mode    = S_IFCHR | 0600;
    return node;
}

void pty_init(void) {
    memset(pty_table, 0, sizeof(pty_table));

    devfs_register_device("pts", S_IFDIR | 0755, NULL, NULL);

    extern uint32_t fb_size_bytes;

    for (uint32_t i = 0; i < NUM_KERNEL_VTYS; i++) {
        pty_t *pty = &pty_table[i];
        pty->id    = (uint8_t)i;
        pty->flags = PTY_FLAG_KERNEL_VTY | PTY_FLAG_ALLOCATED;
        pty_default_attr(&pty->attr);

        if (i == 0) {
            // VTY 0: use existing layer 0 buffer
            pty->screen_buf = vbe_get_layer0_bufptr();
            pty_save_term_state(&pty->term_state);
        } else {
            // VTYs 1-3: alloc new screen buffers
            pty->screen_buf = (uint32_t *)kernel_malloc_align(16, fb_size_bytes);
            if (!pty->screen_buf)
                kernel_panic("pty_init: out of memory for VTY screen buffer");
            memset(pty->screen_buf, 0, fb_size_bytes);
            pty_default_term_state(&pty->term_state);
        }

        pty->slave_node = pty_create_slave_node(pty);
        pty->slave_refcount = 1;
        pty->foreground_pid = -1;

        pty->winsize.ws_col = (uint16_t)(vbe_info.width / VBE_FONT_WIDTH);
        pty->winsize.ws_row = (uint16_t)(vbe_info.height / VBE_FONT_HEIGHT);
        pty->winsize.ws_xpixel = vbe_info.width;
        pty->winsize.ws_ypixel = vbe_info.height;

        pty_register_devfs(pty);
    }

    active_vty = 0;
    printfs(PRINT_STATUS_INFO, "pty: initialized %d kernel VTYs\n", NUM_KERNEL_VTYS);
}

pty_t *pty_alloc(void) {
    for (uint32_t i = NUM_KERNEL_VTYS; i < PTY_MAX; i++) {
        if (!(pty_table[i].flags & PTY_FLAG_ALLOCATED)) {
            pty_t *pty = &pty_table[i];
            memset(pty, 0, sizeof(*pty));
            pty->id    = (uint8_t)i;
            pty->flags = PTY_FLAG_ALLOCATED;
            pty_default_attr(&pty->attr);
            pty_default_term_state(&pty->term_state);

            pty->slave_node  = pty_create_slave_node(pty);
            pty->master_node = pty_create_master_node(pty);
            pty->slave_refcount  = 1;
            pty->master_refcount = 1;

            pty->winsize.ws_col = (uint16_t)(vbe_info.width / VBE_FONT_WIDTH);
            pty->winsize.ws_row = (uint16_t)(vbe_info.height / VBE_FONT_HEIGHT);

            pty_register_devfs(pty);
            return pty;
        }
    }
    return NULL;
}

void pty_free(pty_t *pty) {
    if (!pty || !(pty->flags & PTY_FLAG_ALLOCATED))
        return;
    if (pty->flags & PTY_FLAG_KERNEL_VTY)
        return; // never free kernel VTYs

    // wake all waiters
    lock_scheduler();
    pty_input_wake_all(pty);
    pty_output_wake_all(&pty->output_waiters_head, &pty->output_waiters_tail);
    unlock_scheduler();

    pty->flags = 0;
}

static void pty_ldisc_flush_line(pty_t *pty) {
    lock_scheduler();
    // cppcheck-suppress unreadVariable
    uint32_t wrote = pty_ring_write(&pty->input_ring, pty->line_buf, pty->line_len);
    pty->line_len = 0;
    pty_input_wake_one(pty);
    unlock_scheduler();
}

void pty_ldisc_input(pty_t *pty, char c) {
    // signal generation
    if (pty->attr.isig && c == pty->attr.cc_vintr) {
        // echo ^C
        if (pty->attr.echo) {
            pty_render_to_vty(pty, "^C\n", 3);
        }
        // send SIGINT to foreground process
        if (pty->foreground_pid > 0) {
            process_control_block_t *fg = task_lookup_by_pid(pty->foreground_pid);
            if (fg) {
                task_ipc_signal_raise(fg, EXIT_SIGINT);
            }
        }
        // flush line buffer and wake readers with empty data
        pty->line_len = 0;
        lock_scheduler();
        pty_input_wake_all(pty);
        unlock_scheduler();
        return;
    }

    // EOF (Ctrl+D)
    if (pty->attr.icanon && c == pty->attr.cc_veof) {
        // flush whatever is in the line buffer
        pty_ldisc_flush_line(pty);
        return;
    }

    // cooked mode
    if (pty->attr.icanon) {
        if (c == pty->attr.cc_verase || c == '\b' || c == 0x7F) {
            if (pty->line_len > 0) {
                pty->line_len--;
                if (pty->attr.echo) {
                    if (pty->flags & PTY_FLAG_KERNEL_VTY) {
                        pty_render_to_vty(pty, "\b \b", 3);
                    } else {
                        pty_ring_write(&pty->output_ring, "\b \b", 3);
                        lock_scheduler();
                        pty_output_wake_one(&pty->output_waiters_head, &pty->output_waiters_tail);
                        unlock_scheduler();
                    }
                }
            }
            return;
        }

        if (c == pty->attr.cc_vkill) {
            while (pty->line_len > 0) {
                pty->line_len--;
                if (pty->attr.echo) {
                    if (pty->flags & PTY_FLAG_KERNEL_VTY) {
                        pty_render_to_vty(pty, "\b \b", 3);
                    } else {
                        pty_ring_write(&pty->output_ring, "\b \b", 3);
                    }
                }
            }
            if (pty->attr.echo && !(pty->flags & PTY_FLAG_KERNEL_VTY)) {
                lock_scheduler();
                pty_output_wake_one(&pty->output_waiters_head, &pty->output_waiters_tail);
                unlock_scheduler();
            }
            return;
        }

        if (c == '\n' || c == '\r') {
            if (pty->line_len < PTY_LINE_SIZE)
                pty->line_buf[pty->line_len++] = '\n';
            if (pty->attr.echo) {
                if (pty->flags & PTY_FLAG_KERNEL_VTY) {
                    pty_render_to_vty(pty, "\n", 1);
                } else {
                    // cppcheck-suppress constVariable
                    char crlf[2] = {'\r', '\n'};
                    pty_ring_write(&pty->output_ring, crlf, 2);
                    lock_scheduler();
                    pty_output_wake_one(&pty->output_waiters_head, &pty->output_waiters_tail);
                    unlock_scheduler();
                }
            }
            pty_ldisc_flush_line(pty);
            return;
        }

        if (pty->line_len < PTY_LINE_SIZE) {
            pty->line_buf[pty->line_len++] = c;
            if (pty->attr.echo) {
                if (pty->flags & PTY_FLAG_KERNEL_VTY) {
                    pty_render_to_vty(pty, &c, 1);
                } else {
                    pty_ring_write(&pty->output_ring, &c, 1);
                    lock_scheduler();
                    pty_output_wake_one(&pty->output_waiters_head, &pty->output_waiters_tail);
                    unlock_scheduler();
                }
            }
        }
    } else {
        // raw mode
        lock_scheduler();
        pty_ring_write(&pty->input_ring, &c, 1);
        pty_input_wake_one(pty);
        unlock_scheduler();

        if (pty->attr.echo) {
            if (pty->flags & PTY_FLAG_KERNEL_VTY) {
                pty_render_to_vty(pty, &c, 1);
            } else {
                pty_ring_write(&pty->output_ring, &c, 1);
                lock_scheduler();
                pty_output_wake_one(&pty->output_waiters_head, &pty->output_waiters_tail);
                unlock_scheduler();
            }
        }
    }
}

uint32_t pty_ldisc_output(pty_t *pty, const char *buf, uint32_t len) {
    if (pty->flags & PTY_FLAG_KERNEL_VTY) {
        if (!pty->attr.onlcr) {
            pty_render_to_vty(pty, buf, len);
            return len;
        }
        uint32_t start = 0;
        for (uint32_t i = 0; i < len; i++) {
            if (buf[i] == '\n') {
                if (i > start) pty_render_to_vty(pty, buf + start, i - start);
                pty_render_to_vty(pty, "\r\n", 2);
                start = i + 1;
            }
        }
        if (start < len) pty_render_to_vty(pty, buf + start, len - start);
        return len;
    }
    uint32_t consumed = 0;
    lock_scheduler();

    if (!pty->attr.onlcr) {
        consumed = pty_ring_write(&pty->output_ring, buf, len);
    } else {
        for (uint32_t i = 0; i < len; i++) {
            uint32_t space = PTY_RING_SIZE - pty->output_ring.data_len;
            if (buf[i] == '\n') {
                if (space < 2) break;
                pty_ring_write(&pty->output_ring, "\r\n", 2);
            } else {
                if (space < 1) break;
                pty_ring_write(&pty->output_ring, &buf[i], 1);
            }
            consumed++;
        }
    }

    if (consumed > 0)
        pty_output_wake_one(&pty->output_waiters_head, &pty->output_waiters_tail);

    unlock_scheduler();
    return consumed;
}

void pty_render_to_vty(pty_t *pty, const char *buf, uint32_t len) {
    if (!(pty->flags & PTY_FLAG_KERNEL_VTY))
        return;

    if (pty == &pty_table[active_vty]) {
        vbe_terminal_puts(buf, (int)len);
    } else {
        clear_interrupts();
        uint32_t *saved_bufptr = vbe_get_layer0_bufptr();
        vbe_set_layer0_bufptr(pty->screen_buf);
        vbe_terminal_puts_ctx(&pty->term_state, buf, (int)len);
        vbe_set_layer0_bufptr(saved_bufptr);
        enable_interrupts();
    }
}

void pty_switch_vty(uint32_t id) {
    if (id >= NUM_KERNEL_VTYS || id == active_vty)
        return;

    pty_t *old_pty = &pty_table[active_vty];
    pty_t *new_pty = &pty_table[id];

    pty_save_term_state(&old_pty->term_state);
    pty_restore_term_state(&new_pty->term_state);
    vbe_set_layer0_bufptr(new_pty->screen_buf);

    active_vty = id;
}

int pty_slave_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    pty_t *pty = pty_from_node(node);
    if (!pty)
        return -EBADF;

    lock_scheduler();

    if (pty->input_ring.data_len > 0) {
        uint32_t to_read = pty_ring_read(&pty->input_ring, buffer, size);
        unlock_scheduler();
        return (int)to_read;
    }

    if (!(pty->flags & PTY_FLAG_KERNEL_VTY) && pty->master_refcount == 0) {
        unlock_scheduler();
        return 0;
    }

    uint32_t user_buf = current_task->current_user_buf;
    if (pty_input_waiter_enqueue(&pty->input_waiters_head,
                                  &pty->input_waiters_tail,
                                  current_task, user_buf, size) != 0) {
        unlock_scheduler();
        return -ENOMEM;
    }
    current_task->state = PROCESS_STATE_BLOCKED;
    unlock_scheduler();
    task_yield(1);
    __builtin_unreachable();
}

int pty_slave_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    pty_t *pty = pty_from_node(node);
    if (!pty)
        return -EBADF;

    return (int)pty_ldisc_output(pty, buffer, size);
}

int pty_slave_close(vfs_node_t *node) {
    if (!node) return 0;
    pty_t *pty = pty_from_node(node);
    if (!pty) return 0;

    if (pty->slave_refcount > 0)
        pty->slave_refcount--;

    if (pty->slave_refcount == 0 && pty->master_refcount == 0 &&
        !(pty->flags & PTY_FLAG_KERNEL_VTY)) {
        pty_free(pty);
    }
    return 0;
}

int pty_master_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    pty_t *pty = pty_from_node(node);
    if (!pty)
        return -EBADF;

    lock_scheduler();

    if (pty->output_ring.data_len > 0) {
        uint32_t to_read = pty_ring_read(&pty->output_ring, buffer, size);
        unlock_scheduler();
        return (int)to_read;
    }

    if (pty->slave_refcount == 0) {
        unlock_scheduler();
        return 0;
    }

    if (pty_output_waiter_enqueue(&pty->output_waiters_head,
                                   &pty->output_waiters_tail,
                                   current_task) != 0) {
        unlock_scheduler();
        return -ENOMEM;
    }
    current_task->state = PROCESS_STATE_BLOCKED;
    unlock_scheduler();
    task_yield(1);
    __builtin_unreachable();
}

int pty_master_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    pty_t *pty = pty_from_node(node);
    if (!pty)
        return -EBADF;

    for (uint32_t i = 0; i < size; i++) {
        pty_ldisc_input(pty, buffer[i]);
    }
    return (int)size;
}

int pty_master_close(vfs_node_t *node) {
    if (!node) return 0;
    pty_t *pty = pty_from_node(node);
    if (!pty) return 0;

    if (pty->master_refcount > 0)
        pty->master_refcount--;

    if (pty->foreground_pid > 0) {
        process_control_block_t *fg = task_lookup_by_pid(pty->foreground_pid);
        if (fg) {
            task_ipc_signal_raise(fg, EXIT_SIGHUP);
        }
    }

    lock_scheduler();
    pty_input_wake_all(pty);
    unlock_scheduler();

    if (pty->slave_refcount == 0 && pty->master_refcount == 0) {
        pty_free(pty);
    }
    return 0;
}
