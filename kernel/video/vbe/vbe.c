/*
    vbe.c
    VESA BIOS Extensions (VBE) Graphics Driver for Serotonin.
*/

#include "vbe.h"
#include "../../kernel.h"      // kernel_malloc(), kernel_panic()
#include "../../stdlib/stdlib.h" // for memcpy
#include <stdint.h>
#include <stddef.h>
#include "../../multiboot.h"
#include "../../vmm/paging_init.h"
#include "../font.h"

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

static const uint16_t k255w[8]    __attribute__((aligned(16))) = {0x00FF,0x00FF,0x00FF,0x00FF,0x00FF,0x00FF,0x00FF,0x00FF};
static const uint16_t k257w[8]    __attribute__((aligned(16))) = {0x0101,0x0101,0x0101,0x0101,0x0101,0x0101,0x0101,0x0101};
static const uint16_t k254w[8]    __attribute__((aligned(16))) = {0x00FE,0x00FE,0x00FE,0x00FE,0x00FE,0x00FE,0x00FE,0x00FE};
static const uint32_t kAlphaFF[4] __attribute__((aligned(16))) = {0xFF000000u,0xFF000000u,0xFF000000u,0xFF000000u};

// ---------------- Z-Layer Buffers ----------------
// Layer 0 is reserved (excluded from user rendering per requirement). Layers 1..VBE_NUM_Z_LAYERS-1 available.
// Each layer stores 32-bit ARGB pixels; a pixel value of 0 means fully transparent (alpha==0).
static uint32_t *vbe_z_layers[VBE_NUM_Z_LAYERS];
static inline int vbe_z_valid(uint32_t z){ return (z>0 && z < VBE_NUM_Z_LAYERS); }

#define DIRTY_BITMAP_SIZE ((SCREEN_WIDTH * SCREEN_HEIGHT + 7) / 8)
uint8_t dirty_bitmap[DIRTY_BITMAP_SIZE];

uint32_t vbe_colors[16] = {
    0xFF000000, // BLACK
    0xFF0000AA, // BLUE
    0xFF00AA00, // GREEN
    0xFF00AAAA, // CYAN
    0xFFAA0000, // RED
    0xFFAA00AA, // MAGENTA
    0xFFAA5500, // BROWN / YELLOW
    0xFFAAAAAA, // LIGHT GRAY
    0xFF555555, // DARK GRAY
    0xFF5555FF, // LIGHT BLUE
    0xFF55FF55, // LIGHT GREEN
    0xFF55FFFF, // LIGHT CYAN
    0xFFFF5555, // LIGHT RED
    0xFFFF55FF, // LIGHT MAGENTA
    0xFFFFFF00, // YELLOW / LIGHT YELLOW
    0xFFFFFFFF  // WHITE
};

/**
 * @brief Get the maximum number of terminal columns.
 * 
 * @return The number of columns based on screen width and font width.
 */
static uint32_t term_max_cols(void) {
    return vbe_info.width / VBE_FONT_WIDTH;
}

/**
 * @brief Get the maximum number of terminal rows.
 * 
 * @return The number of rows based on screen height and font height.
 */
static uint32_t term_max_rows(void) {
    return vbe_info.height / VBE_FONT_HEIGHT;
}

/**
  * @brief Clear dirty bitmap
 */
void vbe_clear_dirty_bitmap(void) {
    memset(dirty_bitmap, 0, DIRTY_BITMAP_SIZE);
}

/**
 * @brief Initialize the VBE (VESA BIOS Extensions) for graphics mode.
 *
 * This function sets up the VBE for use with the framebuffer.
 * Must be called *after* paging_init((uintptr_t)mbi->framebuffer_addr) has run.
 * 
 * @param mbi The multiboot information structure.
 */
