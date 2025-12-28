/*
    vbe.c
    VESA BIOS Extensions (VBE) Graphics Driver for Serotonin.
*/

#include "vbe.h"
#include "../../kernel.h"
#include "../../stdlib/stdlib.h"
#include "../../schedule/schedule.h"
#include <stdint.h>
#include <stddef.h>
#include "../../multiboot.h"
#include "../../vmm/paging_init.h"
#include "../font.h"

#include <xmmintrin.h>
#include <emmintrin.h>

uint32_t term_fg_color = 0xFFFFFFFF;
uint32_t term_bg_color = 0xFF000000;
uint32_t dirty_min_x = 0;
uint32_t dirty_min_y = 0;
uint32_t dirty_max_x = 0;
uint32_t dirty_max_y = 0;
static int vbe_any_dirty = 0;
uint8_t dirty_lines[SCREEN_HEIGHT];
vbe_mode_info_t vbe_info;
uint32_t fb_size_bytes;
uint32_t vbe_palette[256];
uint8_t init_z = 0;
process_control_block_t *vbe_worker_task;

static const uint16_t k255w[8] __attribute__((aligned(16))) = {0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF};
static const uint16_t k257w[8] __attribute__((aligned(16))) = {0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101};
static const uint16_t k254w[8] __attribute__((aligned(16))) = {0x00FE, 0x00FE, 0x00FE, 0x00FE, 0x00FE, 0x00FE, 0x00FE, 0x00FE};
static const uint32_t kAlphaFF[4] __attribute__((aligned(16))) = {0xFF000000u, 0xFF000000u, 0xFF000000u, 0xFF000000u};
static vbe_z_layer_t *vbe_z_layers[VBE_NUM_Z_LAYERS];
static inline int vbe_z_valid(uint8_t z) { return (z > 0 && z < VBE_NUM_Z_LAYERS); }

#define DIRTY_BITMAP_SIZE ((SCREEN_WIDTH * SCREEN_HEIGHT + 7) / 8)
uint8_t dirty_bitmap[DIRTY_BITMAP_SIZE];

uint32_t vbe_colors[16] = {
    0x00000000, // BLACK
    0x000000AA, // BLUE
    0x0000AA00, // GREEN
    0x0000AAAA, // CYAN
    0x00AA0000, // RED
    0x00AA00AA, // MAGENTA
    0x00AA5500, // BROWN / YELLOW
    0x00AAAAAA, // LIGHT GRAY
    0x00555555, // DARK GRAY
    0x005555FF, // LIGHT BLUE
    0x0055FF55, // LIGHT GREEN
    0x0055FFFF, // LIGHT CYAN
    0x00FF5555, // LIGHT RED
    0x00FF55FF, // LIGHT MAGENTA
    0x00FFFF00, // YELLOW / LIGHT YELLOW
    0x00FFFFFF  // WHITE
};

static uint32_t ansi_color_table[16] = {
    0x00000000, // black
    0x00FF0000, // red
    0x0000FF00, // green
    0x00FFFF00, // yellow
    0x000000FF, // blue
    0x00FF00FF, // magenta
    0x0000FFFF, // cyan
    0x00FFFFFF, // white
    0x00404040, // bright black (gray)
    0x00FF4040, // bright red
    0x0040FF40, // bright green
    0x00FFFF40, // bright yellow
    0x004040FF, // bright blue
    0x00FF40FF, // bright magenta
    0x0040FFFF, // bright cyan
    0x00FFFFFF  // bright white
};

static uint32_t ansi_fg = 0xFFFFFFFF;
static uint32_t ansi_bg = 0xFF000000;

// dirty bounding box used for rect dirty marking
dirty_bb_t *dbb;

/**
 * @brief Get the maximum number of terminal columns.
 *
 * @return The number of columns based on screen width and font width.
 */
static uint32_t term_max_cols(void)
{
    return vbe_info.width / VBE_FONT_WIDTH;
}

/**
 * @brief Get the maximum number of terminal rows.
 *
 * @return The number of rows based on screen height and font height.
 */
static uint32_t term_max_rows(void)
{
    return vbe_info.height / VBE_FONT_HEIGHT;
}

vbe_z_layer_t* vbe_create_z_layer(uint8_t z, uint8_t alpha, uint8_t active) {
    if (z >= VBE_NUM_Z_LAYERS)
        kernel_panic("vbe_create_z_layer: attempt to allocate more then allowed z layers");

    vbe_z_layer_t *z_layer = (vbe_z_layer_t*)kernel_malloc(sizeof(vbe_z_layer_t));
    z_layer->z = z;
    z_layer->alpha = alpha;
    z_layer->active = active;
    
    uint32_t *z_layer_buf = (uint32_t*)kernel_malloc_align(16, fb_size_bytes);
    z_layer->bufptr = z_layer_buf;
    memset(z_layer->bufptr, 0, fb_size_bytes);

    vbe_z_layers[z] = z_layer;
    init_z++;

    return z_layer;
}

