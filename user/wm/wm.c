#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include "wm.h"
#include "../syscall/sys/poll.h"

int waitpid(pid_t pid, int *status);
int snprintf(char *str, size_t size, const char *fmt, ...);
int listdir(const char *path, char *buf, size_t size);

/* --- Cursor bitmap (16x16 arrow) --- */
static const uint8_t cursor_bitmap[16][16] = {
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,1,1,1,0,0,0,0,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
};

#define CURSOR_W 16
#define CURSOR_H 16

static wm_state_t wm;
static uint32_t blink_counter = 0;
static uint8_t  cursor_blink_on = 1;
#define BLINK_INTERVAL 15  /* poll iterations (~16ms * 15 = ~250ms) */

/* --- palette init --- */

void wm_init_palette(wm_state_t *wm) {
    /* 0-15: standard colors */
    for (int i = 0; i < 16; i++)
        wm->palette[i] = vbe_base_colors[i];
    /* 16-231: 6x6x6 cube */
    int idx = 16;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                uint8_t rr = (r == 0) ? 0 : 55 + r * 40;
                uint8_t gg = (g == 0) ? 0 : 55 + g * 40;
                uint8_t bb = (b == 0) ? 0 : 55 + b * 40;
                wm->palette[idx++] = ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
            }
    /* 232-255: grayscale */
    for (int i = 0; i < 24; i++) {
        uint8_t lv = 8 + i * 10;
        wm->palette[idx++] = ((uint32_t)lv << 16) | ((uint32_t)lv << 8) | lv;
    }
}

/* --- cursor layer --- */

static void init_cursor_layer(wm_state_t *wm) {
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = 0; cfg.y0 = 0;
    cfg.x1 = CURSOR_W; cfg.y1 = CURSOR_H;
    cfg.alpha = 1;
    cfg.stride = CURSOR_W * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(LAYER_CURSOR, &cfg, &info) != 0) return;

    wm->cursor_layer = LAYER_CURSOR;
    wm->cursor_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    wm->cursor_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;

    /* draw cursor bitmap */
    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {
            uint32_t color;
            switch (cursor_bitmap[y][x]) {
            case 1: color = 0xFF000000; break; /* black border */
            case 2: color = 0xFFFFFFFF; break; /* white fill */
            default: color = 0x00000000; break; /* transparent */
            }
            wm->cursor_fb[y * CURSOR_W + x] = color;
        }
    }

    /* submit initial frame */
    wm->cursor_meta->dx0 = 0; wm->cursor_meta->dy0 = 0;
    wm->cursor_meta->dx1 = CURSOR_W; wm->cursor_meta->dy1 = CURSOR_H;
    wm->cursor_meta->frame_id = 1;
    wm->cursor_meta->ready = 1;
}

static void move_cursor(wm_state_t *wm, int x, int y) {
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = (uint16_t)x;
    cfg.y0 = (uint16_t)y;
    cfg.x1 = (uint16_t)(x + CURSOR_W);
    cfg.y1 = (uint16_t)(y + CURSOR_H);
    cfg.alpha = 1;
    cfg.stride = CURSOR_W * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_rcfg_layer(LAYER_CURSOR, &cfg, &info) != 0) return;

    /* update pointers in case kernel remapped */
    wm->cursor_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    wm->cursor_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;

    /* mark dirty so compositor redraws at new position */
    wm->cursor_meta->dx0 = 0; wm->cursor_meta->dy0 = 0;
    wm->cursor_meta->dx1 = CURSOR_W; wm->cursor_meta->dy1 = CURSOR_H;
    wm->cursor_meta->frame_id++;
    wm->cursor_meta->ready = 1;
}

/* --- taskbar --- */

static void init_taskbar(wm_state_t *wm) {
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = 0;
    cfg.y0 = SCREEN_H - TASKBAR_H;
    cfg.x1 = SCREEN_W;
    cfg.y1 = SCREEN_H;
    cfg.alpha = 0;
    cfg.stride = SCREEN_W * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(LAYER_TASKBAR, &cfg, &info) != 0) return;

    wm->taskbar_layer = LAYER_TASKBAR;
    wm->taskbar_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    wm->taskbar_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
}

