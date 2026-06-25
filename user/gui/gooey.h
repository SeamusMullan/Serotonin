/**
 * @file gooey.h
 * @brief Header-only Serotonin GUI helper for user-mode apps under the window manager.
 *
 * Event transport: Serotonin `AF_UNIX` `SOCK_STREAM` (see `../wm/gui_protocol.h`).
 * After `execve`, the child must own its compositor layer (`sys_5ht_req_buf` in
 * the GUI process) — parent VA from `fork` does not survive `execve`.
 *
 * Build (from `user/`, same as other Serotonin user binaries):
 *   i686-serotonin-g++ -std=c++11 -fno-exceptions -fno-rtti app.cpp -o app.elf
 *
 * Include path: place `-I.` so `#include "gui/gooey.h"` works; the header
 * pulls `../wm/gui_protocol.h` relative to this directory.
 */

#ifndef GOOEY_H
#define GOOEY_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <unistd.h>

#include <lib5ht.h>

extern "C" {
#include "../syscall/sys/poll.h"
}

#include "../wm/gui_protocol.h"

namespace gooey {

/** ARGB helpers: alpha 0xFF = opaque. */
inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
    return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return rgba(r, g, b, 0xFF); }

/**
 * Environment variables the window manager can export for a GUI child
 * (hex or decimal uintptr_t values for addresses). Optional until the WM
 * wires GUI launch; `open_layer()` remains the fallback for standalone tests.
 *
 * Theme: @c SG_GUI_ENV_THEME (see `gui_protocol.h`) — 15×8 hex digit WM palette;
 * use `gui/gooey_theme.h` to map into widget @c Theme + @c frame::ChromeColors .
 */
namespace env_name {
static const char k_fb_va[] = "SEROTONIN_GUI_FB_VA";
static const char k_meta_va[] = "SEROTONIN_GUI_META_VA";
static const char k_width_px[] = "SEROTONIN_GUI_WIDTH";
static const char k_height_px[] = "SEROTONIN_GUI_HEIGHT";
static const char k_stride_px[] = "SEROTONIN_GUI_STRIDE_PX";
static const char k_layer_id[] = SG_GUI_ENV_LAYER_ID;
static const char k_x0[] = SG_GUI_ENV_X0;
static const char k_y0[] = SG_GUI_ENV_Y0;
static const char k_x1[] = SG_GUI_ENV_X1;
static const char k_y1[] = SG_GUI_ENV_Y1;
static const char k_events_fd[] = SG_GUI_ENV_EVENTS_FD;
} // namespace env_name

