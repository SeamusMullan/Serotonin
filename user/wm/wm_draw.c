#include "wm.h"
#include "wm_font.h"

void draw_fill_rect(uint32_t *fb, uint32_t stride_px, int x, int y,
                    int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; row++) {
        uint32_t *p = fb + (y + row) * stride_px + x;
        for (int col = 0; col < w; col++)
            p[col] = color;
    }
}

void draw_glyph(uint32_t *fb, uint32_t stride_px, int x, int y,
                const uint8_t *data, uint32_t fg, uint32_t bg) {
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = data[row];
        uint32_t *p = fb + (y + row) * stride_px + x;
        for (int col = 0; col < FONT_W; col++) {
            p[col] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

void draw_char(uint32_t *fb, uint32_t stride_px, int x, int y,
               char ch, uint8_t bold, uint32_t fg, uint32_t bg) {
    FontGlyph *g = bold ? wm_find_glyph_bold((uint16_t)(unsigned char)ch)
                        : wm_find_glyph((uint16_t)(unsigned char)ch);
    if (!g)
        g = wm_find_glyph((uint16_t)(unsigned char)ch);
    if (g)
        draw_glyph(fb, stride_px, x, y, g->data, fg, bg);
    else
        draw_fill_rect(fb, stride_px, x, y, FONT_W, FONT_H, bg);
}

void draw_text(uint32_t *fb, uint32_t stride_px, int x, int y,
               const char *str, uint32_t fg, uint32_t bg) {
    while (*str) {
        draw_char(fb, stride_px, x, y, *str, 0, fg, bg);
        x += FONT_W;
        str++;
    }
}