static void render_taskbar(wm_state_t *wm) {
    if (!wm->taskbar_fb) return;

    uint32_t stride = SCREEN_W;
    /* background */
    draw_fill_rect(wm->taskbar_fb, stride, 0, 0, SCREEN_W, TASKBAR_H, THEME_BG_MEDIUM);

    /* top border line */
    draw_fill_rect(wm->taskbar_fb, stride, 0, 0, SCREEN_W, 1, THEME_BORDER);

    /* window buttons */
    int btn_x = 4;
    int btn_y = (TASKBAR_H - FONT_H) / 2 + 1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wm->windows[i].active) continue;

        uint32_t bg = (i == wm->focused_idx) ? THEME_ACCENT : THEME_BG_DARK;
        uint32_t fg = (i == wm->focused_idx) ? 0xFF000000 : THEME_TEXT_DIM;

        int btn_w = 120;
        draw_fill_rect(wm->taskbar_fb, stride, btn_x, 2, btn_w, TASKBAR_H - 3, bg);

        /* title text (truncated) */
        char label[16];
        strncpy(label, wm->windows[i].title, 14);
        label[14] = '\0';
        draw_text(wm->taskbar_fb, stride, btn_x + 4, btn_y, label, fg, bg);

        btn_x += btn_w + 4;
    }

    /* right side: "Serotonin" label */
    const char *brand = "Serotonin";
    int brand_x = SCREEN_W - (int)strlen(brand) * FONT_W - 8;
    draw_text(wm->taskbar_fb, stride, brand_x, btn_y, brand, THEME_ACCENT, THEME_BG_MEDIUM);

    /* submit frame */
    wm->taskbar_meta->dx0 = 0; wm->taskbar_meta->dy0 = 0;
    wm->taskbar_meta->dx1 = SCREEN_W; wm->taskbar_meta->dy1 = TASKBAR_H;
    wm->taskbar_meta->frame_id++;
    wm->taskbar_meta->ready = 1;
}

/* --- window decorations --- */

void wm_render_decorations(wm_state_t *wm, int idx) {
    wm_window_t *win = &wm->windows[idx];
    if (!win->active || !win->fb) return;

    uint32_t stride = win->fb_stride_px;
    uint32_t tb_bg = win->focused ? THEME_TITLEBAR_BG : THEME_TITLEBAR_INACTIVE;
    uint32_t border = win->focused ? THEME_BORDER_ACTIVE : THEME_BORDER_INACTIVE;

    /* title bar */
    draw_fill_rect(win->fb, stride, 0, 0, win->w, TITLEBAR_H, tb_bg);

    /* title text */
    int text_x = BORDER_W + 4;
    int text_y = (TITLEBAR_H - FONT_H) / 2;
    draw_text(win->fb, stride, text_x, text_y, win->title,
              THEME_TITLEBAR_FG, tb_bg);

    /* close button */
    int close_x = win->w - BORDER_W - CLOSE_BTN_W;
    int close_y = (TITLEBAR_H - CLOSE_BTN_H) / 2;
    draw_fill_rect(win->fb, stride, close_x, close_y,
                   CLOSE_BTN_W, CLOSE_BTN_H, THEME_CLOSE_BTN);
    /* X in close button */
    draw_char(win->fb, stride, close_x + 4, close_y + 0, 'x', 1,
              0xFFFFFFFF, THEME_CLOSE_BTN);

    /* borders - left */
    draw_fill_rect(win->fb, stride, 0, TITLEBAR_H, BORDER_W,
                   win->h - TITLEBAR_H, border);
    /* right */
    draw_fill_rect(win->fb, stride, win->w - BORDER_W, TITLEBAR_H, BORDER_W,
                   win->h - TITLEBAR_H, border);
    /* bottom */
    draw_fill_rect(win->fb, stride, 0, win->h - BORDER_W, win->w, BORDER_W, border);
}

