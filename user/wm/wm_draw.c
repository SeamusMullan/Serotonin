#include "wm.h"
#include "wm_font.h"
#include <emmintrin.h>

/* --- Direct ASCII glyph lookup tables (O(1) instead of O(n) scan) --- */

static FontGlyph *glyph_lut[256];
static FontGlyph *glyph_bold_lut[256];
static int lut_init_done;

static void ensure_glyph_luts(void) {
    if (lut_init_done) return;
    lut_init_done = 1;
    for (int i = 0; i < 256; i++) {
        glyph_lut[i] = NULL;
        glyph_bold_lut[i] = NULL;
    }
    for (int i = 0; i < wm_font_glyph_count; i++) {
        glyph_lut[wm_font[i].codepoint & 0xFF] = &wm_font[i];
        glyph_bold_lut[wm_font_bold[i].codepoint & 0xFF] = &wm_font_bold[i];
    }
}

/* --- SSE2 memcpy (forward, no overlap) --- */

void sse2_copy_fwd(uint32_t *dst, const uint32_t *src, uint32_t count) {
    /* count is in uint32_t elements */
    uint32_t i = 0;

    /* SSE2 bulk: 16 pixels per iteration */
    uint32_t bulk_end = count & ~15u;
    for (; i < bulk_end; i += 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(src + i));
        __m128i b = _mm_loadu_si128((const __m128i *)(src + i + 4));
        __m128i c = _mm_loadu_si128((const __m128i *)(src + i + 8));
        __m128i d = _mm_loadu_si128((const __m128i *)(src + i + 12));
        _mm_storeu_si128((__m128i *)(dst + i), a);
        _mm_storeu_si128((__m128i *)(dst + i + 4), b);
        _mm_storeu_si128((__m128i *)(dst + i + 8), c);
        _mm_storeu_si128((__m128i *)(dst + i + 12), d);
    }
    /* Tail */
    for (; i < count; i++)
        dst[i] = src[i];
}

/* --- SSE2 memcpy (backward, for overlapping dst > src) --- */

void sse2_copy_bwd(uint32_t *dst, const uint32_t *src, uint32_t count) {
    uint32_t i = count;

    /* Bulk backward: 16 pixels per iteration */
    while (i >= 16) {
        i -= 16;
        __m128i a = _mm_loadu_si128((const __m128i *)(src + i));
        __m128i b = _mm_loadu_si128((const __m128i *)(src + i + 4));
        __m128i c = _mm_loadu_si128((const __m128i *)(src + i + 8));
        __m128i d = _mm_loadu_si128((const __m128i *)(src + i + 12));
        _mm_storeu_si128((__m128i *)(dst + i), a);
        _mm_storeu_si128((__m128i *)(dst + i + 4), b);
        _mm_storeu_si128((__m128i *)(dst + i + 8), c);
        _mm_storeu_si128((__m128i *)(dst + i + 12), d);
    }
    /* Tail */
    while (i > 0) {
        i--;
        dst[i] = src[i];
    }
}

/* --- SSE2 fill: 4 pixels (16 bytes) per store --- */

void draw_fill_rect(uint32_t *fb, uint32_t stride_px, int x, int y,
                    int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    __m128i vcolor = _mm_set1_epi32((int)color);

    for (int row = 0; row < h; row++) {
        uint32_t *p = fb + (y + row) * stride_px + x;
        int col = 0;

        /* Scalar head: align to 16-byte boundary */
        while (col < w && ((uintptr_t)(p + col) & 15)) {
            p[col] = color;
            col++;
        }

        /* SSE2 bulk: 16 pixels (64 bytes) per iteration */
        int sse_end = w - 15;
        while (col < sse_end) {
            _mm_store_si128((__m128i *)(p + col),      vcolor);
            _mm_store_si128((__m128i *)(p + col + 4),  vcolor);
            _mm_store_si128((__m128i *)(p + col + 8),  vcolor);
            _mm_store_si128((__m128i *)(p + col + 12), vcolor);
            col += 16;
        }
        /* SSE2 tail: 4 pixels at a time */
        int sse_end4 = w - 3;
        while (col < sse_end4) {
            _mm_store_si128((__m128i *)(p + col), vcolor);
            col += 4;
        }

        /* Scalar tail */
        while (col < w)
            p[col++] = color;
    }
}

