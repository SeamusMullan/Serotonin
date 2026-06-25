#include <signal.h>
#include <unistd.h>
#include "wm.h"
#include "wm_ipc.h"

/* Alt key tracking (scancode 0x38) */
static uint8_t alt_held = 0;
static uint8_t shift_held = 0;

void wm_handle_keyboard(wm_state_t *wm, keyboard_event_t *ev) {
    /* Track modifier state */
    if (ev->scancode == 0x38) { /* Alt */
        alt_held = !(ev->flags & KEY_FLAG_RELEASED);
        return;
    }
    if (ev->scancode == 0x2A || ev->scancode == 0x36) { /* Shift */
        shift_held = !(ev->flags & KEY_FLAG_RELEASED);
        return;
    }

    /* Key releases: forward to focused GUI client; otherwise discard */
    if ((ev->flags & KEY_FLAG_RELEASED) && !wm->launcher_active &&
        wm->focused_idx >= 0 && wm->windows[wm->focused_idx].active &&
        wm->windows[wm->focused_idx].is_gui && wm->windows[wm->focused_idx].pty_master_fd >= 0) {
        sg_gui_event_t ge;
        wm_ipc_fill_keyboard(&ge, ev);
        wm_ipc_send(wm->windows[wm->focused_idx].pty_master_fd, &ge);
        return;
    }

    /* Only handle key presses, not releases */
    if (ev->flags & KEY_FLAG_RELEASED) return;

    /* Launcher intercepts all input when active (except Alt+P to close) */
    if (wm->launcher_active) {
        if (alt_held && ev->scancode == 0x19) {
            launcher_close(wm);
            return;
        }
        launcher_key(wm, ev);
        return;
    }

    /* WM keyboard shortcuts (Alt held) */
    if (alt_held) {
        switch (ev->scancode) {
        case 0x1C: /* Alt+Enter: new terminal */
            wm_create_window(wm);
            return;
        case 0x0F: /* Alt+Tab: cycle focus */
            if (wm->num_windows > 1) {
                int next = wm->focused_idx;
                for (int tries = 0; tries < MAX_WINDOWS; tries++) {
                    next = (next + 1) % MAX_WINDOWS;
                    if (wm->windows[next].active) {
                        wm_focus_window(wm, next);
                        break;
                    }
                }
            }
            return;
        case 0x3E: /* Alt+F4: close window */
            if (wm->focused_idx >= 0 && wm->windows[wm->focused_idx].active)
                wm_close_window(wm, wm->focused_idx);
            return;
        case 0x23: /* Alt+H: shrink master */
            if (wm->tiling.master_ratio > 0.15f) {
                wm->tiling.master_ratio -= 0.05f;
                desktop_mark_dirty(wm, 0, 0, SCREEN_W, SCREEN_H - TASKBAR_H);
                layout_compute(wm);
                for (int i = 0; i < MAX_WINDOWS; i++)
                    if (wm->windows[i].active)
                        wm_render_window(wm, i);
            }
            return;
        case 0x26: /* Alt+L: grow master */
            if (wm->tiling.master_ratio < 0.85f) {
                wm->tiling.master_ratio += 0.05f;
                desktop_mark_dirty(wm, 0, 0, SCREEN_W, SCREEN_H - TASKBAR_H);
                layout_compute(wm);
                for (int i = 0; i < MAX_WINDOWS; i++)
                    if (wm->windows[i].active)
                        wm_render_window(wm, i);
            }
            return;
        case 0x24: /* Alt+J: focus next */
            if (wm->num_windows > 1) {
                int next = wm->focused_idx;
                for (int tries = 0; tries < MAX_WINDOWS; tries++) {
                    next = (next + 1) % MAX_WINDOWS;
                    if (wm->windows[next].active) {
                        wm_focus_window(wm, next);
                        break;
                    }
                }
            }
            return;
        case 0x25: /* Alt+K: focus prev */
            if (wm->num_windows > 1) {
                int prev = wm->focused_idx;
                for (int tries = 0; tries < MAX_WINDOWS; tries++) {
                    prev = (prev - 1 + MAX_WINDOWS) % MAX_WINDOWS;
                    if (wm->windows[prev].active) {
                        wm_focus_window(wm, prev);
                        break;
                    }
                }
            }
            return;
        case 0x19: /* Alt+P: toggle launcher */
            if (wm->launcher_active)
                launcher_close(wm);
            else
                launcher_open(wm);
            return;
        case 0x39: /* Alt+Space: toggle tiled/floating */
            if (wm->focused_idx >= 0 && wm->windows[wm->focused_idx].active) {
                wm_window_t *w = &wm->windows[wm->focused_idx];
                w->mode = (w->mode == WIN_TILED) ? WIN_FLOATING : WIN_TILED;
                desktop_mark_dirty(wm, 0, 0, SCREEN_W, SCREEN_H - TASKBAR_H);
                layout_compute(wm);
                for (int i = 0; i < MAX_WINDOWS; i++)
                    if (wm->windows[i].active)
                        wm_render_window(wm, i);
            }
            return;
        }
    }

    /* Forward to focused window's PTY */
    if (wm->focused_idx >= 0 && wm->windows[wm->focused_idx].active) {
        wm_window_t *win = &wm->windows[wm->focused_idx];

        if (win->is_gui && win->pty_master_fd >= 0) {
            /* Terminals get Ctrl+C as 0x03 on the PTY; GUI clients only have the
             * WM socket, so deliver SIGINT here like a tty interrupt. */
            if ((ev->flags & KEY_FLAG_CTRL) && win->child_pid > 0 &&
                (ev->ascii == 'c' || ev->ascii == 'C')) {
                kill(win->child_pid, 2); /* SIGINT */
                return;
            }
            sg_gui_event_t ge;
            wm_ipc_fill_keyboard(&ge, ev);
            wm_ipc_send(win->pty_master_fd, &ge);
            return;
        }

        /* Cursor / navigation keys → ANSI escape sequences */
        {
            const char *seq = 0;
            switch (ev->scancode) {
            case 0x48: seq = "\033[A"; break; /* Up    */
            case 0x50: seq = "\033[B"; break; /* Down  */
            case 0x4D: seq = "\033[C"; break; /* Right */
            case 0x4B: seq = "\033[D"; break; /* Left  */
            case 0x47: seq = "\033[H"; break; /* Home  */
            case 0x4F: seq = "\033[F"; break; /* End   */
            case 0x49: seq = "\033[5~"; break; /* PgUp  */
            case 0x51: seq = "\033[6~"; break; /* PgDn  */
            case 0x52: seq = "\033[2~"; break; /* Ins   */
            case 0x53: seq = "\033[3~"; break; /* Del   */
            }
            if (seq) {
                while (*seq)
                    write(win->pty_master_fd, seq++, 1);
                return;
            }
        }

        char ch = 0;

        if (ev->flags & KEY_FLAG_CTRL) {
            /* Ctrl+letter: send control character */
            if (ev->ascii >= 'a' && ev->ascii <= 'z')
                ch = ev->ascii - 'a' + 1;
            else if (ev->ascii >= 'A' && ev->ascii <= 'Z')
                ch = ev->ascii - 'A' + 1;
        } else if (ev->ascii) {
            ch = ev->ascii;
        }

        if (ch) {
            write(win->pty_master_fd, &ch, 1);
        }
    }
}