/* --- frame submission --- */

void wm_submit_frame(wm_window_t *win) {
    if (!win->meta) return;
    /* Non-blocking: skip if compositor hasn't consumed previous frame */
    if (win->meta->ready) return;

    win->meta->dx0 = 0;
    win->meta->dy0 = 0;
    win->meta->dx1 = win->w;
    win->meta->dy1 = win->h;
    win->meta->frame_id++;
    win->meta->ready = 1;
}

/* --- window render (decorations + terminal) --- */

void wm_render_window(wm_state_t *wm, int idx) {
    wm_window_t *win = &wm->windows[idx];
    if (!win->active || !win->fb) return;

    /* Reconfigure layer if geometry changed */
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = win->x;
    cfg.y0 = win->y;
    cfg.x1 = win->x + win->w;
    cfg.y1 = win->y + win->h;
    cfg.alpha = 0;
    cfg.stride = win->w * BPP;

    fb_layer_info_t info = {0};
    sys_5ht_rcfg_layer(win->layer_id, &cfg, &info);
    win->fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    win->meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
    win->fb_stride_px = win->w;

    /* Update content area */
    win->cx = win->x + BORDER_W;
    win->cy = win->y + TITLEBAR_H;
    win->cw = win->w - BORDER_W * 2;
    win->ch = win->h - TITLEBAR_H - BORDER_W;

    /* Resize terminal if grid changed */
    uint32_t new_cols = win->cw / FONT_W;
    uint32_t new_rows = win->ch / FONT_H;
    if (new_cols == 0) new_cols = 1;
    if (new_rows == 0) new_rows = 1;

    if (new_cols != win->term.cols || new_rows != win->term.rows) {
        term_resize(&win->term, new_cols, new_rows);
        /* notify shell of new size */
        pty_winsize_t ws = {0};
        ws.ws_row = new_rows;
        ws.ws_col = new_cols;
        ws.ws_xpixel = win->cw;
        ws.ws_ypixel = win->ch;
        sys_5ht_pty_winsize(win->pty_master_fd, &ws, 0);
    }

    /* Clear the full fb first */
    draw_fill_rect(win->fb, win->fb_stride_px, 0, 0, win->w, win->h, THEME_TERM_BG);

    /* Render decorations */
    wm_render_decorations(wm, idx);

    /* Mark all cells dirty and render terminal */
    term_mark_all_dirty(&win->term);
    term_render(win);

    wm_submit_frame(win);
}

/* --- window lifecycle --- */

