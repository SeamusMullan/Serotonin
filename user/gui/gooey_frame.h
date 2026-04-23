/**
 * @file gooey_frame.h
 * @brief Client-drawn window chrome for gooey apps (title bar, borders, close affordance).
 *
 * Metrics and close-button placement match `user/wm/wm.h` so the window manager's
 * hit-testing (close, floating title drag, resize grip) stays aligned with pixels
 * drawn here.
 */

#ifndef GOOEY_FRAME_H
#define GOOEY_FRAME_H

#include "gooey_draw.h"

namespace gooey {
namespace frame {

/** Keep in sync with `TITLEBAR_H`, `BORDER_W`, `CLOSE_BTN_*` in `user/wm/wm.h`. */
static const int TITLEBAR_H = 20;
static const int BORDER_W = 2;
static const int CLOSE_BTN_W = 16;
static const int CLOSE_BTN_H = 16;

/** Fallback decoration colors (WM "Default"). Prefer `gooey_theme.h` + WM env / @c SG_GUI_EV_THEME . */
struct ChromeColors {
    uint32_t titlebar_bg;
    uint32_t titlebar_inactive;
    uint32_t titlebar_fg;
    uint32_t border_active;
    uint32_t border_inactive;
    uint32_t close_btn;
};

inline ChromeColors chrome_colors_wm_default() {
    ChromeColors c;
    c.titlebar_bg = rgba(122, 152, 255);
    c.titlebar_inactive = rgba(64, 64, 80);
    c.titlebar_fg = rgba(255, 255, 255);
    c.border_active = rgba(122, 152, 255);
    c.border_inactive = rgba(48, 48, 64);
    c.close_btn = rgba(255, 64, 64);
    return c;
}

/** Inner rectangle where application content should be laid out and mouse mapped. */
inline void content_bounds(int sw, int sh, int *ox, int *oy, int *cw, int *ch) {
    *ox = BORDER_W;
    *oy = TITLEBAR_H;
    *cw = sw - BORDER_W * 2;
    *ch = sh - TITLEBAR_H - BORDER_W;
    if (*cw < 0)
        *cw = 0;
    if (*ch < 0)
        *ch = 0;
}

inline bool mouse_in_content(int lx, int ly, int sw, int sh) {
    int ox, oy, cw, ch;
    content_bounds(sw, sh, &ox, &oy, &cw, &ch);
    return lx >= ox && ly >= oy && lx < ox + cw && ly < oy + ch;
}

/**
 * If @p in is a mouse event inside the content area, copy it to @p out with
 * layer-local coordinates translated to the content origin (0,0 = top-left of
 * client area). Otherwise returns false.
 */
inline bool peel_content_mouse(const Event &in, Event *out, int sw, int sh) {
    if (in.kind != Event::k_mouse || !out)
        return false;
    if (!mouse_in_content(in.mouse.lx, in.mouse.ly, sw, sh))
        return false;
    int ox, oy, cw, ch;
    content_bounds(sw, sh, &ox, &oy, &cw, &ch);
    *out = in;
    out->mouse.lx = static_cast<int16_t>(in.mouse.lx - ox);
    out->mouse.ly = static_cast<int16_t>(in.mouse.ly - oy);
    if (out->mouse.lx < 0)
        out->mouse.lx = 0;
    if (out->mouse.ly < 0)
        out->mouse.ly = 0;
    if (out->mouse.lx > static_cast<int16_t>(cw - 1))
        out->mouse.lx = static_cast<int16_t>(cw > 0 ? cw - 1 : 0);
    if (out->mouse.ly > static_cast<int16_t>(ch - 1))
        out->mouse.ly = static_cast<int16_t>(ch > 0 ? ch - 1 : 0);
    return true;
}

inline void paint(Surface &s, int sw, int sh, const char *title, bool focused, const ChromeColors &cc) {
    if (!s.valid() || sw <= 0 || sh <= 0)
        return;
    if (!title)
        title = "";

    uint32_t tb = focused ? cc.titlebar_bg : cc.titlebar_inactive;
    uint32_t border = focused ? cc.border_active : cc.border_inactive;

    draw::rect_filled(s, 0, 0, sw, TITLEBAR_H, tb);

    int text_x = BORDER_W + 4;
    int text_y = (TITLEBAR_H - draw::FONT_H) / 2;
    if (text_y < 0)
        text_y = 0;

    int close_x = sw - BORDER_W - CLOSE_BTN_W;
    int close_y = (TITLEBAR_H - CLOSE_BTN_H) / 2;
    if (close_x < text_x)
        close_x = text_x;
    int title_max_w = close_x - text_x - 6;
    if (title_max_w < draw::FONT_W)
        title_max_w = draw::FONT_W;

    const char *t = title;
    size_t n = std::strlen(title);
    char buf[160];
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    while (n > 0) {
        std::memcpy(buf, t, n);
        buf[n] = '\0';
        if (draw::text_width(buf) <= title_max_w)
            break;
        --n;
    }
    if (n > 0)
        draw::draw_text(s, text_x, text_y, buf, cc.titlebar_fg, tb);

    draw::rect_filled(s, close_x, close_y, CLOSE_BTN_W, CLOSE_BTN_H, cc.close_btn);
    draw::draw_char(s, close_x + 4, close_y + 0, 'x', rgba(255, 255, 255), cc.close_btn, true);

    draw::rect_filled(s, 0, TITLEBAR_H, BORDER_W, sh - TITLEBAR_H, border);
    draw::rect_filled(s, sw - BORDER_W, TITLEBAR_H, BORDER_W, sh - TITLEBAR_H, border);
    draw::rect_filled(s, 0, sh - BORDER_W, sw, BORDER_W, border);
}

} // namespace frame
} // namespace gooey

#endif /* GOOEY_FRAME_H */
