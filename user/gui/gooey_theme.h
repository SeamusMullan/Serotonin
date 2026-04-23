/**
 * @file gooey_theme.h
 * @brief Map WM palette (`SG_GUI_ENV_THEME` / @c SG_GUI_EV_THEME) to gooey widgets + frame chrome.
 */

#ifndef GOOEY_THEME_H
#define GOOEY_THEME_H

#include <cstdlib>
#include <cstring>

#include "gooey_frame.h"
#include "gooey_widgets.h"

namespace gooey {
namespace theme {

inline uint32_t lerp_rgb(uint32_t a, uint32_t b, uint8_t t) {
    if (t == 0)
        return (a & 0xFF000000u) | (a & 0x00FFFFFFu);
    if (t >= 255)
        return (b & 0xFF000000u) | (b & 0x00FFFFFFu);
    int r1 = (a >> 16) & 0xFF, g1 = (a >> 8) & 0xFF, b1 = a & 0xFF;
    int r2 = (b >> 16) & 0xFF, g2 = (b >> 8) & 0xFF, b2 = b & 0xFF;
    int r = r1 + ((r2 - r1) * t >> 8);
    int g = g1 + ((g2 - g1) * t >> 8);
    int bl = b1 + ((b2 - b1) * t >> 8);
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(bl);
}

/** @p c : @c SG_GUI_WM_THEME_NCOLORS ARGB values in @c sg_gui_wm_theme_colors_t field order. */
inline void map_wm_palette(const uint32_t c[SG_GUI_WM_THEME_NCOLORS], widgets::Theme *th,
                           frame::ChromeColors *cc) {
    if (!c)
        return;
    if (th) {
        th->bg = c[1];
        th->fg = c[4];
        th->accent = c[2];
        th->accent_hover = lerp_rgb(c[2], 0xFFFFFFFFu, 48);
        th->accent_pressed = c[3];
        th->border = c[6];
        th->border_focused = c[11];
        th->input_bg = c[0];
        th->input_fg = c[4];
        th->disabled_fg = c[5];
        th->disabled_bg = c[1];
        th->selection = c[2];
        th->scrollbar_bg = c[0];
        th->scrollbar_thumb = c[5];
    }
    if (cc) {
        cc->titlebar_bg = c[9];
        cc->titlebar_inactive = c[10];
        cc->titlebar_fg = c[8];
        cc->border_active = c[11];
        cc->border_inactive = c[12];
        cc->close_btn = c[7];
    }
}

inline bool parse_theme_env_value(const char *digits, uint32_t out[SG_GUI_WM_THEME_NCOLORS]) {
    if (!digits || !out)
        return false;
    size_t len = std::strlen(digits);
    if (len != static_cast<size_t>(SG_GUI_WM_THEME_NCOLORS) * 8u)
        return false;
    for (unsigned i = 0; i < SG_GUI_WM_THEME_NCOLORS; ++i) {
        char chunk[9];
        std::memcpy(chunk, digits + i * 8u, 8u);
        chunk[8] = '\0';
        char *end = nullptr;
        unsigned long v = std::strtoul(chunk, &end, 16);
        if (end != chunk + 8)
            return false;
        out[i] = static_cast<uint32_t>(v);
    }
    return true;
}

/** If @c SG_GUI_ENV_THEME set and valid, overwrite @p th / @p cc (non-null only). */
inline bool sync_from_wm_environment(widgets::Theme *th, frame::ChromeColors *cc) {
    const char *s = std::getenv(SG_GUI_ENV_THEME);
    if (!s || !*s)
        return false;
    uint32_t buf[SG_GUI_WM_THEME_NCOLORS];
    if (!parse_theme_env_value(s, buf))
        return false;
    map_wm_palette(buf, th, cc);
    return true;
}

inline bool apply_gui_event(const Event &ev, widgets::Theme *th, frame::ChromeColors *cc) {
    if (ev.kind != Event::k_theme)
        return false;
    map_wm_palette(ev.theme.wm, th, cc);
    return true;
}

} // namespace theme
} // namespace gooey

#endif /* GOOEY_THEME_H */