int wm_create_window(wm_state_t *wm) {
    /* find free slot */
    int idx = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wm->windows[i].active) { idx = i; break; }
    }
    if (idx < 0) return -1;

    /* find free layer */
    int layer_id = -1;
    for (int l = LAYER_WIN_BASE; l <= LAYER_WIN_MAX; l++) {
        int used = 0;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (wm->windows[i].active && wm->windows[i].layer_id == l)
                { used = 1; break; }
        }
        if (!used) { layer_id = l; break; }
    }
    if (layer_id < 0) return -1;

    wm_window_t *win = &wm->windows[idx];
    memset(win, 0, sizeof(*win));
    win->active = 1;
    win->layer_id = (uint16_t)layer_id;
    win->mode = WIN_TILED;
    snprintf(win->title, sizeof(win->title), "Terminal %d", idx + 1);

    wm->num_windows++;

    /* compute layout */
    layout_compute(wm);

    /* allocate layer */
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = win->x;
    cfg.y0 = win->y;
    cfg.x1 = win->x + win->w;
    cfg.y1 = win->y + win->h;
    cfg.alpha = 0;
    cfg.stride = win->w * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(layer_id, &cfg, &info) != 0) {
        win->active = 0;
        wm->num_windows--;
        return -1;
    }

    win->fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    win->meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
    win->fb_stride_px = win->w;

    /* compute content area */
    win->cx = win->x + BORDER_W;
    win->cy = win->y + TITLEBAR_H;
    win->cw = win->w - BORDER_W * 2;
    win->ch = win->h - TITLEBAR_H - BORDER_W;

    /* init terminal */
    uint32_t cols = win->cw / FONT_W;
    uint32_t rows = win->ch / FONT_H;
    if (cols == 0) cols = 1;
    if (rows == 0) rows = 1;
    term_init(&win->term, cols, rows);

    /* open PTY */
    int fds[2];
    if (sys_5ht_pty_open(fds) != 0) {
        sys_5ht_rel_buf(layer_id);
        win->active = 0;
        wm->num_windows--;
        return -1;
    }
    win->pty_master_fd = fds[0];

    /* set window size */
    pty_winsize_t ws = {0};
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = win->cw;
    ws.ws_ypixel = win->ch;
    sys_5ht_pty_winsize(fds[0], &ws, 0);

    /* fork shell */
    pid_t pid = fork();
    if (pid == 0) {
        /* child */
        close(fds[0]); /* close master */
        close(wm->kb_fd);
        close(wm->mouse_fd);

        /* set slave as stdio */
        close(0); close(1); close(2);
        dup2(fds[1], 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        if (fds[1] > 2) close(fds[1]);

        sys_5ht_pty_setpgrp(0);

        char *argv[] = { "/bin/login", NULL };
        char *envp[] = { "TERM=xterm", "HOME=/root", NULL };
        execve("/bin/login", argv, envp);
        /* fallback to shell */
        argv[0] = "/bin/sh";
        execve("/bin/sh", argv, envp);
        _exit(127);
    }

    close(fds[1]); /* close slave in parent */
    win->child_pid = pid;

    /* set non-blocking on master */
    /* TODO: if fcntl not available, we rely on poll timeout */

    /* focus new window */
    wm_focus_window(wm, idx);

    /* render all windows (layout may have changed) */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wm->windows[i].active)
            wm_render_window(wm, i);
    }

    render_taskbar(wm);
    return idx;
}

void wm_close_window(wm_state_t *wm, int idx) {
    wm_window_t *win = &wm->windows[idx];
    if (!win->active) return;

    /* kill child */
    if (win->child_pid > 0)
        kill(win->child_pid, 15); /* SIGTERM */

    /* close PTY master */
    if (win->pty_master_fd >= 0)
        close(win->pty_master_fd);

    /* free terminal */
    term_free(&win->term);

    /* release layer */
    sys_5ht_rel_buf(win->layer_id);

    win->active = 0;
    wm->num_windows--;

    /* reap child */
    if (win->child_pid > 0) {
        int status;
        waitpid(win->child_pid, &status);
    }

    /* refocus */
    if (wm->focused_idx == idx) {
        wm->focused_idx = -1;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (wm->windows[i].active) {
                wm_focus_window(wm, i);
                break;
            }
        }
    }

    /* recompute layout */
    layout_compute(wm);
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wm->windows[i].active)
            wm_render_window(wm, i);
    }

    render_taskbar(wm);
}

void wm_focus_window(wm_state_t *wm, int idx) {
    if (idx == wm->focused_idx) return;

    /* unfocus old */
    if (wm->focused_idx >= 0 && wm->windows[wm->focused_idx].active) {
        wm->windows[wm->focused_idx].focused = 0;
        wm_render_decorations(wm, wm->focused_idx);
        wm_submit_frame(&wm->windows[wm->focused_idx]);
    }

    wm->focused_idx = idx;

    /* focus new */
    if (idx >= 0 && wm->windows[idx].active) {
        wm->windows[idx].focused = 1;
        wm_render_decorations(wm, idx);
        wm_submit_frame(&wm->windows[idx]);
    }

    render_taskbar(wm);
}

/* --- signal handler --- */

static void sig_child(int sig) { (void)sig; }

/* --- main --- */

