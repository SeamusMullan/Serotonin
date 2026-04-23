/**
 * WM ↔ GUI application binary event protocol (packed structs).
 *
 * Transport: Serotonin `AF_UNIX` `SOCK_STREAM` socket (see `socketpair(2)` /
 * `socket(2)` in user/syscall). Not Linux-specific — kernel implements Unix
 * domain sockets in kernel/schedule/schedule.c + kernel/syscall/syscall.c.
 *
 * Direction: window manager writes framed records; GUI client reads the
 * same-sized `sg_gui_event_t` records (use `read(2)` until buffer full).
 *
 * Default fd after WM spawn: dup2 client end to 3, or set
 * `SEROTONIN_GUI_EVENTS_FD` in the environment to the integer fd number.
 */

#ifndef SEROTONIN_GUI_PROTOCOL_H
#define SEROTONIN_GUI_PROTOCOL_H

#include <lib5ht.h>
#include <stdint.h>

/** Default fd for GUI event stream in child after WM `dup2`. */
#define SG_GUI_EVENTS_FD 3

/** Environment variable overriding the events fd (decimal). */
#define SG_GUI_ENV_EVENTS_FD "SEROTONIN_GUI_EVENTS_FD"

/** Child calls `sys_5ht_req_buf` on this layer (reserved by WM before fork). */
#define SG_GUI_ENV_LAYER_ID "SEROTONIN_GUI_LAYER_ID"

/** Initial / updated layer geometry: screen-space `fb_layer_config_t` edges. */
#define SG_GUI_ENV_X0 "SEROTONIN_GUI_X0"
#define SG_GUI_ENV_Y0 "SEROTONIN_GUI_Y0"
#define SG_GUI_ENV_X1 "SEROTONIN_GUI_X1"
#define SG_GUI_ENV_Y1 "SEROTONIN_GUI_Y1"

enum {
    SG_GUI_EV_KEYBOARD = 1,
    SG_GUI_EV_MOUSE = 2,
    SG_GUI_EV_CONFIGURE = 3,
    SG_GUI_EV_FOCUS = 4,
    /** Compositor z-slot changed; client must use @c layer_id for `sys_5ht_rcfg_layer`. */
    SG_GUI_EV_LAYER = 5,
};

typedef struct __attribute__((packed)) {
    uint8_t type;
    union {
        keyboard_event_t kb;
        struct {
            int16_t lx, ly;
            uint8_t buttons;
            uint8_t ev_type;
        } mouse;
        struct {
            uint16_t x0, y0, x1, y1;
        } cfg;
        struct {
            uint8_t focused;
        } focus;
        struct {
            uint16_t layer_id;
            uint16_t reserved[3];
        } layer;
    } u;
} sg_gui_event_t;

#endif