/**
 * @brief Initialize the VBE (VESA BIOS Extensions) for graphics mode.
 *
 * This function sets up the VBE for use with the framebuffer.
 * Must be called *after* paging_init((uintptr_t)mbi->framebuffer_addr) has run.
 *
 * @param mbi The multiboot information structure.
 */
void vbe_init(multiboot_info_t *mbi)
{
    uint32_t phys_fb = (uint32_t)(mbi->framebuffer_addr);
    uint32_t pitch = (uint32_t)(mbi->framebuffer_pitch);
    uint32_t width = (uint32_t)(mbi->framebuffer_width);
    uint32_t height = (uint32_t)(mbi->framebuffer_height);
    uint32_t bpp = (uint32_t)(mbi->framebuffer_bpp);

    uint32_t *virt_fb = (uint32_t *)FB_VMA_BASE;
    if (!virt_fb)
    {
        // ?!?!?!
        kernel_panic("vbe_init: unable to get virtual framebuffer address");
    }

    vbe_info.framebuffer = virt_fb;
    vbe_info.width = width;
    vbe_info.height = height;
    vbe_info.pitch = pitch;
    vbe_info.bpp = bpp;

    fb_size_bytes = (uint32_t)height * pitch;
    vbe_info.backbuffer = (uint32_t *)kernel_malloc_align(16, fb_size_bytes);
    if (!vbe_info.backbuffer)
    {
        kernel_panic("vbe_init: could not allocate backbuffer");
    }

    memset(vbe_info.backbuffer, 0, fb_size_bytes);

    for (int i = 0; i < VBE_NUM_Z_LAYERS; i++) {
        vbe_create_z_layer(i, 0, 0);
        vbe_clear_all_z_layers();
    }

    // create dirty bounding box
    dbb = (dirty_bb_t*)kernel_malloc(sizeof(dirty_bb_t));
    dbb->x0 = (uint16_t)-1;
    dbb->y0 = (uint16_t)-1;
    dbb->x1 = (uint16_t)-1;
    dbb->y1 = (uint16_t)-1;
}

/**
 * @brief Mark pixel as dirty
 *
 * @param x X-coordinate.
 * @param y Y-coordinate.
 */
// static inline void vbe_mark_pixel_dirty(uint32_t x, uint32_t y) {
//     if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
//     dirty_lines[y] = 1;
// }

static inline void vbe_mark_pixel_dirty(uint16_t x, uint16_t y)
{
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;

    // check if outside existing bb and update the points to respect the new bounds

    if (dbb->x0 == (uint16_t)-1 ||
        dbb->x1 == (uint16_t)-1 ||
        dbb->y0 == (uint16_t)-1 ||
        dbb->y1 == (uint16_t)-1)
    {
        // dbb hasnt been used yet, assign start to curresnt pixel being marked dirty
        dbb->x0 = x;
        dbb->x1 = x+1;
        dbb->y0 = y;
        dbb->y1 = y+1;
    }
    else
    {
        if (x < dbb->x0) dbb->x0 = x;
        if (x >= dbb->x1) dbb->x1 = x + 1;

        if (y < dbb->y0) dbb->y0 = y;
        if (y >= dbb->y1) dbb->y1 = y + 1;
    }
}


inline void vbe_mark_region_dirty(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (x + w > vbe_info.width)
        w = vbe_info.width - x;
    if (y + h > vbe_info.height)
        h = vbe_info.height - y;
    
    if (dbb->x0 == (uint16_t)-1 ||
        dbb->x1 == (uint16_t)-1 ||
        dbb->y0 == (uint16_t)-1 ||
        dbb->y1 == (uint16_t)-1)
    {
        dbb->x0 = x;
        dbb->x1 = x+w;
        dbb->y0 = y;
        dbb->y1 = y+h;
    }
    else
    {
        if (x < dbb->x0) dbb->x0 = x;
        if (x >= dbb->x1) dbb->x1 = x + w;

        if (y < dbb->y0) dbb->y0 = y;
        if (y >= dbb->y1) dbb->y1 = y + h;
    }
}
 
/**
 * @brief Draw a pixel on the backbuffer.
 *
 * @param x The x-coordinate.
 * @param y The y-coordinate.
 * @param color The pixel color.
 */
void vbe_putpixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= vbe_info.width || y >= vbe_info.height)
        return;
    uint8_t *row_start = (uint8_t *)vbe_z_layers[0]->bufptr + (y * vbe_info.pitch);
    uint32_t *dest = (uint32_t *)(row_start + (x * 4));
    *dest = color;
    vbe_mark_pixel_dirty(x, y);
}