void vbe_init(multiboot_info_t *mbi) {
    uint32_t phys_fb = (uint32_t)(mbi->framebuffer_addr);
    uint32_t pitch   = (uint32_t)(mbi->framebuffer_pitch);
    uint32_t width   = (uint32_t)(mbi->framebuffer_width);
    uint32_t height  = (uint32_t)(mbi->framebuffer_height);
    uint32_t bpp     = (uint32_t)(mbi->framebuffer_bpp);

    uint32_t *virt_fb = (uint32_t *)FB_VMA_BASE;
    if (!virt_fb) {
        // ?!?!?!
        kernel_panic("vbe_init: unable to get virtual framebuffer address");
    }

    vbe_info.framebuffer = virt_fb;
    vbe_info.width       = width;
    vbe_info.height      = height;
    vbe_info.pitch       = pitch;
    vbe_info.bpp         = bpp;

    fb_size_bytes = (uint32_t)height * pitch;
    vbe_info.backbuffer = (uint32_t *)kernel_malloc(fb_size_bytes);
    if (!vbe_info.backbuffer) {
        kernel_panic("vbe_init: could not allocate backbuffer");
    }

    memset(vbe_info.backbuffer, 0, fb_size_bytes);

    // Allocate z-layer buffers
    for (uint32_t i=1;i<VBE_NUM_Z_LAYERS;i++) {
        vbe_z_layers[i] = (uint32_t*)kernel_malloc(fb_size_bytes);
        if (!vbe_z_layers[i]) {
            kernel_panic("vbe_init: could not allocate z-layer buffer");
        }
        memset(vbe_z_layers[i], 0, fb_size_bytes);
    }

    {
        uintptr_t back_start = (uintptr_t)vbe_info.backbuffer;
        uintptr_t back_end   = back_start + fb_size_bytes;
        if (back_start < KERNEL_HEAP_VMA || back_end > (KERNEL_HEAP_VMA + KERNEL_HEAP_SIZE)) {
            kernel_panic("vbe_init: backbuffer out of heap bounds");
        }
    }

    vbe_clear_dirty_bitmap();
}

/**
  * @brief Mark pixel as dirty
  *
  * @param x X-coordinate.
  * @param y Y-coordinate.
 */
static inline void vbe_mark_pixel_dirty(uint32_t x, uint32_t y) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    dirty_lines[y] = 1;
}

/**
 * @brief Draw a pixel on the backbuffer.
 * 
 * @param x The x-coordinate.
 * @param y The y-coordinate.
 * @param color The pixel color.
 */
