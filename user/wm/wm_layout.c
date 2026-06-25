#include "wm.h"

// cppcheck-suppress constParameterPointer
int count_tiled_windows(wm_state_t *wm) {
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wm->windows[i].active && wm->windows[i].mode == WIN_TILED)
            n++;
    }
    return n;
}

void layout_compute(wm_state_t *wm) {
    int usable_h = SCREEN_H - TASKBAR_H;
    int n_tiled = count_tiled_windows(wm);
    if (n_tiled == 0) return;

    int master_w = (int)(SCREEN_W * wm->tiling.master_ratio);
    int stack_w = SCREEN_W - master_w;
    int tiled_idx = 0;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        wm_window_t *win = &wm->windows[i];
        if (!win->active || win->mode != WIN_TILED) continue;

        if (n_tiled == 1) {
            /* single window: full screen */
            win->x = 0;
            win->y = 0;
            win->w = SCREEN_W;
            win->h = usable_h;
        } else if (tiled_idx == 0) {
            /* master */
            win->x = 0;
            win->y = 0;
            win->w = master_w;
            win->h = usable_h;
        } else {
            /* stack */
            int stack_n = n_tiled - 1;
            int slot = tiled_idx - 1;
            int slot_h = usable_h / stack_n;
            int extra = (slot == stack_n - 1) ? (usable_h - slot_h * stack_n) : 0;
            win->x = master_w;
            win->y = slot * slot_h;
            win->w = stack_w;
            win->h = slot_h + extra;
        }

        /* compute content area */
        win->cx = win->x + BORDER_W;
        win->cy = win->y + TITLEBAR_H;
        win->cw = win->w - BORDER_W * 2;
        win->ch = win->h - TITLEBAR_H - BORDER_W;

        tiled_idx++;
    }
}
