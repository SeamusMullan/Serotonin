#include <unistd.h>
#include "wm.h"

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

    /* Only handle key presses, not releases */
    if (ev->flags & KEY_FLAG_RELEASED) return;

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
                layout_compute(wm);
                for (int i = 0; i < MAX_WINDOWS; i++)
                    if (wm->windows[i].active)
                        wm_render_window(wm, i);
            }
            return;
        case 0x26: /* Alt+L: grow master */
            if (wm->tiling.master_ratio < 0.85f) {
                wm->tiling.master_ratio += 0.05f;
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
        case 0x39: /* Alt+Space: toggle tiled/floating */
            if (wm->focused_idx >= 0 && wm->windows[wm->focused_idx].active) {
                wm_window_t *w = &wm->windows[wm->focused_idx];
                w->mode = (w->mode == WIN_TILED) ? WIN_FLOATING : WIN_TILED;
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

void wm_handle_mouse(wm_state_t *wm, mouse_event_t *ev) {
    wm->mouse_x = ev->x;
    wm->mouse_y = ev->y;
    wm->mouse_buttons = ev->buttons;

    /* Handle drag in progress */
    if (wm->drag.mode != DRAG_NONE) {
        if (ev->event_type == MOUSE_EVENT_BUTTON_UP) {
            wm->drag.mode = DRAG_NONE;
            return;
        }
        /* TODO: implement drag move/resize in Phase 4 */
        return;
    }

    /* Click: find which window was hit */
    if (ev->event_type == MOUSE_EVENT_BUTTON_DOWN && (ev->buttons & MOUSE_BTN_LEFT)) {
        for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
            wm_window_t *win = &wm->windows[i];
            if (!win->active) continue;

            int mx = ev->x, my = ev->y;
            if (mx >= win->x && mx < win->x + win->w &&
                my >= win->y && my < win->y + win->h) {

                /* focus this window */
                if (wm->focused_idx != i)
                    wm_focus_window(wm, i);

                /* check close button */
                int close_x = win->x + win->w - BORDER_W - CLOSE_BTN_W;
                int close_y = win->y + (TITLEBAR_H - CLOSE_BTN_H) / 2;
                if (mx >= close_x && mx < close_x + CLOSE_BTN_W &&
                    my >= close_y && my < close_y + CLOSE_BTN_H) {
                    wm_close_window(wm, i);
                }
                break;
            }
        }
    }
}