/**
 * @brief Draw a horizontal line on the given buffer.
 *
 * @param buf The destination buffer.
 * @param pitch The pitch (bytes per row).
 * @param x Starting x-coordinate.
 * @param y Y-coordinate.
 * @param w Width of the line.
 * @param color Line color.
 */
void vbe_fast_draw_hline(uint32_t *buf, uint32_t pitch, uint32_t x, uint32_t y, uint32_t w, uint32_t color)
{
    uint8_t *row = (uint8_t *)buf + y * pitch;
    uint32_t *dst = ((uint32_t *)row) + x;
    for (uint32_t i = 0; i < w; i++)
    {
        dst[i] = color;
        vbe_mark_pixel_dirty(x + i, y);
    }
}

/**
 * @brief Fill a rectangle on the backbuffer with a specific color.
 *
 * All pixels in the rectangle are marked dirty in the dirty bitmap.
 *
 * @param x Top-left x-coordinate of the rectangle.
 * @param y Top-left y-coordinate of the rectangle.
 * @param w Width of the rectangle.
 * @param h Height of the rectangle.
 * @param color The fill color.
 */
void vbe_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (x + w > vbe_info.width)
        w = vbe_info.width - x;
    if (y + h > vbe_info.height)
        h = vbe_info.height - y;
    for (uint32_t row = 0; row < h; row++)
    {
        vbe_fast_draw_hline(vbe_z_layers[0]->bufptr, vbe_info.pitch, x, y + row, w, color);
    }
    // vbe_fast_mark_dirty(x, y, w, h);
}

/**
 * @brief Quickly put a pixel in a buffer.
 *
 * The caller MUST assure validity of arguments.
 *
 * @param buf Buffer to draw to.
 * @param pitch Pitch in bytes.
 * @param width Width of screen.
 * @param height Height of screen.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param color Pixel color.
 */
inline void fast_putpixel(uint32_t *buf, uint32_t pitch, uint32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t color)
{
    uint8_t *row = (uint8_t *)buf + y * pitch;
    ((uint32_t *)row)[x] = color;
}

inline uint32_t div255(uint32_t p)
{
    p = p + ((p + 257u) >> 8);
    return p >> 8;
}

