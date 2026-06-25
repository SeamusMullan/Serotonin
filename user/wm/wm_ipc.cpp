/**
 * @file wm_ipc.cpp
 * @brief C++-compiled WM ↔ GUI helpers (Serotonin AF_UNIX SOCK_STREAM).
 */

#include "wm_ipc.h"

#include <string.h>
#include <unistd.h>

extern "C" {
#include "../syscall/sys/socket.h"
}

extern "C" int wm_ipc_socketpair(int sv[2]) {
    if (!sv)
        return -1;
    return socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
}

extern "C" int wm_ipc_send(int fd, const sg_gui_event_t *ev) {
    if (fd < 0 || !ev)
        return -1;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(ev);
    size_t left = sizeof(*ev);
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n <= 0)
            return -1;
        left -= static_cast<size_t>(n);
        p += static_cast<size_t>(n);
    }
    return 0;
}

extern "C" void wm_ipc_fill_keyboard(sg_gui_event_t *out, const keyboard_event_t *kb) {
    if (!out || !kb)
        return;
    memset(out, 0, sizeof(*out));
    out->type = SG_GUI_EV_KEYBOARD;
    out->u.kb = *kb;
}

extern "C" void wm_ipc_fill_mouse(sg_gui_event_t *out, int16_t lx, int16_t ly,
                                  uint8_t buttons, uint8_t ev_type) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->type = SG_GUI_EV_MOUSE;
    out->u.mouse.lx = lx;
    out->u.mouse.ly = ly;
    out->u.mouse.buttons = buttons;
    out->u.mouse.ev_type = ev_type;
}

extern "C" void wm_ipc_fill_configure(sg_gui_event_t *out, uint16_t x0, uint16_t y0,
                                      uint16_t x1, uint16_t y1) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->type = SG_GUI_EV_CONFIGURE;
    out->u.cfg.x0 = x0;
    out->u.cfg.y0 = y0;
    out->u.cfg.x1 = x1;
    out->u.cfg.y1 = y1;
}

extern "C" void wm_ipc_fill_focus(sg_gui_event_t *out, uint8_t focused) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->type = SG_GUI_EV_FOCUS;
    out->u.focus.focused = focused;
}

extern "C" void wm_ipc_fill_layer(sg_gui_event_t *out, uint16_t layer_id) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->type = SG_GUI_EV_LAYER;
    out->u.layer.layer_id = layer_id;
    out->u.layer.reserved[0] = 0;
    out->u.layer.reserved[1] = 0;
    out->u.layer.reserved[2] = 0;
}

extern "C" void wm_ipc_fill_theme(sg_gui_event_t *out, const sg_gui_wm_theme_colors_t *colors) {
    if (!out || !colors)
        return;
    memset(out, 0, sizeof(*out));
    out->type = SG_GUI_EV_THEME;
    out->u.theme = *colors;
}