inline bool parse_uint(const char *s, uint32_t *out) {
    if (!s || !*s || !out)
        return false;
    char *end = nullptr;
    unsigned long v = strtoul(s, &end, 0);
    if (end == s)
        return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

inline bool parse_u64(const char *s, uint64_t *out) {
    if (!s || !*s || !out)
        return false;
    char *end = nullptr;
    unsigned long long v = strtoull(s, &end, 0);
    if (end == s)
        return false;
    *out = static_cast<uint64_t>(v);
    return true;
}

/** Fixed-size event from `SG_GUI_EVENTS_FD` (see gui_protocol.h). */
struct Event {
    enum Kind {
        k_none = 0,
        k_keyboard = SG_GUI_EV_KEYBOARD,
        k_mouse = SG_GUI_EV_MOUSE,
        k_configure = SG_GUI_EV_CONFIGURE,
        k_focus = SG_GUI_EV_FOCUS,
        k_layer = SG_GUI_EV_LAYER,
        k_theme = SG_GUI_EV_THEME
    };

    Kind kind;
    keyboard_event_t keyboard;
    struct {
        int16_t lx, ly;
        uint8_t buttons;
        uint8_t ev_type;
    } mouse;
    struct {
        uint16_t x0, y0, x1, y1;
    } configure;
    struct {
        uint8_t focused;
    } focus;
    struct {
        uint16_t layer_id;
    } layer;
    struct {
        uint32_t wm[SG_GUI_WM_THEME_NCOLORS];
    } theme;
};

/**
 * Read one packed `sg_gui_event_t` from @p fd (blocking unless fd is non-blocking).
 * @return bytes read, or -1 on error (check errno), or 0 on EOF.
 */
inline ssize_t read_gui_event(int fd, Event *out) {
    if (!out || fd < 0)
        return -1;
    sg_gui_event_t raw;
    memset(&raw, 0, sizeof(raw));
    unsigned char *buf = reinterpret_cast<unsigned char *>(&raw);
    size_t got = 0;
    while (got < sizeof(raw)) {
        ssize_t n = read(fd, buf + got, sizeof(raw) - got);
        if (n == 0)
            return got == 0 ? 0 : -1;
        if (n < 0)
            return -1;
        got += static_cast<size_t>(n);
    }

    out->kind = static_cast<Event::Kind>(raw.type);
    switch (raw.type) {
    case SG_GUI_EV_KEYBOARD:
        out->keyboard = raw.u.kb;
        break;
    case SG_GUI_EV_MOUSE:
        out->mouse.lx = raw.u.mouse.lx;
        out->mouse.ly = raw.u.mouse.ly;
        out->mouse.buttons = raw.u.mouse.buttons;
        out->mouse.ev_type = raw.u.mouse.ev_type;
        break;
    case SG_GUI_EV_CONFIGURE:
        out->configure.x0 = raw.u.cfg.x0;
        out->configure.y0 = raw.u.cfg.y0;
        out->configure.x1 = raw.u.cfg.x1;
        out->configure.y1 = raw.u.cfg.y1;
        break;
    case SG_GUI_EV_FOCUS:
        out->focus.focused = raw.u.focus.focused;
        break;
    case SG_GUI_EV_LAYER:
        out->layer.layer_id = raw.u.layer.layer_id;
        break;
    case SG_GUI_EV_THEME:
        memcpy(out->theme.wm, &raw.u.theme, sizeof(out->theme.wm));
        break;
    default:
        out->kind = Event::k_none;
        break;
    }
    return static_cast<ssize_t>(sizeof(raw));
}

/**
 * Non-blocking event read: poll fd first, only read if data available.
 * @return >0 on event read, 0 if nothing available, -1 on EOF/error.
 */
inline ssize_t poll_gui_event(int fd, Event *out) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, 0);
    if (rc <= 0)
        return 0;
    if (pfd.revents & (POLLHUP | POLLERR))
        return -1;
    if (!(pfd.revents & POLLIN))
        return 0;
    return read_gui_event(fd, out);
}

/**
 * Send a client → WM request on the GUI events socket (see @c SG_GUI_CLI_* in
 * @c gui_protocol.h). Same record size as inbound @c sg_gui_event_t .
 */
inline bool wm_send_gui_cli_index(int fd, uint8_t cli_type, int index) {
    if (fd < 0)
        return false;
    if (index < -32768 || index > 32767)
        return false;
    sg_gui_event_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = cli_type;
    msg.u.cli_index.index = static_cast<int16_t>(index);
    ssize_t w = write(fd, reinterpret_cast<const unsigned char *>(&msg), sizeof(msg));
    return w == static_cast<ssize_t>(sizeof(msg));
}

/** Drawable view: ARGB8888, stride in pixels (matches WM `fb_stride_px`). */
class Surface {
public:
    Surface() : pixels_(nullptr), stride_px_(0), width_(0), height_(0) {}

    Surface(uint32_t *pixels, uint32_t stride_px, uint32_t width, uint32_t height)
        : pixels_(pixels), stride_px_(stride_px), width_(width), height_(height) {}