__attribute__((noinline, force_align_arg_pointer))
static void vbe_blend_area(uint32_t *dst, uint32_t *src, uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2) {
    if (!dst || !src || x2 <= x1 || y2 <= y1) return;

    const uint32_t w = x2 - x1;
    const uint32_t h = y2 - y1;

    const uint32_t pitch = vbe_info.pitch;
    const uint32_t stride = pitch / sizeof(uint32_t);

    const __m128i vz     = _mm_setzero_si128();
    const __m128i v255   = _mm_set1_epi16((short)255);
    const __m128i v255_32= _mm_set1_epi32(0x000000FF);
    const __m128i vrnd   = _mm_set1_epi32(128);
    const __m128i amsk   = _mm_set1_epi32(0xFF000000);
    const __m128i vones  = _mm_set1_epi32(-1);

    uint32_t *drow = dst + y1 * stride + x1;
    uint32_t *srow = src + y1 * stride + x1;

    for (uint32_t y = 0; y < h; y++) {
        uint32_t i = 0;

        // SIMD alpha blend
        for (; i + 4 <= w; i += 4) {
            // load 4 src/dst pixels, BBBB GGGG RRRR AAAA bytes per pixel, little endian
            __m128i spx = _mm_loadu_si128((const __m128i *)&srow[i]);
            __m128i dpx = _mm_loadu_si128((const __m128i *)&drow[i]);

            // extract alpha bytes
            __m128i a32 = _mm_srli_epi32(spx, 24);

            __m128i m_trans = _mm_cmpeq_epi32(a32, vz);        // alpha == 0
            __m128i m_opaque= _mm_cmpeq_epi32(a32, v255_32);   // alpha == 255

            // fast paths if all 4 lanes are the same
            int mt = _mm_movemask_epi8(m_trans);
            if (mt == 0xFFFF) {
                // transparent
                continue;
            }
            int mo = _mm_movemask_epi8(m_opaque);
            if (mo == 0xFFFF) {
                // opaque
                _mm_storeu_si128((__m128i *)&drow[i], spx);
                continue;
            }

            // widen to 16 bit: [b0 g0 r0 a0 b1 g1 r1 a1] / [b2 g2 r2 a2 b3 g3 r3 a3]
            __m128i s_lo = _mm_unpacklo_epi8(spx, vz);
            __m128i s_hi = _mm_unpackhi_epi8(spx, vz);
            __m128i d_lo = _mm_unpacklo_epi8(dpx, vz);
            __m128i d_hi = _mm_unpackhi_epi8(dpx, vz);

            // extract per pixel alpha bytes into words and replicate across channels
            // after shift: each dword is 0x000000AA, bytes layout [AA 00 00 00 ...]
            __m128i a    = _mm_srli_epi32(spx, 24);
            __m128i a_lo = _mm_unpacklo_epi8(a, vz); // [a0 0 0 0 a1 0 0 0] (as 16 bit words)
            __m128i a_hi = _mm_unpackhi_epi8(a, vz); // [a2 0 0 0 a3 0 0 0]

            // replicate word 0 across low 64b, and word 4 across high 64b (=> [a0 a0 a0 a0 a1 a1 a1 a1])
            a_lo = _mm_shufflelo_epi16(a_lo, 0x00);
            a_lo = _mm_shufflehi_epi16(a_lo, 0x00);
            a_hi = _mm_shufflelo_epi16(a_hi, 0x00);
            a_hi = _mm_shufflehi_epi16(a_hi, 0x00);

            // inv_a = 255 - a
            __m128i ia_lo = _mm_sub_epi16(v255, a_lo);
            __m128i ia_hi = _mm_sub_epi16(v255, a_hi);

            // products in 16 bit
            __m128i ps_lo = _mm_mullo_epi16(s_lo, a_lo);
            __m128i pd_lo = _mm_mullo_epi16(d_lo, ia_lo);
            __m128i ps_hi = _mm_mullo_epi16(s_hi, a_hi);
            __m128i pd_hi = _mm_mullo_epi16(d_hi, ia_hi);

            // widen to 32 bit before summing to avoid overflow
            __m128i sum0 = _mm_add_epi32(_mm_unpacklo_epi16(ps_lo, vz), _mm_unpacklo_epi16(pd_lo, vz));
            __m128i sum1 = _mm_add_epi32(_mm_unpackhi_epi16(ps_lo, vz), _mm_unpackhi_epi16(pd_lo, vz));
            __m128i sum2 = _mm_add_epi32(_mm_unpacklo_epi16(ps_hi, vz), _mm_unpacklo_epi16(pd_hi, vz));
            __m128i sum3 = _mm_add_epi32(_mm_unpackhi_epi16(ps_hi, vz), _mm_unpackhi_epi16(pd_hi, vz));

            // add rounding
            sum0 = _mm_add_epi32(sum0, vrnd);
            sum1 = _mm_add_epi32(sum1, vrnd);
            sum2 = _mm_add_epi32(sum2, vrnd);
            sum3 = _mm_add_epi32(sum3, vrnd);

            // >> 8
            sum0 = _mm_srli_epi32(sum0, 8);
            sum1 = _mm_srli_epi32(sum1, 8);
            sum2 = _mm_srli_epi32(sum2, 8);
            sum3 = _mm_srli_epi32(sum3, 8);

            // pack 32->16 then 16->8
            __m128i o_lo = _mm_packs_epi32(sum0, sum1);   // 8x16bit
            __m128i o_hi = _mm_packs_epi32(sum2, sum3);   // 8x16bit
            __m128i ob   = _mm_packus_epi16(o_lo, o_hi);  // 16x8bit, BGRA per pixel

            // overwrite alpha with src alpha
            __m128i rgb = _mm_andnot_si128(amsk, ob);
            __m128i sa  = _mm_and_si128(spx, amsk);
            __m128i out = _mm_or_si128(rgb, sa);

            _mm_storeu_si128((__m128i *)&drow[i], out);
        }

        // scalar
        for (; i < w; i++) {
            uint32_t s = srow[i];
            uint32_t a = s >> 24;
            if (a == 0) {
                continue;
            } else if (a == 255) {
                drow[i] = s;
            } else {
                uint32_t d = drow[i];
                uint32_t ia = 255 - a;

                uint32_t rb = (((d & 0x00FF00FFu) * ia) + ((s & 0x00FF00FFu) * a) + 0x00800080u) >> 8;
                uint32_t g  = (((d & 0x0000FF00u) * ia) + ((s & 0x0000FF00u) * a) + 0x00008000u) >> 8;

                drow[i] = (s & 0xFF000000u) | (rb & 0x00FF00FFu) | (g & 0x0000FF00u);
            }
        }

        drow += stride;
        srow += stride;
    }
}

/**
 * @brief Copy the backbuffer contents to the framebuffer.
 */
