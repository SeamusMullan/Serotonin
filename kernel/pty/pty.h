#ifndef _KERNEL_PTY_H
#define _KERNEL_PTY_H

#include <stdint.h>
#include <kernel/schedule/schedule.h>
#include <kernel/filesystem/vfs.h>
#include <kernel/filesystem/user_fs/user_fs.h>

#define PTY_RING_SIZE    65536
#define PTY_LINE_SIZE    4096
#define PTY_MAX          16
#define NUM_KERNEL_VTYS  4

#define PTY_FLAG_KERNEL_VTY  (1 << 0)
#define PTY_FLAG_ALLOCATED   (1 << 1)

typedef struct term_state {
    uint32_t cursor_col;
    uint32_t cursor_row;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t ansi_fg;
    uint32_t ansi_bg;
    uint8_t  ansi_bold;
    uint32_t scroll_region_top;
    uint32_t scroll_region_bottom;
    uint32_t saved_cursor_col;
    uint32_t saved_cursor_row;
    uint8_t  cursor_visible;
    uint8_t  in_alt_screen;
    uint32_t alt_cursor_col;
    uint32_t alt_cursor_row;
} term_state_t;

/* Line discipline attributes */
typedef struct pty_attr {
    uint8_t echo;        /* ECHO */
    uint8_t icanon;      /* canonical mode (line buffered) */
    uint8_t isig;        /* signal generation (Ctrl+C etc) */
    uint8_t onlcr;       /* NL -> CR NL on output */
    char cc_vintr;       /* interrupt char (default Ctrl+C = 3) */
    char cc_veof;        /* EOF char (default Ctrl+D = 4) */
    char cc_verase;      /* erase char (default backspace = 8) */
    char cc_vkill;       /* kill line char (default Ctrl+U = 21) */
} pty_attr_t;

/* Ring buffer for data transfer */
typedef struct pty_ring {
    char buf[PTY_RING_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t data_len;
} pty_ring_t;

/* Window size */
typedef struct pty_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} pty_winsize_t;

typedef struct pty_input_waiter {
    process_control_block_t *task;
    struct pty_input_waiter *next;
    uint32_t user_buf;          /* user-space destination address */
    uint32_t buf_size;          /* requested read size */
} pty_input_waiter_t;

/* Full PTY structure */
typedef struct pty {
    uint8_t flags;
    uint8_t id;

    pty_attr_t attr;
    pty_ring_t input_ring;     /* data for slave readers (process stdin) */
    pty_ring_t output_ring;    /* data for master readers (userspace PTYs only) */

    char line_buf[PTY_LINE_SIZE];
    uint32_t line_len;

    uint32_t *screen_buf;      /* per-VTY framebuffer (NULL for userspace PTYs) */
    term_state_t term_state;

    int foreground_pid;

    vfs_node_t *master_node;
    vfs_node_t *slave_node;

    /* input waiter queue - uses pty_input_waiter_t for direct delivery */
    pty_input_waiter_t *input_waiters_head;
    pty_input_waiter_t *input_waiters_tail;
    /* output waiter queue (master readers for userspace PTYs) */
    pipe_waiter_t *output_waiters_head;
    pipe_waiter_t *output_waiters_tail;
    uint32_t master_refcount;
    uint32_t slave_refcount;

    pty_winsize_t winsize;
} pty_t;

/* Global PTY table */
extern pty_t pty_table[PTY_MAX];
extern uint32_t active_vty;

/* VFS ops for PTY slave and master */
extern vfs_ops_t pty_slave_ops;
extern vfs_ops_t pty_master_ops;

/* Core PTY functions */
void pty_init(void);
pty_t *pty_alloc(void);
void pty_free(pty_t *pty);

/* Extract pty_t* from a VFS node (handles both direct and devfs nodes) */
pty_t *pty_from_node(vfs_node_t *node);

/* Slave side VFS ops */
int pty_slave_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int pty_slave_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
int pty_slave_close(vfs_node_t *node);

/* Master side VFS ops (userspace PTYs) */
int pty_master_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
int pty_master_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
int pty_master_close(vfs_node_t *node);

/* Line discipline */
void pty_ldisc_input(pty_t *pty, char c);
uint32_t pty_ldisc_output(pty_t *pty, const char *buf, uint32_t len);

/* VTY management */
void pty_render_to_vty(pty_t *pty, const char *buf, uint32_t len);
void pty_switch_vty(uint32_t id);
void pty_save_term_state(term_state_t *ts);
void pty_restore_term_state(const term_state_t *ts);

/* Ring buffer helpers */
uint32_t pty_ring_write(pty_ring_t *ring, const char *data, uint32_t len);
uint32_t pty_ring_read(pty_ring_t *ring, char *data, uint32_t len);

#endif