/* --- Branchless glyph: table lookup fg/bg per pixel, pure scalar stores ---
 *
 * On i686 with BORDER_W=2, glyph pixel addresses are 8-byte aligned but never
 * 16-byte aligned, so unaligned SSE stores cross cache lines and are SLOWER
 * than 8 simple aligned 4-byte stores.  The lookup-table approach is also
 * branchless (no mispredictions on the bit test).
 */

void draw_glyph(uint32_t *fb, uint32_t stride_px, int x, int y,
                const uint8_t *data, uint32_t fg, uint32_t bg) {
    // cppcheck-suppress constVariable
    uint32_t colors[2] = { bg, fg };

    for (int row = 0; row < FONT_H; row++) {
        uint32_t bits = data[row];
        uint32_t *p = fb + (y + row) * stride_px + x;
        p[0] = colors[(bits >> 7) & 1];
        p[1] = colors[(bits >> 6) & 1];
        p[2] = colors[(bits >> 5) & 1];
        p[3] = colors[(bits >> 4) & 1];
        p[4] = colors[(bits >> 3) & 1];
        p[5] = colors[(bits >> 2) & 1];
        p[6] = colors[(bits >> 1) & 1];
        p[7] = colors[bits & 1];
    }
}

void draw_char(uint32_t *fb, uint32_t stride_px, int x, int y,
               char ch, uint8_t bold, uint32_t fg, uint32_t bg) {
    ensure_glyph_luts();
    uint8_t idx = (uint8_t)ch;
    // cppcheck-suppress constVariablePointer
    FontGlyph *g = bold ? glyph_bold_lut[idx] : glyph_lut[idx];
    if (!g)
        g = glyph_lut[idx];
    if (g)
        draw_glyph(fb, stride_px, x, y, g->data, fg, bg);
    else
        draw_fill_rect(fb, stride_px, x, y, FONT_W, FONT_H, bg);
}

void fb_set_alpha(uint32_t *fb, uint32_t pixel_count, uint8_t alpha) {
    uint32_t aval = (uint32_t)alpha << 24;
    __m128i mask = _mm_set1_epi32((int)0x00FFFFFF);
    __m128i new_a = _mm_set1_epi32((int)aval);

    uint32_t i = 0;
    uint32_t bulk = pixel_count & ~15u;
    for (; i < bulk; i += 16) {
        __m128i p0 = _mm_loadu_si128((__m128i *)(fb + i));
        __m128i p1 = _mm_loadu_si128((__m128i *)(fb + i + 4));
        __m128i p2 = _mm_loadu_si128((__m128i *)(fb + i + 8));
        __m128i p3 = _mm_loadu_si128((__m128i *)(fb + i + 12));
        p0 = _mm_or_si128(_mm_and_si128(p0, mask), new_a);
        p1 = _mm_or_si128(_mm_and_si128(p1, mask), new_a);
        p2 = _mm_or_si128(_mm_and_si128(p2, mask), new_a);
        p3 = _mm_or_si128(_mm_and_si128(p3, mask), new_a);
        _mm_storeu_si128((__m128i *)(fb + i), p0);
        _mm_storeu_si128((__m128i *)(fb + i + 4), p1);
        _mm_storeu_si128((__m128i *)(fb + i + 8), p2);
        _mm_storeu_si128((__m128i *)(fb + i + 12), p3);
    }
    for (; i < pixel_count; i++)
        fb[i] = (fb[i] & 0x00FFFFFF) | aval;
}

void draw_text(uint32_t *fb, uint32_t stride_px, int x, int y,
               const char *str, uint32_t fg, uint32_t bg) {
    ensure_glyph_luts();
    while (*str) {
        uint8_t idx = (uint8_t)*str;
        // cppcheck-suppress constVariablePointer
        FontGlyph *g = glyph_lut[idx];
        if (g)
            draw_glyph(fb, stride_px, x, y, g->data, fg, bg);
        else
            draw_fill_rect(fb, stride_px, x, y, FONT_W, FONT_H, bg);
        x += FONT_W;
        str++;
    }
}