void vbe_flip(void)
{
    uint16_t x0 = dbb->x0;
    uint16_t x1 = dbb->x1;
    uint16_t y0 = dbb->y0;
    uint16_t y1 = dbb->y1;

    uint16_t rect_height = y1-y0;
    uint32_t rect_bytes = rect_height * vbe_info.pitch;

    uint32_t* bb_ptr = vbe_info.backbuffer + y0 * vbe_info.pitch; 
    uint32_t* fb_ptr = vbe_info.framebuffer + y0 * vbe_info.pitch;
    uint32_t* buf0_ptr = vbe_z_layers[0]->bufptr + y0 * vbe_info.pitch;

    memcpy_nt(bb_ptr, buf0_ptr, rect_bytes);

    for (uint8_t z = 1; z < init_z; z++) {
        vbe_z_layer_t *layer = (vbe_z_layer_t*)vbe_z_layers[z];
        if (!layer->active)
            continue;

        uint32_t* src_z_ptr = vbe_z_layers[z]->bufptr + y0 * vbe_info.pitch;
        uint32_t* dst_z_ptr = bb_ptr;

        vbe_blend_area(dst_z_ptr, src_z_ptr, x0, x1, y0, y1);
    }

    memcpy_nt(fb_ptr, bb_ptr, rect_bytes);

    dbb->x0 = 0;
    dbb->x1 = 0;
    dbb->y0 = 0;
    dbb->y1 = 0;
    /*
    // OLD IMPLEMENTATION
    // Composite only dirty scanlines from base backbuffer + z-layers into framebuffer.
    uint32_t stride = vbe_info.pitch / sizeof(uint32_t);
    uint32_t *base_buf = vbe_info.backbuffer; // base layer
    uint32_t *dst_buf = vbe_info.framebuffer;

    for (uint32_t y = 0; y < SCREEN_HEIGHT; y++)
    {
        if (!dirty_lines[y])
            continue;
        uint32_t *dst_row = &dst_buf[y * stride];
        uint32_t *base_row = &base_buf[y * stride];
        // Start with base
        memcpy(dst_row, base_row, SCREEN_WIDTH * sizeof(uint32_t));
        // Blend each z layer on top
        for (uint32_t z = 1; z < VBE_NUM_Z_LAYERS; z++)
        {
            uint32_t *layer_row = vbe_z_layers[z] ? &vbe_z_layers[z][y * stride] : 0;
            if (!layer_row)
                continue;
            vbe_blend_row(dst_row, layer_row, SCREEN_WIDTH);
        }
        dirty_lines[y] = 0;
    }
    vbe_clear_dirty_bitmap();
    */
}

/**
 * @brief Flip the backbuffer to the framebuffer.
 *
 * This function copies the contents of the backbuffer to the framebuffer,
 * only for the dirty lines.
 */
void vbe_flip_all(void)
{
    uint32_t stride = vbe_info.pitch / sizeof(uint32_t);
    uint32_t *base_buf = vbe_z_layers[0]->bufptr;
    uint32_t *dst_buf = vbe_info.framebuffer;
    memcpy(dst_buf, base_buf, fb_size_bytes);
    dbb->x0 = 0;
    dbb->x1 = 0;
    dbb->y0 = 0;
    dbb->y1 = 0;
}

/**
 * @brief Draw a font glyph at a given position with a given color.
 *
 * The glyph is drawn as an 8x16 rectangle starting at (x, y).
 * The glyph data is 8 bits per row (one byte), each bit is one pixel.
 *
 * @param glyph The font glyph to draw.
 * @param x X-coordinate of the glyph's top-left corner.
 * @param y Y-coordinate of the glyph's top-left corner.
 * @param color The foreground color to draw the glyph.
 */
void vbe_drawglyph(FontGlyph *glyph, uint32_t x, uint32_t y, uint32_t color) {
    if (!glyph)
        return;

    uint32_t stride = vbe_info.pitch / sizeof(uint32_t);
    uint32_t *dst_buf = vbe_z_layers[0]->bufptr;

    for (uint32_t row = 0; row < VBE_FONT_HEIGHT; row++) {
        uint8_t bits = glyph->data[row];
        uint32_t *dst = dst_buf + (y + row) * stride + x;

        for (uint32_t col = 0; col < VBE_FONT_WIDTH; col++) {
            if (bits & (uint8_t)(1u << (7u - col))) {
                dst[col] = color;
                vbe_mark_pixel_dirty(x + col, y + row);
            }
        }
    }
}

/**
 * @brief Shift the dirty bitmap up by the given number of rows.
 *        Used when the terminal scrolls.
 *
 * @param num_rows Number of rows to scroll up.
 */
void vbe_shift_dirty_bitmap_up(uint32_t num_rows)
{
    uint16_t y0 = dbb->y0;
    uint16_t y1 = dbb->y1;

    uint16_t pixels_shift = num_rows * VBE_FONT_HEIGHT;

    int16_t sy0 = y0 - pixels_shift;
    int16_t sy1 = y1 - pixels_shift;

    if (sy0 < 0 || sy1 < 0) {
        y0 = 0;
        y1 = 0;
        return;
    }

    y0 = (uint16_t)sy0;
    y1 = (uint16_t)sy1;
    return;
}

/**
 * @brief Print a character to the terminal.
 *
 * @param c The character to print.
 */
