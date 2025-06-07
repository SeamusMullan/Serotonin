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

    uint32_t *virt_fb = (uint32_t *)phys_to_virt(phys_fb);
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

    dirty_min_x = 0;
    dirty_min_y = 0;
    dirty_max_x = vbe_info.width - 1;
    dirty_max_y = vbe_info.height - 1;
    vbe_any_dirty = 1;

    {
        uintptr_t back_start = (uintptr_t)vbe_info.backbuffer;
        uintptr_t back_end   = back_start + fb_size_bytes;
        if (back_start < KERNEL_HEAP_VMA || back_end > (KERNEL_HEAP_VMA + KERNEL_HEAP_SIZE)) {
            kernel_panic("vbe_init: backbuffer out of heap bounds");
        }
    }
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
    vbe_fast_mark_dirty(x, y, 1, 1);
}

/**
 * @brief Mark a rectangular region as dirty.
 * 
 * @param x The x-coordinate of the region.
 * @param y The y-coordinate of the region.
 * @param w Width of the region.
 * @param h Height of the region.
 */
void vbe_fast_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;

    uint32_t max_x = x + w - 1;
    uint32_t max_y = y + h - 1;

    // Clamp to framebuffer size
    if (max_x >= vbe_info.width)  max_x = vbe_info.width - 1;
    if (max_y >= vbe_info.height) max_y = vbe_info.height - 1;

    if (!vbe_any_dirty) {
        dirty_min_x = x;    dirty_min_y = y;
        dirty_max_x = x+w-1; dirty_max_y = y+h-1;
        vbe_any_dirty = 1;
    } else {
        if (x             < dirty_min_x) dirty_min_x = x;
        if (y             < dirty_min_y) dirty_min_y = y;
        if (x + w - 1     > dirty_max_x) dirty_max_x = x + w - 1;
        if (y + h - 1     > dirty_max_y) dirty_max_y = y + h - 1;
    }
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
    }
}

/**
 * @brief Fill a rectangle on the screen with a specific color.
 * 
 * @param x X-coordinate of the top-left corner.
 * @param y Y-coordinate of the top-left corner.
 * @param w Width of the rectangle.
 * @param h Height of the rectangle.
 * @param color Fill color.
 */
void vbe_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (x + w  > vbe_info.width)  w = vbe_info.width  - x;
    if (y + h  > vbe_info.height) h = vbe_info.height - y;
    for (uint32_t row = 0; row < h; row++) {
        vbe_fast_draw_hline(vbe_info.backbuffer, vbe_info.pitch, x, y + row, w, color);
    }
    vbe_fast_mark_dirty(x, y, w, h);
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
    // just cpy for now
    memcpy(vbe_info.framebuffer,vbe_info.backbuffer,fb_size_bytes);
    return;

    // todo: fix this shit
    if (!vbe_any_dirty) return;

    if (dirty_min_x >= vbe_info.width) dirty_min_x = vbe_info.width - 1;
    if (dirty_max_x >= vbe_info.width) dirty_max_x = vbe_info.width - 1;
    if (dirty_min_y >= vbe_info.height) dirty_min_y = vbe_info.height - 1;
    if (dirty_max_y >= vbe_info.height) dirty_max_y = vbe_info.height - 1;

    uint32_t bytes_per_pixel = vbe_info.bpp / 8;

    // Safe fallback: copy entire rows regardless of dirty_min_x
    for (uint32_t y = dirty_min_y; y <= dirty_max_y; y++) {
        uint8_t *src = (uint8_t*)vbe_info.backbuffer + y * vbe_info.pitch;
        uint8_t *dst = (uint8_t*)vbe_info.framebuffer + y * vbe_info.pitch;

        // *** FULL ROW COPY ***
        memcpy(dst, src, vbe_info.pitch);
    }

    vbe_any_dirty = 0;
    dirty_min_x = dirty_min_y = 0;
    dirty_max_x = dirty_max_y = 0;
}

/**
 * @brief Draw a font glyph at a given position with a given color.
 *
 * @param glyph The font glyph to draw.
 * @param x X-coordinate of the glyph.
 * @param y Y-coordinate of the glyph.
 * @param color Color to draw the glyph.
 */
void vbe_drawglyph(FontGlyph *glyph, uint32_t x, uint32_t y, uint32_t color) {
    if (!glyph) return;
    for (uint32_t row = 0; row < VBE_FONT_HEIGHT; row++) {
        uint8_t bits = glyph->data[row];
        uint8_t *rowptr = (uint8_t *)vbe_info.backbuffer + (y+row)*vbe_info.pitch;
        uint32_t *dst = (uint32_t *)(rowptr + x*4);

        uint32_t m0 = -(uint32_t)((bits >> 7) & 1);
        uint32_t m1 = -(uint32_t)((bits >> 6) & 1);
        uint32_t m2 = -(uint32_t)((bits >> 5) & 1);
        uint32_t m3 = -(uint32_t)((bits >> 4) & 1);
        uint32_t m4 = -(uint32_t)((bits >> 3) & 1);
        uint32_t m5 = -(uint32_t)((bits >> 2) & 1);
        uint32_t m6 = -(uint32_t)((bits >> 1) & 1);
        uint32_t m7 = -(uint32_t)((bits >> 0) & 1);

        dst[0] = (color & m0) | (dst[0] & ~m0);
        dst[1] = (color & m1) | (dst[1] & ~m1);
        dst[2] = (color & m2) | (dst[2] & ~m2);
        dst[3] = (color & m3) | (dst[3] & ~m3);
        dst[4] = (color & m4) | (dst[4] & ~m4);
        dst[5] = (color & m5) | (dst[5] & ~m5);
        dst[6] = (color & m6) | (dst[6] & ~m6);
        dst[7] = (color & m7) | (dst[7] & ~m7);
    }
    vbe_fast_mark_dirty(x, y, VBE_FONT_WIDTH, VBE_FONT_HEIGHT);
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
    vbe_fast_mark_dirty(x, y, 1, 1);
}