// cppcheck-suppress constParameterPointer
void wm_handle_mouse(wm_state_t *wm, mouse_event_t *ev) {
    wm->mouse_x = ev->x;
    wm->mouse_y = ev->y;
    wm->mouse_buttons = ev->buttons;

    /* Handle drag in progress */
    if (wm->drag.mode != DRAG_NONE) {
        if (ev->event_type == MOUSE_EVENT_BUTTON_UP) {
            if (wm->drag.mode == DRAG_RESIZE) {
                /* Now do the actual layer reconfigure + full render */
                wm_render_window(wm, wm->drag.win_idx);
            }
            wm->drag.mode = DRAG_NONE;
            return;
        }

        wm_window_t *win = &wm->windows[wm->drag.win_idx];
        if (!win->active) { wm->drag.mode = DRAG_NONE; return; }

        int dx = ev->x - wm->drag.start_mx;
        int dy = ev->y - wm->drag.start_my;

        if (wm->drag.mode == DRAG_MOVE) {
            /* Mark old position on desktop so compositor erases the ghost */
            desktop_mark_dirty(wm, win->x, win->y, win->w, win->h);

            int nx = wm->drag.start_wx + dx;
            int ny = wm->drag.start_wy + dy;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx + win->w > SCREEN_W) nx = SCREEN_W - win->w;
            if (ny + win->h > SCREEN_H - TASKBAR_H) ny = SCREEN_H - TASKBAR_H - win->h;

            if (win->is_gui) {
                /* Layer owned by GUI child; WM cannot rcfg (EPERM). Push geometry like
                   wm_render_window does so the client moves its layer. */
                win->x = (uint16_t)nx;
                win->y = (uint16_t)ny;
                win->cx = win->x;
                win->cy = win->y;
                win->cw = win->w;
                win->ch = win->h;
                if (win->pty_master_fd >= 0) {
                    sg_gui_event_t ge;
                    wm_ipc_fill_configure(&ge, win->x, win->y,
                        (uint16_t)(win->x + win->w), (uint16_t)(win->y + win->h));
                    wm_ipc_send(win->pty_master_fd, &ge);
                }
            } else {
                /* Reposition WM-owned layer — same dimensions, no realloc. Commit x/y
                   only after syscall succeeds; on failure kernel leaves info untouched
                   (zeroed here) and writing meta would fault. */
                fb_layer_config_t cfg = {0};
                cfg.size = sizeof(cfg);
                cfg.x0 = (uint16_t)nx;
                cfg.y0 = (uint16_t)ny;
                cfg.x1 = (uint16_t)(nx + win->w);
                cfg.y1 = (uint16_t)(ny + win->h);
                cfg.alpha = 0;
                cfg.hints = FB_LAYER_HINT_OPAQUE_CONTENT | FB_LAYER_HINT_FREQUENT_UPDATES;
                cfg.stride = win->w * BPP;
                fb_layer_info_t info = {0};
                if (sys_5ht_rcfg_layer(win->layer_id, &cfg, &info) != 0)
                    return;
                win->x = (uint16_t)nx;
                win->y = (uint16_t)ny;
                win->cx = win->x + BORDER_W;
                win->cy = win->y + TITLEBAR_H;
                win->fb = (uint32_t *)(uintptr_t)info.fb_user_va;
                win->meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
                /* Submit full window dirty at new position */
                win->meta->dx0 = 0; win->meta->dy0 = 0;
                win->meta->dx1 = win->w; win->meta->dy1 = win->h;
                win->meta->frame_id++;
                win->meta->ready = 1;
            }
        } else if (wm->drag.mode == DRAG_RESIZE) {
            /* Just track desired size during drag — defer the expensive
               layer reconfigure + realloc to button-up */
            int nw = wm->drag.start_ww + dx;
            int nh = wm->drag.start_wh + dy;
            int min_w = BORDER_W * 2 + FONT_W * 10;
            int min_h = TITLEBAR_H + BORDER_W + FONT_H * 3;
            if (nw < min_w) nw = min_w;
            if (nh < min_h) nh = min_h;
            if (win->x + nw > SCREEN_W) nw = SCREEN_W - win->x;
            if (win->y + nh > SCREEN_H - TASKBAR_H) nh = SCREEN_H - TASKBAR_H - win->y;
            win->w = (uint16_t)nw;
            win->h = (uint16_t)nh;
        }
        return;
    }

    /* Click: check overlays and taskbar first */
    if (ev->event_type == MOUSE_EVENT_BUTTON_DOWN &&
        (ev->buttons & MOUSE_BTN_LEFT)) {
        int mx = ev->x, my = ev->y;

        /* Taskbar settings button → Settings GUI */
        if (my >= SCREEN_H - TASKBAR_H && my < SCREEN_H) {
            int brand_x = SCREEN_W - 9 * FONT_W - 8;
            int sbtn_x = brand_x - SETTINGS_BTN_W - 8;
            int sbtn_y = SCREEN_H - TASKBAR_H + (TASKBAR_H - SETTINGS_BTN_H) / 2 + 1;
            if (mx >= sbtn_x && mx < sbtn_x + SETTINGS_BTN_W &&
                my >= sbtn_y && my < sbtn_y + SETTINGS_BTN_H) {
                (void)wm_launch_gui_window(wm, "settings");
                return;
            }
        }
    }

    /* Click: find which window was hit */
    if (ev->event_type == MOUSE_EVENT_BUTTON_DOWN &&
        (ev->buttons & (MOUSE_BTN_LEFT | MOUSE_BTN_RIGHT))) {

        for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
            // cppcheck-suppress constVariablePointer
            wm_window_t *win = &wm->windows[i];
            if (!win->active) continue;

            int mx = ev->x, my = ev->y;
            if (mx >= win->x && mx < win->x + win->w &&
                my >= win->y && my < win->y + win->h) {

                /* focus this window */
                if (wm->focused_idx != i)
                    wm_focus_window(wm, i);

                /* Alt+click on floating: drag move */
                if (alt_held && (ev->buttons & MOUSE_BTN_LEFT) &&
                    win->mode == WIN_FLOATING) {
                    wm->drag.mode = DRAG_MOVE;
                    wm->drag.win_idx = i;
                    wm->drag.start_mx = mx;
                    wm->drag.start_my = my;
                    wm->drag.start_wx = win->x;
                    wm->drag.start_wy = win->y;
                    return;
                }
                /* Alt+right-click on floating: drag resize */
                if (alt_held && (ev->buttons & MOUSE_BTN_RIGHT) &&
                    win->mode == WIN_FLOATING) {
                    wm->drag.mode = DRAG_RESIZE;
                    wm->drag.win_idx = i;
                    wm->drag.start_mx = mx;
                    wm->drag.start_my = my;
                    wm->drag.start_ww = win->w;
                    wm->drag.start_wh = win->h;
                    return;
                }

                if (ev->buttons & MOUSE_BTN_LEFT) {
                    /* check close button */
                    int close_x = win->x + win->w - BORDER_W - CLOSE_BTN_W;
                    int close_y = win->y + (TITLEBAR_H - CLOSE_BTN_H) / 2;
                    if (mx >= close_x && mx < close_x + CLOSE_BTN_W &&
                        my >= close_y && my < close_y + CLOSE_BTN_H) {
                        wm_close_window(wm, i);
                        return;
                    }

                    /* title bar drag (floating windows) */
                    if (win->mode == WIN_FLOATING &&
                        my >= win->y && my < win->y + TITLEBAR_H) {
                        wm->drag.mode = DRAG_MOVE;
                        wm->drag.win_idx = i;
                        wm->drag.start_mx = mx;
                        wm->drag.start_my = my;
                        wm->drag.start_wx = win->x;
                        wm->drag.start_wy = win->y;
                        return;
                    }

                    /* bottom-right corner resize grip (floating windows) */
                    #define GRIP_SIZE 8
                    if (win->mode == WIN_FLOATING &&
                        mx >= win->x + win->w - GRIP_SIZE &&
                        my >= win->y + win->h - GRIP_SIZE) {
                        wm->drag.mode = DRAG_RESIZE;
                        wm->drag.win_idx = i;
                        wm->drag.start_mx = mx;
                        wm->drag.start_my = my;
                        wm->drag.start_ww = win->w;
                        wm->drag.start_wh = win->h;
                        return;
                    }
                }
                break;
            }
        }
    }

    /* Forward mouse to focused GUI (layer-local coordinates) */
    if (wm->focused_idx >= 0 && wm->windows[wm->focused_idx].active) {
        // cppcheck-suppress constVariablePointer
        wm_window_t *gw = &wm->windows[wm->focused_idx];
        if (gw->is_gui && gw->pty_master_fd >= 0) {
            int lx = ev->x - gw->x;
            int ly = ev->y - gw->y;
            if (lx < 0) lx = 0;
            if (ly < 0) ly = 0;
            if (lx > gw->w) lx = gw->w;
            if (ly > gw->h) ly = gw->h;
            sg_gui_event_t ge;
            wm_ipc_fill_mouse(&ge, (int16_t)lx, (int16_t)ly, ev->buttons, ev->event_type);
            wm_ipc_send(gw->pty_master_fd, &ge);
        }
    }
}