    uint32_t *pixels() { return pixels_; }
    const uint32_t *pixels() const { return pixels_; }
    uint32_t stride_px() const { return stride_px_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    bool valid() const { return pixels_ && width_ && height_ && stride_px_; }

    void put_pixel(int x, int y, uint32_t argb) {
        if (!valid() || x < 0 || y < 0)
            return;
        if (static_cast<uint32_t>(x) >= width_ || static_cast<uint32_t>(y) >= height_)
            return;
        pixels_[static_cast<uint32_t>(y) * stride_px_ + static_cast<uint32_t>(x)] = argb;
    }

    void fill_rect(int x, int y, int w, int h, uint32_t argb) {
        if (!valid() || w <= 0 || h <= 0)
            return;
        int x1 = x + w;
        int y1 = y + h;
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x1 > static_cast<int>(width_))
            x1 = static_cast<int>(width_);
        if (y1 > static_cast<int>(height_))
            y1 = static_cast<int>(height_);
        if (x1 <= x || y1 <= y)
            return;
        for (int row = y; row < y1; ++row) {
            uint32_t *line = pixels_ + static_cast<uint32_t>(row) * stride_px_;
            for (int col = x; col < x1; ++col)
                line[static_cast<uint32_t>(col)] = argb;
        }
    }

private:
    uint32_t *pixels_;
    uint32_t stride_px_;
    uint32_t width_;
    uint32_t height_;
};

/**
 * Owns (optional) layer allocation and compositor dirty / ready signalling.
 * When the WM hands off an existing layer via environment variables, call
 * `attach_from_environment()` and do not `close()` with release — the WM
 * retains the layer. When opened with `open_layer()`, `close()` releases it.
 */
class Window {
public:
    Window()
        : meta_(nullptr), layer_id_(0), owns_layer_(false), dirty_valid_(false), d0_(0), d1_(0),
          d2_(0), d3_(0) {}

    ~Window() { close(); }

