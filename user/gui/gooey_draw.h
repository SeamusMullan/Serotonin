/**
 * @file gooey_draw.h
 * @brief Shape and text drawing primitives for gooey::Surface.
 *
 * Header-only. All functions operate on a gooey::Surface reference.
 * Font data from ../wm/wm_font.h (8x16 bitmap, regular + bold).
 *
 * Shapes: line, rect, circle, ellipse, triangle, ngon (outline + filled).
 * Text:   draw_char, draw_text, text_width.
 */

#ifndef GOOEY_DRAW_H
#define GOOEY_DRAW_H

#include "gooey.h"
#include "../wm/wm_font.h"

namespace gooey {
namespace draw {

static const int FONT_W = 8;
static const int FONT_H = 16;

/* ── Helpers ──────────────────────────────────────────────── */

inline int min2(int a, int b) { return a < b ? a : b; }
inline int max2(int a, int b) { return a > b ? a : b; }
inline int min3(int a, int b, int c) { return min2(a, min2(b, c)); }
inline int max3(int a, int b, int c) { return max2(a, max2(b, c)); }
inline int abs_i(int v) { return v < 0 ? -v : v; }

inline void swap_i(int &a, int &b) {
    int t = a;
    a = b;
    b = t;
}

/* ── Horizontal / Vertical line (clipped, fast) ──────────── */

inline void hline(Surface &s, int x0, int x1, int y, uint32_t color) {
    if (y < 0 || y >= static_cast<int>(s.height()))
        return;
    if (x0 > x1) swap_i(x0, x1);
    if (x0 < 0) x0 = 0;
    if (x1 >= static_cast<int>(s.width())) x1 = static_cast<int>(s.width()) - 1;
    if (x0 > x1) return;
    uint32_t *row = s.pixels() + static_cast<uint32_t>(y) * s.stride_px();
    for (int x = x0; x <= x1; ++x)
        row[x] = color;
}

inline void vline(Surface &s, int x, int y0, int y1, uint32_t color) {
    if (x < 0 || x >= static_cast<int>(s.width()))
        return;
    if (y0 > y1) swap_i(y0, y1);
    if (y0 < 0) y0 = 0;
    if (y1 >= static_cast<int>(s.height())) y1 = static_cast<int>(s.height()) - 1;
    if (y0 > y1) return;
    for (int y = y0; y <= y1; ++y)
        s.pixels()[static_cast<uint32_t>(y) * s.stride_px() + static_cast<uint32_t>(x)] = color;
}

/* ── Line (Bresenham) ────────────────────────────────────── */

inline void line(Surface &s, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs_i(x1 - x0);
    int dy = -abs_i(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        s.put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ── Rectangle ───────────────────────────────────────────── */

inline void rect(Surface &s, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    hline(s, x, x + w - 1, y, color);
    hline(s, x, x + w - 1, y + h - 1, color);
    vline(s, x, y, y + h - 1, color);
    vline(s, x + w - 1, y, y + h - 1, color);
}

inline void rect_filled(Surface &s, int x, int y, int w, int h, uint32_t color) {
    s.fill_rect(x, y, w, h, color);
}

/* ── Rounded Rectangle ───────────────────────────────────── */

inline void rect_rounded(Surface &s, int x, int y, int w, int h, int r, uint32_t color) {
    if (w <= 0 || h <= 0 || r < 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    hline(s, x + r, x + w - 1 - r, y, color);
    hline(s, x + r, x + w - 1 - r, y + h - 1, color);
    vline(s, x, y + r, y + h - 1 - r, color);
    vline(s, x + w - 1, y + r, y + h - 1 - r, color);

    int cx1 = x + r, cy1 = y + r;
    int cx2 = x + w - 1 - r, cy2 = y + r;
    int cx3 = x + r, cy3 = y + h - 1 - r;
    int cx4 = x + w - 1 - r, cy4 = y + h - 1 - r;

    int px = 0, py = r;
    int d = 1 - r;
    while (px <= py) {
        s.put_pixel(cx2 + py, cy2 - px, color);
        s.put_pixel(cx2 + px, cy2 - py, color);
        s.put_pixel(cx1 - py, cy1 - px, color);
        s.put_pixel(cx1 - px, cy1 - py, color);
        s.put_pixel(cx4 + py, cy4 + px, color);
        s.put_pixel(cx4 + px, cy4 + py, color);
        s.put_pixel(cx3 - py, cy3 + px, color);
        s.put_pixel(cx3 - px, cy3 + py, color);
        px++;
        if (d < 0) {
            d += 2 * px + 1;
        } else {
            py--;
            d += 2 * (px - py) + 1;
        }
    }
}

inline void rect_rounded_filled(Surface &s, int x, int y, int w, int h, int r, uint32_t color) {
    if (w <= 0 || h <= 0 || r < 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    s.fill_rect(x + r, y, w - 2 * r, r, color);
    s.fill_rect(x, y + r, w, h - 2 * r, color);
    s.fill_rect(x + r, y + h - r, w - 2 * r, r, color);

    int cx1 = x + r, cy1 = y + r;
    // cppcheck-suppress unreadVariable
    int cx2 = x + w - 1 - r, cy2 = y + r;
    int cx3 = x + r, cy3 = y + h - 1 - r;
    // cppcheck-suppress unreadVariable
    int cx4 = x + w - 1 - r, cy4 = y + h - 1 - r;

    int px = 0, py = r;
    int d = 1 - r;
    while (px <= py) {
        hline(s, cx1 - py, cx2 + py, cy1 - px, color);
        hline(s, cx1 - px, cx2 + px, cy1 - py, color);
        hline(s, cx3 - py, cx4 + py, cy3 + px, color);
        hline(s, cx3 - px, cx4 + px, cy3 + py, color);
        px++;
        if (d < 0) {
            d += 2 * px + 1;
        } else {
            py--;
            d += 2 * (px - py) + 1;
        }
    }
}

/* ── Circle (midpoint algorithm) ─────────────────────────── */

inline void circle(Surface &s, int cx, int cy, int r, uint32_t color) {
    if (r <= 0) { s.put_pixel(cx, cy, color); return; }
    int x = 0, y = r;
    int d = 1 - r;
    while (x <= y) {
        s.put_pixel(cx + x, cy + y, color);
        s.put_pixel(cx - x, cy + y, color);
        s.put_pixel(cx + x, cy - y, color);
        s.put_pixel(cx - x, cy - y, color);
        s.put_pixel(cx + y, cy + x, color);
        s.put_pixel(cx - y, cy + x, color);
        s.put_pixel(cx + y, cy - x, color);
        s.put_pixel(cx - y, cy - x, color);
        x++;
        if (d < 0) {
            d += 2 * x + 1;
        } else {
            y--;
            d += 2 * (x - y) + 1;
        }
    }
}

inline void circle_filled(Surface &s, int cx, int cy, int r, uint32_t color) {
    if (r <= 0) { s.put_pixel(cx, cy, color); return; }
    int x = 0, y = r;
    int d = 1 - r;
    while (x <= y) {
        hline(s, cx - x, cx + x, cy + y, color);
        hline(s, cx - x, cx + x, cy - y, color);
        hline(s, cx - y, cx + y, cy + x, color);
        hline(s, cx - y, cx + y, cy - x, color);
        x++;
        if (d < 0) {
            d += 2 * x + 1;
        } else {
            y--;
            d += 2 * (x - y) + 1;
        }
    }
}

/* ── Ellipse (midpoint algorithm) ────────────────────────── */

inline void ellipse(Surface &s, int cx, int cy, int rx, int ry, uint32_t color) {
    if (rx <= 0 && ry <= 0) { s.put_pixel(cx, cy, color); return; }
    long rx2 = (long)rx * rx;
    long ry2 = (long)ry * ry;
    long x = 0, y = ry;
    long d1 = ry2 - rx2 * ry + rx2 / 4;

    while (ry2 * x < rx2 * y) {
        s.put_pixel(cx + (int)x, cy + (int)y, color);
        s.put_pixel(cx - (int)x, cy + (int)y, color);
        s.put_pixel(cx + (int)x, cy - (int)y, color);
        s.put_pixel(cx - (int)x, cy - (int)y, color);
        x++;
        if (d1 < 0) {
            d1 += ry2 * (2 * x + 1);
        } else {
            y--;
            d1 += ry2 * (2 * x + 1) - 2 * rx2 * y;
        }
    }

    long d2 = ry2 * (x * 2 + 1) * (x * 2 + 1) / 4 + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        s.put_pixel(cx + (int)x, cy + (int)y, color);
        s.put_pixel(cx - (int)x, cy + (int)y, color);
        s.put_pixel(cx + (int)x, cy - (int)y, color);
        s.put_pixel(cx - (int)x, cy - (int)y, color);
        y--;
        if (d2 > 0) {
            d2 -= rx2 * (2 * y + 1);
        } else {
            x++;
            d2 += ry2 * (2 * x + 1) - rx2 * (2 * y + 1);
        }
    }
}

inline void ellipse_filled(Surface &s, int cx, int cy, int rx, int ry, uint32_t color) {
    if (rx <= 0 && ry <= 0) { s.put_pixel(cx, cy, color); return; }
    long rx2 = (long)rx * rx;
    long ry2 = (long)ry * ry;
    long x = 0, y = ry;
    long d1 = ry2 - rx2 * ry + rx2 / 4;

    while (ry2 * x < rx2 * y) {
        hline(s, cx - (int)x, cx + (int)x, cy + (int)y, color);
        hline(s, cx - (int)x, cx + (int)x, cy - (int)y, color);
        x++;
        if (d1 < 0) {
            d1 += ry2 * (2 * x + 1);
        } else {
            y--;
            d1 += ry2 * (2 * x + 1) - 2 * rx2 * y;
        }
    }

    long d2 = ry2 * (x * 2 + 1) * (x * 2 + 1) / 4 + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        hline(s, cx - (int)x, cx + (int)x, cy + (int)y, color);
        hline(s, cx - (int)x, cx + (int)x, cy - (int)y, color);
        y--;
        if (d2 > 0) {
            d2 -= rx2 * (2 * y + 1);
        } else {
            x++;
            d2 += ry2 * (2 * x + 1) - rx2 * (2 * y + 1);
        }
    }
}

/* ── Triangle ────────────────────────────────────────────── */

inline void triangle(Surface &s, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    line(s, x0, y0, x1, y1, color);
    line(s, x1, y1, x2, y2, color);
    line(s, x2, y2, x0, y0, color);
}

inline void triangle_filled(Surface &s, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    if (y0 > y1) { swap_i(x0, x1); swap_i(y0, y1); }
    if (y0 > y2) { swap_i(x0, x2); swap_i(y0, y2); }
    if (y1 > y2) { swap_i(x1, x2); swap_i(y1, y2); }

    int total_h = y2 - y0;
    if (total_h == 0) {
        hline(s, min3(x0, x1, x2), max3(x0, x1, x2), y0, color);
        return;
    }

    for (int y = y0; y <= y2; ++y) {
        bool second_half = y > y1 || y1 == y0;
        int seg_h = second_half ? (y2 - y1) : (y1 - y0);
        if (seg_h == 0) seg_h = 1;

        int alpha = y - y0;
        int beta = second_half ? (y - y1) : (y - y0);

        int ax = x0 + (x2 - x0) * alpha / total_h;
        int bx;
        if (second_half)
            bx = x1 + (x2 - x1) * beta / seg_h;
        else
            bx = x0 + (x1 - x0) * beta / seg_h;

        if (ax > bx) swap_i(ax, bx);
        hline(s, ax, bx, y, color);
    }
}

/* ── N-gon (regular polygon) ─────────────────────────────── */

/* Fixed-point sin/cos table: 256 entries for full circle.
 * Values scaled by 1024 (10-bit fraction). */

namespace detail {

static const int SIN_SCALE = 1024;
static const int SIN_TABLE_SIZE = 256;

static const int16_t sin_table[256] = {
       0,   25,   50,   75,  100,  125,  150,  175,
     200,  224,  249,  273,  297,  321,  345,  369,
     392,  415,  438,  460,  483,  505,  526,  548,
     569,  590,  610,  630,  650,  669,  688,  707,
     724,  742,  759,  775,  792,  807,  822,  837,
     851,  865,  878,  891,  903,  915,  926,  936,
     946,  955,  964,  972,  980,  987,  993,  999,
    1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009,
    1004,  999,  993,  987,  980,  972,  964,  955,
     946,  936,  926,  915,  903,  891,  878,  865,
     851,  837,  822,  807,  792,  775,  759,  742,
     724,  707,  688,  669,  650,  630,  610,  590,
     569,  548,  526,  505,  483,  460,  438,  415,
     392,  369,  345,  321,  297,  273,  249,  224,
     200,  175,  150,  125,  100,   75,   50,   25,
       0,  -25,  -50,  -75, -100, -125, -150, -175,
    -200, -224, -249, -273, -297, -321, -345, -369,
    -392, -415, -438, -460, -483, -505, -526, -548,
    -569, -590, -610, -630, -650, -669, -688, -707,
    -724, -742, -759, -775, -792, -807, -822, -837,
    -851, -865, -878, -891, -903, -915, -926, -936,
    -946, -955, -964, -972, -980, -987, -993, -999,
   -1004,-1009,-1013,-1016,-1019,-1021,-1023,-1024,
   -1024,-1024,-1023,-1021,-1019,-1016,-1013,-1009,
   -1004, -999, -993, -987, -980, -972, -964, -955,
    -946, -936, -926, -915, -903, -891, -878, -865,
    -851, -837, -822, -807, -792, -775, -759, -742,
    -724, -707, -688, -669, -650, -630, -610, -590,
    -569, -548, -526, -505, -483, -460, -438, -415,
    -392, -369, -345, -321, -297, -273, -249, -224,
    -200, -175, -150, -125, -100,  -75,  -50,  -25,
};

inline int isin(int angle_256) {
    return sin_table[angle_256 & 0xFF];
}

inline int icos(int angle_256) {
    return sin_table[(angle_256 + 64) & 0xFF];
}

} // namespace detail

static const int NGON_MAX_SIDES = 32;

inline void ngon(Surface &s, int cx, int cy, int r, int sides, uint32_t color) {
    if (sides < 3) sides = 3;
    if (sides > NGON_MAX_SIDES) sides = NGON_MAX_SIDES;

    int prev_x = cx + (r * detail::icos(0)) / detail::SIN_SCALE;
    int prev_y = cy + (r * detail::isin(0)) / detail::SIN_SCALE;

    for (int i = 1; i <= sides; ++i) {
        int angle = (i * detail::SIN_TABLE_SIZE) / sides;
        int nx = cx + (r * detail::icos(angle)) / detail::SIN_SCALE;
        int ny = cy + (r * detail::isin(angle)) / detail::SIN_SCALE;
        line(s, prev_x, prev_y, nx, ny, color);
        prev_x = nx;
        prev_y = ny;
    }
}

inline void ngon_filled(Surface &s, int cx, int cy, int r, int sides, uint32_t color) {
    if (sides < 3) sides = 3;
    if (sides > NGON_MAX_SIDES) sides = NGON_MAX_SIDES;

    int vx[NGON_MAX_SIDES], vy[NGON_MAX_SIDES];
    int min_y = cy, max_y = cy;

    for (int i = 0; i < sides; ++i) {
        int angle = (i * detail::SIN_TABLE_SIZE) / sides;
        vx[i] = cx + (r * detail::icos(angle)) / detail::SIN_SCALE;
        vy[i] = cy + (r * detail::isin(angle)) / detail::SIN_SCALE;
        if (vy[i] < min_y) min_y = vy[i];
        if (vy[i] > max_y) max_y = vy[i];
    }

    for (int y = min_y; y <= max_y; ++y) {
        int nodes[NGON_MAX_SIDES * 2];
        int node_count = 0;
        int j = sides - 1;
        for (int i = 0; i < sides; ++i) {
            if ((vy[i] <= y && vy[j] > y) || (vy[j] <= y && vy[i] > y)) {
                int nx = vx[i] + (y - vy[i]) * (vx[j] - vx[i]) / (vy[j] - vy[i]);
                if (node_count < NGON_MAX_SIDES * 2)
                    nodes[node_count++] = nx;
            }
            j = i;
        }
        for (int i = 0; i < node_count - 1; ++i)
            for (int k = i + 1; k < node_count; ++k)
                if (nodes[i] > nodes[k]) swap_i(nodes[i], nodes[k]);
        for (int i = 0; i + 1 < node_count; i += 2)
            hline(s, nodes[i], nodes[i + 1], y, color);
    }
}

/* ── Text rendering (8x16 bitmap font) ──────────────────── */

namespace detail {

static FontGlyph *glyph_lut[256];
static FontGlyph *glyph_bold_lut[256];
static bool glyph_lut_ready = false;

inline void ensure_glyph_luts() {
    if (glyph_lut_ready) return;
    glyph_lut_ready = true;
    for (int i = 0; i < 256; ++i) {
        glyph_lut[i] = nullptr;
        glyph_bold_lut[i] = nullptr;
    }
    for (int i = 0; i < wm_font_glyph_count; ++i) {
        glyph_lut[wm_font[i].codepoint & 0xFF] = &wm_font[i];
        glyph_bold_lut[wm_font_bold[i].codepoint & 0xFF] = &wm_font_bold[i];
    }
}

} // namespace detail

inline void draw_char(Surface &s, int x, int y, char ch, uint32_t fg, uint32_t bg, bool bold = false) {
    detail::ensure_glyph_luts();
    uint8_t idx = static_cast<uint8_t>(ch);
    FontGlyph *g = bold ? detail::glyph_bold_lut[idx] : detail::glyph_lut[idx];
    if (!g) g = detail::glyph_lut[idx];

    if (g) {
        // cppcheck-suppress constVariable
        uint32_t colors[2] = { bg, fg };
        for (int row = 0; row < FONT_H; ++row) {
            uint8_t bits = g->data[row];
            for (int col = 0; col < FONT_W; ++col)
                s.put_pixel(x + col, y + row, colors[(bits >> (7 - col)) & 1]);
        }
    } else {
        s.fill_rect(x, y, FONT_W, FONT_H, bg);
    }
}

inline void draw_char_transparent(Surface &s, int x, int y, char ch, uint32_t fg, bool bold = false) {
    detail::ensure_glyph_luts();
    uint8_t idx = static_cast<uint8_t>(ch);
    FontGlyph *g = bold ? detail::glyph_bold_lut[idx] : detail::glyph_lut[idx];
    if (!g) g = detail::glyph_lut[idx];
    if (!g) return;

    for (int row = 0; row < FONT_H; ++row) {
        uint8_t bits = g->data[row];
        for (int col = 0; col < FONT_W; ++col)
            if ((bits >> (7 - col)) & 1)
                s.put_pixel(x + col, y + row, fg);
    }
}

inline void draw_text(Surface &s, int x, int y, const char *str, uint32_t fg, uint32_t bg, bool bold = false) {
    while (*str) {
        draw_char(s, x, y, *str, fg, bg, bold);
        x += FONT_W;
        ++str;
    }
}

inline void draw_text_transparent(Surface &s, int x, int y, const char *str, uint32_t fg, bool bold = false) {
    while (*str) {
        draw_char_transparent(s, x, y, *str, fg, bold);
        x += FONT_W;
        ++str;
    }
}

inline int text_width(const char *str) {
    int w = 0;
    while (*str) { w += FONT_W; ++str; }
    return w;
}

inline int text_width(const char *str, int len) {
    return len * FONT_W;
}

} // namespace draw
} // namespace gooey

#endif /* GOOEY_DRAW_H */