int main(void) {
    memset(&wm, 0, sizeof(wm));
    wm.focused_idx = -1;
    wm.tiling.master_ratio = 0.6f;
    wm.tiling.master_count = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    signal(17, (void *)sig_child); /* SIGCHLD */
    signal(13, (void *)sig_child); /* SIGPIPE */

    wm_init_palette(&wm);

    /* grab keyboard input so VTY doesn't consume keystrokes */
    sys_5ht_grab_input(1);

    /* open input devices */
    wm.kb_fd = open("/dev/keyboard/event", O_RDONLY | O_NONBLOCK);
    if (wm.kb_fd < 0) {
        printf("wm: failed to open keyboard\n");
        return 1;
    }

    wm.mouse_fd = open("/dev/mouse/event", O_RDONLY | O_NONBLOCK);
    if (wm.mouse_fd < 0) {
        printf("wm: failed to open mouse\n");
        return 1;
    }

    /* init cursor and taskbar */
    init_cursor_layer(&wm);
    init_taskbar(&wm);
    render_taskbar(&wm);

    /* create first terminal window */
    if (wm_create_window(&wm) < 0) {
        printf("wm: failed to create initial window\n");
        return 1;
    }

    printf("wm: started\n");

    /* --- event loop --- */
    while (1) {
        /* build pollfd array */
        struct pollfd pfds[2 + MAX_WINDOWS];
        int pfd_map[2 + MAX_WINDOWS]; /* maps pfd index to window index */
        int npfds = 0;

        pfds[npfds].fd = wm.kb_fd;
        pfds[npfds].events = POLLIN;
        pfds[npfds].revents = 0;
        int kb_idx = npfds++;

        pfds[npfds].fd = wm.mouse_fd;
        pfds[npfds].events = POLLIN;
        pfds[npfds].revents = 0;
        int mouse_idx = npfds++;

        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!wm.windows[i].active) continue;
            pfd_map[npfds] = i;
            pfds[npfds].fd = wm.windows[i].pty_master_fd;
            pfds[npfds].events = POLLIN;
            pfds[npfds].revents = 0;
            npfds++;
        }

        poll(pfds, npfds, 16); /* ~60Hz wake for cursor blink */

        /* handle keyboard */
        if (pfds[kb_idx].revents & POLLIN) {
            keyboard_event_t ev;
            while (read(wm.kb_fd, &ev, sizeof(ev)) == sizeof(ev)) {
                wm_handle_keyboard(&wm, &ev);
            }
        }

        /* handle mouse */
        if (pfds[mouse_idx].revents & POLLIN) {
            mouse_event_t ev;
            while (read(wm.mouse_fd, &ev, sizeof(ev)) == sizeof(ev)) {
                wm_handle_mouse(&wm, &ev);
                move_cursor(&wm, wm.mouse_x, wm.mouse_y);
            }
        }

        /* handle PTY output */
        for (int p = 2; p < npfds; p++) {
            int win_idx = pfd_map[p];
            wm_window_t *win = &wm.windows[win_idx];

            if (pfds[p].revents & (POLLIN | POLLHUP)) {
                char buf[4096];
                int n = read(win->pty_master_fd, buf, sizeof(buf));
                if (n > 0) {
                    term_process(&win->term, buf, n);
                    /* render only terminal content (not full rerender) */
                    term_render(win);
                    wm_submit_frame(win);
                } else if (n <= 0 && (pfds[p].revents & POLLHUP)) {
                    wm_close_window(&wm, win_idx);
                    break; /* pfds invalidated, restart loop */
                }
            }
        }

        /* cursor blink */
        blink_counter++;
        if (blink_counter >= BLINK_INTERVAL) {
            blink_counter = 0;
            cursor_blink_on ^= 1;
            if (wm.focused_idx >= 0 && wm.windows[wm.focused_idx].active) {
                wm_window_t *win = &wm.windows[wm.focused_idx];
                win->term.cursor_visible = cursor_blink_on;
                term_render(win);
                wm_submit_frame(win);
            }
        }

        /* Dead children are detected via POLLHUP on PTY master fd.
           SIGCHLD handler reaps zombies. */
    }

    return 0;
}