    /** Try WM-provided addresses in the environment (non-owning). */
    bool attach_from_environment() {
        close();
        const char *es = getenv(env_name::k_fb_va);
        const char *em = getenv(env_name::k_meta_va);
        const char *ew = getenv(env_name::k_width_px);
        const char *eh = getenv(env_name::k_height_px);
        const char *est = getenv(env_name::k_stride_px);
        const char *el = getenv(env_name::k_layer_id);
        if (!es || !em || !ew || !eh || !est)
            return false;

        uint64_t fb64 = 0, meta64 = 0;
        if (!parse_u64(es, &fb64) || !parse_u64(em, &meta64))
            return false;
        uint32_t w = 0, h = 0, st = 0, lid = 0;
        if (!parse_uint(ew, &w) || !parse_uint(eh, &h) || !parse_uint(est, &st))
            return false;
        if (el)
            parse_uint(el, &lid);

        surface_ = Surface(reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(fb64)), st, w, h);
        meta_ = reinterpret_cast<volatile fb_layer_metadata_t *>(static_cast<uintptr_t>(meta64));
        layer_id_ = static_cast<uint16_t>(lid);
        owns_layer_ = false;
        reset_dirty_tracking();
        return surface_.valid() && meta_;
    }

    /**
     * Allocate a compositor layer (same pattern as `red_rect.c` / WM).
     * Caller picks a free `layer_id` consistent with the compositor policy.
     */
    bool open_layer(uint16_t layer_id, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                    uint8_t alpha = FB_LAYER_ALPHA_OPAQUE,
                    uint16_t hints = static_cast<uint16_t>(FB_LAYER_HINT_OPAQUE_CONTENT |
                                                           FB_LAYER_HINT_FREQUENT_UPDATES)) {
        close();
        if (x1 <= x0 || y1 <= y0)
            return false;

        fb_layer_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.size = sizeof(cfg);
        cfg.x0 = x0;
        cfg.y0 = y0;
        cfg.x1 = x1;
        cfg.y1 = y1;
        cfg.alpha = alpha;
        cfg.hints = hints;
        cfg.stride = static_cast<uint16_t>((x1 - x0) * 4);

        fb_layer_info_t info;
        memset(&info, 0, sizeof(info));
        if (sys_5ht_req_buf(layer_id, &cfg, &info) != 0)
            return false;

        uint32_t w = static_cast<uint32_t>(x1 - x0);
        uint32_t h = static_cast<uint32_t>(y1 - y0);
        surface_ = Surface(reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(info.fb_user_va)), w,
                           w, h);
        meta_ =
            reinterpret_cast<volatile fb_layer_metadata_t *>(static_cast<uintptr_t>(info.metadata_user_va));
        layer_id_ = layer_id;
        owns_layer_ = true;
        reset_dirty_tracking();
        return surface_.valid() && meta_;
    }

    void close() {
        if (owns_layer_ && layer_id_)
            sys_5ht_rel_buf(layer_id_);
        owns_layer_ = false;
        layer_id_ = 0;
        meta_ = nullptr;
        surface_ = Surface();
        dirty_valid_ = false;
    }

    Surface &surface() { return surface_; }
    const Surface &surface() const { return surface_; }

    volatile fb_layer_metadata_t *meta() { return meta_; }

    /** Expand the dirty rectangle to include the given pixel rectangle (layer-local). */
    void damage(int x0, int y0, int x1, int y1) {
        if (x1 <= x0 || y1 <= y0)
            return;
        if (!dirty_valid_) {
            d0_ = static_cast<uint16_t>(x0);
            d1_ = static_cast<uint16_t>(y0);
            d2_ = static_cast<uint16_t>(x1);
            d3_ = static_cast<uint16_t>(y1);
            dirty_valid_ = true;
            return;
        }
        if (x0 < d0_)
            d0_ = static_cast<uint16_t>(x0);
        if (y0 < d1_)
            d1_ = static_cast<uint16_t>(y0);
        if (x1 > d2_)
            d2_ = static_cast<uint16_t>(x1);
        if (y1 > d3_)
            d3_ = static_cast<uint16_t>(y1);
    }

    /** Mark the full surface dirty. */
    void damage_all() {
        damage(0, 0, static_cast<int>(surface_.width()), static_cast<int>(surface_.height()));
    }

    /**
     * Publish the current frame. If no damage was recorded, submits the whole
     * surface bounds.
     */
    void present() {
        if (!meta_ || !surface_.valid())
            return;
        if (!dirty_valid_)
            damage_all();
        if (d0_ >= d2_ || d1_ >= d3_)
            return;

        uint16_t dx0 = d0_;
        uint16_t dy0 = d1_;
        uint16_t dx1 = d2_;
        uint16_t dy1 = d3_;
        const uint16_t sw = static_cast<uint16_t>(surface_.width());
        const uint16_t sh = static_cast<uint16_t>(surface_.height());
        if (dx1 > sw)
            dx1 = sw;
        if (dy1 > sh)
            dy1 = sh;
        if (dx0 >= dx1 || dy0 >= dy1)
            return;

        /* Match wm_submit_frame: widen dirty if compositor has not cleared yet. */
        if (meta_->ready) {
            if (dx0 > meta_->dx0)
                dx0 = meta_->dx0;
            if (dy0 > meta_->dy0)
                dy0 = meta_->dy0;
            if (dx1 < meta_->dx1)
                dx1 = meta_->dx1;
            if (dy1 < meta_->dy1)
                dy1 = meta_->dy1;
        }

        meta_->dx0 = dx0;
        meta_->dy0 = dy0;
        meta_->dx1 = dx1;
        meta_->dy1 = dy1;
        meta_->frame_id++;
        meta_->ready = 1;
        reset_dirty_tracking();
    }

    /** Block until compositor clears `ready` (matches `fb_layer_test.c`). */
    void wait_until_presented() const {
        while (meta_ && meta_->ready)
            pause();
    }

    /**
     * Apply `SG_GUI_EV_CONFIGURE` from WM: `sys_5ht_rcfg_layer` in the GUI
     * process (caller must own the layer).
     */
    bool apply_configure_event(const Event &ev) {
        if (ev.kind != Event::k_configure || !layer_id_)
            return false;
        if (ev.configure.x1 <= ev.configure.x0 || ev.configure.y1 <= ev.configure.y0)
            return false;

        fb_layer_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.size = sizeof(cfg);
        cfg.x0 = ev.configure.x0;
        cfg.y0 = ev.configure.y0;
        cfg.x1 = ev.configure.x1;
        cfg.y1 = ev.configure.y1;
        cfg.alpha = FB_LAYER_ALPHA_OPAQUE;
        cfg.hints = static_cast<uint16_t>(FB_LAYER_HINT_OPAQUE_CONTENT | FB_LAYER_HINT_FREQUENT_UPDATES);
        cfg.stride = static_cast<uint16_t>((cfg.x1 - cfg.x0) * 4);

        fb_layer_info_t info;
        memset(&info, 0, sizeof(info));
        if (sys_5ht_rcfg_layer(layer_id_, &cfg, &info) != 0)
            return false;

        uint32_t w = static_cast<uint32_t>(cfg.x1 - cfg.x0);
        uint32_t h = static_cast<uint32_t>(cfg.y1 - cfg.y0);
        surface_ = Surface(reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(info.fb_user_va)), w, w, h);
        meta_ =
            reinterpret_cast<volatile fb_layer_metadata_t *>(static_cast<uintptr_t>(info.metadata_user_va));
        owns_layer_ = true;
        reset_dirty_tracking();
        return surface_.valid() && meta_;
    }

    /** WM changed compositor z-slot; same SHM — only update syscall layer id. */
    bool apply_layer_event(const Event &ev) {
        if (ev.kind != Event::k_layer || ev.layer.layer_id == 0)
            return false;
        layer_id_ = ev.layer.layer_id;
        return true;
    }

    /**
     * WM-spawned GUI: read `SEROTONIN_GUI_LAYER_ID` and screen edges, then
     * `sys_5ht_req_buf` in this process so the layer survives `execve`.
     */
    bool attach_layer_from_wm_environment() {
        close();
        const char *el = getenv(env_name::k_layer_id);
        const char *ex0 = getenv(env_name::k_x0);
        const char *ey0 = getenv(env_name::k_y0);
        const char *ex1 = getenv(env_name::k_x1);
        const char *ey1 = getenv(env_name::k_y1);
        if (!el || !ex0 || !ey0 || !ex1 || !ey1)
            return false;

        uint32_t lid = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        if (!parse_uint(el, &lid) || !parse_uint(ex0, &x0) || !parse_uint(ey0, &y0) ||
            !parse_uint(ex1, &x1) || !parse_uint(ey1, &y1))
            return false;
        if (x1 <= x0 || y1 <= y0 || lid == 0 || lid > 65535)
            return false;

        return open_layer(static_cast<uint16_t>(lid), static_cast<uint16_t>(x0), static_cast<uint16_t>(y0),
                          static_cast<uint16_t>(x1), static_cast<uint16_t>(y1));
    }

    /** Events fd: `SEROTONIN_GUI_EVENTS_FD` or `SG_GUI_EVENTS_FD`. */
    static int events_fd_from_environment() {
        const char *es = getenv(env_name::k_events_fd);
        if (es && *es) {
            uint32_t v = 0;
            if (parse_uint(es, &v) && v < 256u)
                return static_cast<int>(v);
        }
        return SG_GUI_EVENTS_FD;
    }

private:
    void reset_dirty_tracking() {
        dirty_valid_ = false;
        d0_ = d1_ = d2_ = d3_ = 0;
    }

    Surface surface_;
    volatile fb_layer_metadata_t *meta_;
    uint16_t layer_id_;
    bool owns_layer_;
    bool dirty_valid_;
    uint16_t d0_, d1_, d2_, d3_;
};

} // namespace gooey

#endif /* GOOEY_H */