void vbe_terminal_putchar(char c)
{
    if (c == '\n')
    {
        term_cursor_col = 0;
        term_cursor_row++;
    }
    else if (c == '\r')
    {
        term_cursor_col = 0;
    }
    else
    {
        FontGlyph *glyph = find_glyph((uint8_t)c);
        if (glyph)
        {
            uint32_t px = term_cursor_col * VBE_FONT_WIDTH;
            uint32_t py = term_cursor_row * VBE_FONT_HEIGHT;
            vbe_fillrect(px, py, VBE_FONT_WIDTH, VBE_FONT_HEIGHT, term_bg_color);
            vbe_drawglyph(glyph, px, py, term_fg_color);
        }
        term_cursor_col++;
    }

    if (term_cursor_col >= term_max_cols())
    {
        term_cursor_col = 0;
        term_cursor_row++;
    }

    if (term_cursor_row >= term_max_rows())
    {
        uint32_t bytes_per_row = vbe_info.pitch * VBE_FONT_HEIGHT;
        uint32_t visible_rows = vbe_info.height - VBE_FONT_HEIGHT;

        uint8_t *dst = (uint8_t *)vbe_z_layers[0]->bufptr;
        uint8_t *src = dst + bytes_per_row;

        memmove(dst, src, visible_rows * vbe_info.pitch);
        //vbe_shift_dirty_bitmap_up(5);
        vbe_fillrect(0, visible_rows, vbe_info.width, VBE_FONT_HEIGHT, term_bg_color);
        term_cursor_row = term_max_rows() - 1;
        //vbe_fast_mark_dirty(0, 0, vbe_info.width, vbe_info.height);
    }
}

/**
 * @brief Print a null-terminated string to the terminal.
 *
 * @param str The string to print.
 */
void vbe_terminal_puts(const char *str, int len) {
    uint8_t in_escape = 0;
    char esc_buf[32];
    int esc_len = 0;
    int str_len = len;
    if (len == 0)
        str_len = strlen(str);

    for (int i = 0; i < str_len; i++) {
        char c = str[i];
        if (!in_escape) {
            if (c == '\033') { // escape
                in_escape = 1;
                esc_len = 0;
            } else { vbe_terminal_putchar(c); }
        } else {
            if (esc_len < (int)sizeof(esc_buf) - 1)
                esc_buf[esc_len++] = c;

            // ansi sequences end with a letter
            if (isalpha(c)) {
                esc_buf[esc_len] = '\0';
                in_escape = 0;

                if (esc_buf[0] == '[')
                {
                    vbe_handle_ansi_sequence(esc_buf + 1); // skip [
                }
            }
        }
    }
}

/**
 * @brief Remove the last character printed to the terminal.
 */
void vbe_terminal_back(void)
{
    if (term_cursor_col == 0 && term_cursor_row == 0)
    {
        return;
    }

    if (term_cursor_col == 0)
    {
        term_cursor_row--;
        term_cursor_col = term_max_cols() - 1;
    }
    else
    {
        term_cursor_col--;
    }

    uint32_t px = term_cursor_col * VBE_FONT_WIDTH;
    uint32_t py = term_cursor_row * VBE_FONT_HEIGHT;
    vbe_fillrect(px, py, VBE_FONT_WIDTH, VBE_FONT_HEIGHT, 0x000000);
    //vbe_fast_mark_dirty(px, py, VBE_FONT_WIDTH, VBE_FONT_HEIGHT);
}

/**
 * @brief Set the foreground color using a 32-bit value (0xAARRGGBB).
 *
 * @param color The new foreground color.
 */
void vbe_setcolor_fg(uint32_t color)
{
    term_fg_color = color;
}

/**
 * @brief Set the background color using a 32-bit value (0xAARRGGBB).
 *
 * @param color The new background color.
 */
void vbe_setcolor_bg(uint32_t color)
{
    term_bg_color = color;
}

/**
 * @brief Initialize the VBE palette with 256 colors.
 */
void vbe_palette_init(void)
{
    for (int i = 0; i < 16; i++)
    {
        vbe_palette[i] = vbe_colors[i];
    }

    int index = 16;
    for (int r = 0; r < 6; r++)
    {
        for (int g = 0; g < 6; g++)
        {
            for (int b = 0; b < 6; b++)
            {
                uint8_t rr = (r == 0) ? 0 : 55 + r * 40;
                uint8_t gg = (g == 0) ? 0 : 55 + g * 40;
                uint8_t bb = (b == 0) ? 0 : 55 + b * 40;
                vbe_palette[index++] = (rr << 16) | (gg << 8) | bb;
            }
        }
    }

    for (int i = 0; i < 24; i++)
    {
        uint8_t level = 8 + i * 10;
        vbe_palette[index++] = (level << 16) | (level << 8) | level;
    }
}

/**
 * @brief Set the foreground color from the VBE palette.
 *
 * @param color Index in the palette.
 */
