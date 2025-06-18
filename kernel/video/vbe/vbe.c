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
#include "../../paging.h"
#include "../font.h"

uint32_t term_fg_color = 0xFFFFFF;
uint32_t term_bg_color = 0x000000;

uint32_t dirty_min_x = 0;
uint32_t dirty_min_y = 0;
uint32_t dirty_max_x = 0;
uint32_t dirty_max_y = 0;
static int vbe_any_dirty = 0;

vbe_mode_info_t vbe_info;

static uint32_t fb_size_bytes;

uint32_t vbe_palette[256];

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
        kernel_panic("vbe_init: phys_to_virt failed");
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
    uint32_t index = y * SCREEN_WIDTH + x;
    dirty_bitmap[index / 8] |= (1 << (index % 8));
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

/**
 * @brief Copy the backbuffer contents to the framebuffer.
 */
void vbe_flip(void) {

    uint32_t stride = vbe_info.pitch / sizeof(uint32_t);
    uint32_t *src_buf = vbe_info.backbuffer;
    uint32_t *dst_buf = vbe_info.framebuffer;

    for (uint32_t y = 0; y < SCREEN_HEIGHT; y++) {
        for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
            uint32_t index = y * SCREEN_WIDTH + x;
            uint8_t byte = dirty_bitmap[index / 8];
            uint8_t mask = (1 << (index % 8));

            if (byte & mask) {
                uint32_t offset = y * stride + x;
                dst_buf[offset] = src_buf[offset];
            }
        }
    }

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
void vbe_setcolor_fg(uint8_t color) {
    term_fg_color = color;
}

/**
 * @brief Set the background color using a 32-bit value (0xAARRGGBB).
 *
 * @param color The new background color.
 */
void vbe_setcolor_bg(uint8_t color) {
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

void vbe_set_cursor(uint32_t col, uint32_t row) {
    if (col >= term_max_cols() || row >= term_max_rows()) {
        return;
    }
    term_cursor_col = col;
    term_cursor_row = row;
}