void vbe_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= vbe_info.width || y >= vbe_info.height) return;
    uint8_t *row_start = (uint8_t *)vbe_info.backbuffer + (y * vbe_info.pitch);
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
void vbe_fast_draw_hline(uint32_t *buf, uint32_t pitch, uint32_t x, uint32_t y, uint32_t w, uint32_t color) {
    uint8_t *row = (uint8_t *)buf + y * pitch;
    uint32_t *dst = ((uint32_t *)row) + x;
    for (uint32_t i = 0; i < w; i++) {
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
void vbe_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (x + w  > vbe_info.width)  w = vbe_info.width  - x;
    if (y + h  > vbe_info.height) h = vbe_info.height - y;
    for (uint32_t row = 0; row < h; row++) {
        vbe_fast_draw_hline(vbe_info.backbuffer, vbe_info.pitch, x, y + row, w, color);
    }
    //vbe_fast_mark_dirty(x, y, w, h);
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
inline void fast_putpixel(uint32_t *buf, uint32_t pitch, uint32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t color) {
    uint8_t *row = (uint8_t *)buf + y * pitch;
    ((uint32_t *)row)[x] = color;
}

inline uint32_t div255(uint32_t p) {
    p = p + ((p + 257u) >> 8);
    return p >> 8;
}

static inline void vbe_blend_row(uint32_t *dst, const uint32_t *src, size_t width_px) {
    size_t n4 = width_px >> 2;      // groups of 4 pixels (16 bytes)
    size_t rem = width_px & 3;

    const uint8_t *s = (const uint8_t*)src;
    uint8_t *d = (uint8_t*)dst;

    if (n4) {
        asm volatile(
            "pxor %%xmm7, %%xmm7\n\t"                 // xmm7 = 0
            "1:\n\t"
            // load 4 src and 4 dst pixels
            "movdqu (%[s]), %%xmm0\n\t"              // xmm0 = src bytes
            "movdqu (%[d]), %%xmm1\n\t"              // xmm1 = dst bytes

            // unpack to u16 lanes [B,G,R,A,B,G,R,A] for low and high bytes
            "pxor     %%xmm7, %%xmm7\n\t"
            "movdqa   %%xmm0, %%xmm2\n\t"
            "punpcklbw %%xmm7, %%xmm2\n\t"     // s_lo
            "movdqa   %%xmm0, %%xmm3\n\t"
            "punpckhbw %%xmm7, %%xmm3\n\t"     // s_hi
            "movdqa   %%xmm1, %%xmm4\n\t"
            "punpcklbw %%xmm7, %%xmm4\n\t"     // d_lo
            "movdqa   %%xmm1, %%xmm5\n\t"
            "punpckhbw %%xmm7, %%xmm5\n\t"     // d_hi

            // ---- low half (2 pixels) ----
            // A_lo = [a0,a0,a0,a0,a1,a1,a1,a1]
            "movdqa   %%xmm2, %%xmm6\n\t"
            "pshuflw  $0xFF, %%xmm6, %%xmm6\n\t"
            "pshufhw  $0xFF, %%xmm6, %%xmm6\n\t"

            // invA_lo = 255 - A_lo
            "movdqa   %[K255], %%xmm1\n\t"
            "psubw    %%xmm6, %%xmm1\n\t"        // xmm1 = invA_lo

            // Psa_lo = s_lo * A_lo (keep a copy for remainder)
            "movdqa   %%xmm2, %%xmm0\n\t"
            "pmullw   %%xmm6, %%xmm0\n\t"        // xmm0 = Psa_lo
            "movdqa   %%xmm0, %%xmm7\n\t"        // save Psa_lo -> xmm7

            // q1_lo = floor(Psa_lo/255)
            "movdqa   %%xmm0, %%xmm6\n\t"
            "paddw    %[K257], %%xmm6\n\t"
            "psrlw    $8, %%xmm6\n\t"
            "paddw    %%xmm6, %%xmm0\n\t"
            "psrlw    $8, %%xmm0\n\t"            // xmm0 = q1_lo

            // Pda_lo = d_lo * invA_lo  (keep copy)
            "movdqa   %%xmm4, %%xmm6\n\t"
            "pmullw   %%xmm1, %%xmm6\n\t"        // xmm6 = Pda_lo
            "movdqa   %%xmm6, %%xmm1\n\t"        // save Pda_lo -> xmm1

            // q2_lo = floor(Pda_lo/255)
            "movdqa   %%xmm6, %%xmm4\n\t"
            "paddw    %[K257], %%xmm4\n\t"
            "psrlw    $8, %%xmm4\n\t"
            "paddw    %%xmm4, %%xmm6\n\t"
            "psrlw    $8, %%xmm6\n\t"            // xmm6 = q2_lo

            // r1_lo = Psa_lo - (q1_lo<<8) + q1_lo
            "movdqa   %%xmm0, %%xmm4\n\t"
            "psllw    $8, %%xmm4\n\t"
            "psubw    %%xmm4, %%xmm7\n\t"
            "paddw    %%xmm0, %%xmm7\n\t"        // xmm7 = r1_lo

            // r2_lo = Pda_lo - (q2_lo<<8) + q2_lo
            "movdqa   %%xmm6, %%xmm4\n\t"
            "psllw    $8, %%xmm4\n\t"
            "psubw    %%xmm4, %%xmm1\n\t"
            "paddw    %%xmm6, %%xmm1\n\t"        // xmm1 = r2_lo

            // carry_lo = (r1_lo + r2_lo) >= 255 ? 1 : 0
            "paddw    %%xmm1, %%xmm7\n\t"        // rsum_lo
            "movdqa   %%xmm7, %%xmm4\n\t"
            "pcmpgtw  %[K254], %%xmm4\n\t"       // rsum_lo > 254 -> 0xFFFF
            "psrlw    $15, %%xmm4\n\t"           // 0/1 per lane

            // q_lo = q1_lo + q2_lo + carry_lo
            "paddw    %%xmm6, %%xmm0\n\t"
            "paddw    %%xmm4, %%xmm0\n\t"        // xmm0 = q_lo

            // ---------- high half (pixels 2,3) ----------
            // A_hi
            "movdqa   %%xmm3, %%xmm6\n\t"
            "pshuflw  $0xFF, %%xmm6, %%xmm6\n\t"
            "pshufhw  $0xFF, %%xmm6, %%xmm6\n\t"

            // invA_hi
            "movdqa   %[K255], %%xmm2\n\t"
            "psubw    %%xmm6, %%xmm2\n\t"        // xmm2 = invA_hi

            // Psa_hi, keep copy
            "movdqa   %%xmm3, %%xmm7\n\t"
            "pmullw   %%xmm6, %%xmm7\n\t"        // xmm7 = Psa_hi
            "movdqa   %%xmm7, %%xmm4\n\t"        // save -> xmm4

            // q1_hi
            "movdqa   %%xmm7, %%xmm6\n\t"
            "paddw    %[K257], %%xmm6\n\t"
            "psrlw    $8, %%xmm6\n\t"
            "paddw    %%xmm6, %%xmm7\n\t"
            "psrlw    $8, %%xmm7\n\t"            // xmm7 = q1_hi

            // Pda_hi (keep copy), q2_hi
            "movdqa   %%xmm5, %%xmm6\n\t"
            "pmullw   %%xmm2, %%xmm6\n\t"        // xmm6 = Pda_hi
            "movdqa   %%xmm6, %%xmm2\n\t"        // save -> xmm2
            "movdqa   %%xmm6, %%xmm5\n\t"
            "paddw    %[K257], %%xmm5\n\t"
            "psrlw    $8, %%xmm5\n\t"
            "paddw    %%xmm5, %%xmm6\n\t"
            "psrlw    $8, %%xmm6\n\t"            // xmm6 = q2_hi

            // r1_hi
            "movdqa   %%xmm7, %%xmm5\n\t"
            "psllw    $8, %%xmm5\n\t"
            "psubw    %%xmm5, %%xmm4\n\t"
            "paddw    %%xmm7, %%xmm4\n\t"        // xmm4 = r1_hi

            // r2_hi
            "movdqa   %%xmm6, %%xmm5\n\t"
            "psllw    $8, %%xmm5\n\t"
            "psubw    %%xmm5, %%xmm2\n\t"
            "paddw    %%xmm6, %%xmm2\n\t"        // xmm2 = r2_hi

            // carry_hi
            "paddw    %%xmm2, %%xmm4\n\t"        // rsum_hi
            "pcmpgtw  %[K254], %%xmm4\n\t"
            "psrlw    $15, %%xmm4\n\t"           // carry_hi

            // q_hi
            "paddw    %%xmm6, %%xmm7\n\t"
            "paddw    %%xmm4, %%xmm7\n\t"        // xmm7 = q_hi

            // pack to bytes, set alpha=0xFF, store
            "packuswb %%xmm7, %%xmm0\n\t"        // BGRA BGRA BGRA BGRA
            "por      %[KALPHA], %%xmm0\n\t"
            "movdqu   %%xmm0, (%[d])\n\t"

            // advance
            "add      $16, %[s]\n\t"
            "add      $16, %[d]\n\t"
            "dec      %[n4]\n\t"
            "jnz      1b\n\t"
            : [d] "+r"(d), [s] "+r"(s), [n4] "+r"(n4)
            : [K255] "m"(k255w), [K257] "m"(k257w), [K254] "m"(k254w), [KALPHA] "m"(kAlphaFF)
            : "cc", "memory",
              // clobbers
              "%xmm0","%xmm1","%xmm2","%xmm3","%xmm4","%xmm5","%xmm6","%xmm7"
        );
    }

    // leftover 1..3 pixels
    for (size_t i = 0; i < rem; ++i) {
        uint32_t s32 = ((const uint32_t*)s)[i];
        if (!s32) continue; // fast path: fully transparent
        uint32_t d32 = ((uint32_t*)d)[i];

        uint32_t a = (s32 >> 24) & 0xFFu;
        uint32_t invA = 255u - a;

        uint32_t sr = (s32 >> 16) & 0xFFu, sg = (s32 >> 8) & 0xFFu, sb = s32 & 0xFFu;
        uint32_t dr = (d32 >> 16) & 0xFFu, dg = (d32 >> 8) & 0xFFu, db = d32 & 0xFFu;

        // exact truncating /255
        uint32_t rr = div255(sr * a) + div255(dr * invA);
        uint32_t rg = div255(sg * a) + div255(dg * invA);
        uint32_t rb = div255(sb * a) + div255(db * invA);

        ((uint32_t*)d)[i] = (0xFFu<<24) | (rr<<16) | (rg<<8) | rb;
    }
}

/**
 * @brief Copy the backbuffer contents to the framebuffer.
 */
void vbe_flip(void) {
    // Composite only dirty scanlines from base backbuffer + z-layers into framebuffer.
    uint32_t stride = vbe_info.pitch / sizeof(uint32_t);
    uint32_t *base_buf = vbe_info.backbuffer; // base layer
    uint32_t *dst_buf = vbe_info.framebuffer;

    for (uint32_t y=0;y<SCREEN_HEIGHT;y++) {
        if (!dirty_lines[y]) continue;
        uint32_t *dst_row = &dst_buf[y*stride];
        uint32_t *base_row = &base_buf[y*stride];
        // Start with base
        memcpy(dst_row, base_row, SCREEN_WIDTH * sizeof(uint32_t));
        // Blend each z layer on top
        for (uint32_t z=1; z<VBE_NUM_Z_LAYERS; z++) {
            uint32_t *layer_row = vbe_z_layers[z] ? &vbe_z_layers[z][y*stride] : 0;
            if (!layer_row) continue;
            vbe_blend_row(dst_row, layer_row, SCREEN_WIDTH);
        }
        dirty_lines[y] = 0;
    }
    vbe_clear_dirty_bitmap();
}

/**
 * @brief Flip the backbuffer to the framebuffer.
 *
 * This function copies the contents of the backbuffer to the framebuffer,
 * only for the dirty lines.
 */
void vbe_flip_all(void) {
    uint32_t stride = vbe_info.pitch / sizeof(uint32_t);
    uint32_t *base_buf = vbe_info.backbuffer;
    uint32_t *dst_buf = vbe_info.framebuffer;
    memcpy(dst_buf, base_buf, fb_size_bytes);
    vbe_clear_dirty_bitmap();
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
    if (!glyph) return;

    uint32_t stride = vbe_info.pitch / sizeof(uint32_t);
    uint32_t *dst_buf = vbe_info.backbuffer;

    for (uint32_t row = 0; row < VBE_FONT_HEIGHT; row++) {
        uint8_t bits = glyph->data[row];
        uint32_t dst_index = (y + row) * stride + x;
        uint32_t *dst = dst_buf + dst_index;

        for (uint32_t bit = 0; bit < VBE_FONT_WIDTH; bit++) {
            if (bits & (1 << (7 - bit))) {
                dst[bit] = color;
                vbe_mark_pixel_dirty(x + bit, y + row);
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
void vbe_shift_dirty_bitmap_up(uint32_t num_rows) {
    if (num_rows >= SCREEN_HEIGHT) {
        vbe_clear_dirty_bitmap();
        return;
    }

    uint32_t total_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    uint32_t scroll_pixels = num_rows * SCREEN_WIDTH;

    for (uint32_t i = 0; i < total_pixels - scroll_pixels; i++) {
        uint32_t src_index = i + scroll_pixels;
        uint32_t dst_index = i;

        uint8_t src_bit = (dirty_bitmap[src_index / 8] >> (src_index % 8)) & 1;
        if (src_bit)
            dirty_bitmap[dst_index / 8] |= (1 << (dst_index % 8));
        else
            dirty_bitmap[dst_index / 8] &= ~(1 << (dst_index % 8));
    }

    // Clear the bottom num_rows rows in the bitmap:
    for (uint32_t i = total_pixels - scroll_pixels; i < total_pixels; i++) {
        dirty_bitmap[i / 8] &= ~(1 << (i % 8));
    }

    vbe_fast_mark_dirty(0,0,SCREEN_WIDTH,SCREEN_HEIGHT-num_rows);
}

/**
 * @brief Print a character to the terminal.
 *
 * @param c The character to print.
 */
void vbe_terminal_putchar(char c) {
    if (c == '\n') {
        term_cursor_col = 0;
        term_cursor_row++;
    } else if (c == '\r') {
        term_cursor_col = 0;
    } else {
        FontGlyph *glyph = find_glyph((uint8_t)c);
        if (glyph) {
            uint32_t px = term_cursor_col * VBE_FONT_WIDTH;
            uint32_t py = term_cursor_row * VBE_FONT_HEIGHT;
            vbe_fillrect(px, py, VBE_FONT_WIDTH, VBE_FONT_HEIGHT, term_bg_color);
            vbe_drawglyph(glyph, px, py, term_fg_color);
        }
        term_cursor_col++;
    }

    if (term_cursor_col >= term_max_cols()) {
        term_cursor_col = 0;
        term_cursor_row++;
    }

    if (term_cursor_row >= term_max_rows()) {
        uint32_t bytes_per_row = vbe_info.pitch * VBE_FONT_HEIGHT;
        uint32_t visible_rows = vbe_info.height - VBE_FONT_HEIGHT;
        memmove(vbe_info.backbuffer,
                (uint8_t *)vbe_info.backbuffer + bytes_per_row,
                visible_rows * vbe_info.pitch);
        vbe_shift_dirty_bitmap_up(5);
        vbe_fillrect(0, visible_rows, vbe_info.width, VBE_FONT_HEIGHT, term_bg_color);
        term_cursor_row = term_max_rows() - 1;
        vbe_fast_mark_dirty(0, 0, vbe_info.width, vbe_info.height);
    }
}

/**
 * @brief Print a null-terminated string to the terminal.
 *
 * @param str The string to print.
 */
void vbe_terminal_puts(const char *str) {
    while (*str) {
        vbe_terminal_putchar(*str++);
    }
}

/**
 * @brief Remove the last character printed to the terminal.
 */
void vbe_terminal_back(void) {
    if (term_cursor_col == 0 && term_cursor_row == 0) {
        return;
    }

    if (term_cursor_col == 0) {
        term_cursor_row--;
        term_cursor_col = term_max_cols() - 1;
    } else {
        term_cursor_col--;
    }

    uint32_t px = term_cursor_col * VBE_FONT_WIDTH;
    uint32_t py = term_cursor_row * VBE_FONT_HEIGHT;
    vbe_fillrect(px, py, VBE_FONT_WIDTH, VBE_FONT_HEIGHT, 0x000000);
    vbe_fast_mark_dirty(px, py, VBE_FONT_WIDTH, VBE_FONT_HEIGHT);
}


/**
 * @brief Set the foreground color using a 32-bit value (0xAARRGGBB).
 *
 * @param color The new foreground color.
 */
void vbe_setcolor_fg(uint32_t color) {
    term_fg_color = color;
}

/**
 * @brief Set the background color using a 32-bit value (0xAARRGGBB).
 *
 * @param color The new background color.
 */
void vbe_setcolor_bg(uint32_t color) {
    term_bg_color = color;
}

/**
 * @brief Initialize the VBE palette with 256 colors.
 */
void vbe_palette_init(void) {
    for (int i = 0; i < 16; i++) {
        vbe_palette[i] = vbe_colors[i];
    }

    int index = 16;
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                uint8_t rr = (r == 0) ? 0 : 55 + r * 40;
                uint8_t gg = (g == 0) ? 0 : 55 + g * 40;
                uint8_t bb = (b == 0) ? 0 : 55 + b * 40;
                vbe_palette[index++] = (rr << 16) | (gg << 8) | bb;
            }
        }
    }

    for (int i = 0; i < 24; i++) {
        uint8_t level = 8 + i * 10;
        vbe_palette[index++] = (level << 16) | (level << 8) | level;
    }
}

/**
 * @brief Set the foreground color from the VBE palette.
 *
 * @param color Index in the palette.
 */
void vbe_setcolor_fg_palette(vbe_color_t color) {
    term_fg_color = vbe_colors[color];
}

/**
 * @brief Set the background color from the VBE palette.
 *
 * @param color Index in the palette.
 */
void vbe_setcolor_bg_palette(vbe_color_t color) {
    term_bg_color = vbe_colors[color];
}

/**
 * @brief Optimized function to draw a single pixel on the backbuffer.
 *
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param color Pixel color.
 */
void vbe_fast_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= vbe_info.width || y >= vbe_info.height) return;
    fast_putpixel(vbe_info.backbuffer,
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
void vbe_fast_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;

    uint32_t index = y * SCREEN_WIDTH + x;
    dirty_bitmap[index / 8] |= (1 << (index % 8));


    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;

    for (uint32_t dy = 0; dy < h; dy++) {
        for (uint32_t dx = 0; dx < w; dx++) {
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
void vbe_set_cursor(uint32_t col, uint32_t row) {
    if (col >= term_max_cols() || row >= term_max_rows()) {
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
void vbe_clear_screen(uint32_t color) {
    uint32_t *back_buf = vbe_info.backbuffer;
    memset(back_buf,color, fb_size_bytes);
    vbe_clear_dirty_bitmap();
}

void vbe_z_putpixel(uint32_t z, uint32_t x, uint32_t y, uint32_t color) {
    if (!vbe_z_valid(z)) return;
    if (x >= vbe_info.width || y >= vbe_info.height) return;
    uint8_t *row_start = (uint8_t*)vbe_z_layers[z] + (y * vbe_info.pitch);
    uint32_t *dest = (uint32_t *)(row_start + (x * 4));
    *dest = color; 
    vbe_mark_pixel_dirty(x,y);
}

void vbe_z_fillrect(uint32_t z, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!vbe_z_valid(z)) return;
    if (x + w  > vbe_info.width)  w = vbe_info.width  - x;
    if (y + h  > vbe_info.height) h = vbe_info.height - y;
    for (uint32_t row=0; row<h; row++) {
        uint8_t *row_ptr = (uint8_t*)vbe_z_layers[z] + ( (y+row) * vbe_info.pitch );
        uint32_t *dst = (uint32_t*)(row_ptr) + x;
        for (uint32_t col=0; col<w; col++) {
            dst[col] = color;
        }
        for (uint32_t col=0; col<w; col++) vbe_mark_pixel_dirty(x+col, y+row);
    }
}

void vbe_clear_z_layer(uint32_t z, uint32_t color) {
    if (!vbe_z_valid(z)) return;
    memset(vbe_z_layers[z], color, fb_size_bytes);
}

void vbe_clear_all_z_layers(void) {
    for (uint32_t z=1; z<VBE_NUM_Z_LAYERS; z++) {
        if (vbe_z_layers[z]) memset(vbe_z_layers[z],0,fb_size_bytes);
    }
}

// Copy source z-layer to destination fading alpha by fade_amount.
// fade_amount: amount to subtract from alpha (0-255).
// If src_z == dst_z an in-place fade is applied.
void vbe_z_copy_and_fade(uint32_t src_z, uint32_t dst_z, uint8_t fade_amount) {
    if (!(src_z>0 && src_z < VBE_NUM_Z_LAYERS)) return;
    if (!(dst_z>0 && dst_z < VBE_NUM_Z_LAYERS)) return;
    uint32_t *src = vbe_z_layers[src_z];
    uint32_t *dst = vbe_z_layers[dst_z];
    if (!src || !dst) return;

    if (fade_amount == 0) {
        if (src_z == dst_z) return; // nothing to do
        memcpy(dst, src, fb_size_bytes);
        return;
    }

    uint32_t pixels = fb_size_bytes / sizeof(uint32_t);
    if (src_z == dst_z) {
        for (uint32_t i=0;i<pixels;i++) {
            uint32_t c = src[i];
            if (c == 0) continue;
            uint8_t a = (uint8_t)(c >> 24);
            if (a <= fade_amount) { src[i] = 0; continue; }
            a -= fade_amount;
            src[i] = ((uint32_t)a << 24) | (c & 0x00FFFFFFu);
        }
    } else {
        for (uint32_t i=0;i<pixels;i++) {
            uint32_t c = src[i];
            if (c == 0) { dst[i] = 0; continue; }
            uint8_t a = (uint8_t)(c >> 24);
            if (a <= fade_amount) { dst[i] = 0; continue; }
            a -= fade_amount;
            dst[i] = ((uint32_t)a << 24) | (c & 0x00FFFFFFu);
        }
    }
}