void vbe_setcolor_fg_palette(vbe_color_t color)
{
    term_fg_color = vbe_colors[color];
}

/**
 * @brief Set the background color from the VBE palette.
 *
 * @param color Index in the palette.
 */
void vbe_setcolor_bg_palette(vbe_color_t color)
{
    term_bg_color = vbe_colors[color];
}

/**
 * @brief Optimized function to draw a single pixel on the backbuffer.
 *
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param color Pixel color.
 */
void vbe_fast_putpixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= vbe_info.width || y >= vbe_info.height)
        return;
    fast_putpixel(vbe_z_layers[0]->bufptr,
                  vbe_info.pitch,
                  vbe_info.width,
                  vbe_info.height,
                  x, y, color);
    vbe_mark_pixel_dirty(x, y);
}

/**
 * @brief Mark rectangular section as dirty.
 *
 * @param x X-coordinate.
 * @param y Y-coordinate.
 */
void vbe_fast_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0)
        return;

    uint32_t index = y * SCREEN_WIDTH + x;
    dirty_bitmap[index / 8] |= (1 << (index % 8));

    if (x + w > SCREEN_WIDTH)
        w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT)
        h = SCREEN_HEIGHT - y;

    for (uint32_t dy = 0; dy < h; dy++)
    {
        for (uint32_t dx = 0; dx < w; dx++)
        {
            vbe_mark_pixel_dirty(x + dx, y + dy);
        }
    }
}

/**
 * @brief Set the cursor position.
 *
 * @param col Column position.
 * @param row Row position.
 */
void vbe_set_cursor(uint32_t col, uint32_t row)
{
    if (col >= term_max_cols() || row >= term_max_rows())
    {
        return;
    }
    term_cursor_col = col;
    term_cursor_row = row;
}

/**
 * @brief Clear the screen.
 *
 * @param color The color to fill the screen with.
 */
void vbe_clear_screen(uint32_t color)
{
    uint32_t *back_buf = vbe_z_layers[0]->bufptr;
    memset(back_buf, color, fb_size_bytes);
    vbe_mark_region_dirty(0,0,SCREEN_WIDTH,SCREEN_WIDTH);
}

void vbe_z_putpixel(uint32_t z, uint32_t x, uint32_t y, uint32_t color)
{
    if (!vbe_z_valid(z))
        return;
    if (x >= vbe_info.width || y >= vbe_info.height)
        return;
    uint8_t *row_start = (uint8_t *)vbe_z_layers[z]->bufptr + (y * vbe_info.pitch);
    uint32_t *dest = (uint32_t *)(row_start + (x * 4));
    *dest = color;
    vbe_mark_pixel_dirty(x, y);
}

void vbe_z_fillrect(uint32_t z, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!vbe_z_valid(z))
        return;
    if (x + w > vbe_info.width)
        w = vbe_info.width - x;
    if (y + h > vbe_info.height)
        h = vbe_info.height - y;
    for (uint32_t row = 0; row < h; row++)
    {
        uint8_t *row_ptr = (uint8_t *)vbe_z_layers[z]->bufptr + ((y + row) * vbe_info.pitch);
        uint32_t *dst = (uint32_t *)(row_ptr) + x;
        for (uint32_t col = 0; col < w; col++)
        {
            dst[col] = color;
        }
    }

    vbe_mark_region_dirty(x,y,w,h);
}

void vbe_clear_z_layer(uint32_t z, uint32_t color)
{
    if (!vbe_z_valid(z))
        return;
    memset(vbe_z_layers[z]->bufptr, color, fb_size_bytes);
    dbb->x0 = 0;
    dbb->x1 = SCREEN_WIDTH-1;
    dbb->y0 = 0;
    dbb->y1 = SCREEN_HEIGHT-1;
}

void vbe_clear_all_z_layers(void)
{
    for (uint32_t z = 1; z < VBE_NUM_Z_LAYERS; z++)
    {
        if (vbe_z_layers[z])
            memset(vbe_z_layers[z]->bufptr, 0, fb_size_bytes);
    }
}

