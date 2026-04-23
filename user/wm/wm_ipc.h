#ifndef WM_IPC_H
#define WM_IPC_H

#include "gui_protocol.h"
#include <lib5ht.h>

#ifdef __cplusplus
extern "C" {
#endif

int wm_ipc_socketpair(int sv[2]);
int wm_ipc_send(int fd, const sg_gui_event_t *ev);

void wm_ipc_fill_keyboard(sg_gui_event_t *out, const keyboard_event_t *kb);
void wm_ipc_fill_mouse(sg_gui_event_t *out, int16_t lx, int16_t ly,
                       uint8_t buttons, uint8_t ev_type);
void wm_ipc_fill_configure(sg_gui_event_t *out, uint16_t x0, uint16_t y0,
                           uint16_t x1, uint16_t y1);
void wm_ipc_fill_focus(sg_gui_event_t *out, uint8_t focused);
void wm_ipc_fill_layer(sg_gui_event_t *out, uint16_t layer_id);
void wm_ipc_fill_theme(sg_gui_event_t *out, const sg_gui_wm_theme_colors_t *colors);

#ifdef __cplusplus
}
#endif

#endif