// Copy source z-layer to destination fading alpha by fade_amount.
// fade_amount: amount to subtract from alpha (0-255).
// If src_z == dst_z an in-place fade is applied.
void vbe_z_copy_and_fade(uint32_t src_z, uint32_t dst_z, uint8_t fade_amount)
{
    if (!(src_z > 0 && src_z < VBE_NUM_Z_LAYERS))
        return;
    if (!(dst_z > 0 && dst_z < VBE_NUM_Z_LAYERS))
        return;
    uint32_t *src = vbe_z_layers[src_z]->bufptr;
    uint32_t *dst = vbe_z_layers[dst_z]->bufptr;
    if (!src || !dst)
        return;

    if (fade_amount == 0)
    {
        if (src_z == dst_z)
            return; // nothing to do
        memcpy(dst, src, fb_size_bytes);
        return;
    }

    uint32_t pixels = fb_size_bytes / sizeof(uint32_t);
    if (src_z == dst_z)
    {
        for (uint32_t i = 0; i < pixels; i++)
        {
            uint32_t c = src[i];
            if (c == 0)
                continue;
            uint8_t a = (uint8_t)(c >> 24);
            if (a <= fade_amount)
            {
                src[i] = 0;
                continue;
            }
            a -= fade_amount;
            src[i] = ((uint32_t)a << 24) | (c & 0x00FFFFFFu);
        }
    }
    else
    {
        for (uint32_t i = 0; i < pixels; i++)
        {
            uint32_t c = src[i];
            if (c == 0)
            {
                dst[i] = 0;
                continue;
            }
            uint8_t a = (uint8_t)(c >> 24);
            if (a <= fade_amount)
            {
                dst[i] = 0;
                continue;
            }
            a -= fade_amount;
            dst[i] = ((uint32_t)a << 24) | (c & 0x00FFFFFFu);
        }
    }
}

void vbe_handle_ansi_sequence(const char *seq) {
    char buf[64];
    strncpy(buf, seq, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // find command letter
    int len = strlen(buf);
    if (len == 0)
        return;
    char cmd = buf[len - 1];
    buf[len - 1] = '\0'; // remove command char for parsing
    
    // split params
    char *params[16];
    int count = 0;
    char *tok = strtok(buf, ";");
    while (tok && count < 16) {
        params[count++] = tok;
        tok = strtok(NULL, ";");
    }

    switch (cmd) {
        case 'm': { // SGR (Select Graphic Rendition)
            if (count == 0) {
                // reset
                ansi_fg = 0xFFFFFFFF;
                ansi_bg = 0xFF000000;
                vbe_setcolor_fg(ansi_fg);
                vbe_setcolor_bg(ansi_bg);
                return;
            }

            for (int i = 0; i < count; i++) {
                int code = atoi(params[i]);

                if (code == 0) {
                    ansi_fg = 0xFFFFFFFF;
                    ansi_bg = 0xFF000000;
                    vbe_setcolor_fg(ansi_fg);
                    vbe_setcolor_bg(ansi_bg);
                }
                else if (code >= 30 && code <= 37) {
                    ansi_fg = ansi_color_table[code - 30];
                    vbe_setcolor_fg(ansi_fg);
                }
                else if (code == 39) {
                    ansi_fg = 0xFFFFFFFF;
                    vbe_setcolor_fg(ansi_fg);
                }
                else if (code >= 40 && code <= 47) {
                    ansi_bg = ansi_color_table[code - 40];
                    vbe_setcolor_bg(ansi_bg);
                }
                else if (code == 49) {
                    ansi_bg = 0xFF000000;
                    vbe_setcolor_bg(ansi_bg);
                }
                else if (code >= 90 && code <= 97) {
                    ansi_fg = ansi_color_table[8 + (code - 90)];
                    vbe_setcolor_fg(ansi_fg);
                }
                else if (code >= 100 && code <= 107) {
                    ansi_bg = ansi_color_table[8 + (code - 100)];
                    vbe_setcolor_bg(ansi_bg);
                }
                // 24bit truecolor: 38;2;R;G;B or 48;2;R;G;B
                else if (code == 38 || code == 48) {
                    uint8_t is_fg = (code == 38);
                    if (i + 4 < count && atoi(params[i + 1]) == 2)
                    {
                        uint8_t r = (uint8_t)atoi(params[i + 2]);
                        uint8_t g = (uint8_t)atoi(params[i + 3]);
                        uint8_t b = (uint8_t)atoi(params[i + 4]);
                        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
                        if (is_fg)
                            vbe_setcolor_fg(color);
                        else
                            vbe_setcolor_bg(color);
                        i += 4;
                    }
                }
            }
            break;
        }

        case 'H': // cursor move (row;col)
        case 'f': {
            uint32_t row = (count >= 1) ? atoi(params[0]) : 1;
            uint32_t col = (count >= 2) ? atoi(params[1]) : 1;
            if (row < 1) row = 1;
            if (col < 1) col = 1;
            if (row > term_max_rows()) row = term_max_rows();
            if (col > term_max_cols()) col = term_max_cols();
            vbe_set_cursor(col - 1, row - 1);
            break;
        }

        case 'J': { // clear screen
            int mode = (count > 0) ? atoi(params[0]) : 0;
            if (mode == 2) // clear all
                vbe_clear_screen(ansi_bg);
                vbe_set_cursor(0,0);
            break;
        }

        default:
            // unsupported sequence
            break;
    }
}

void vbe_worker(void) {
    while (1) {
        vbe_flip();
        kernel_yield();
    }
}