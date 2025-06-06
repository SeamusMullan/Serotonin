#include "vbe.h"
#include "../../kernel.h"      // kernel_malloc(), kernel_panic()
#include "../../stdlib/stdlib.h" // for memcpy
#include <stdint.h>
#include <stddef.h>
#include "../../multiboot.h"
#include "../../paging.h"
#include "../font.h"

int terminal_dirty = 0;
uint32_t term_fg_color = 0xFFFFFF;
uint32_t term_bg_color = 0x000000;

vbe_mode_info_t vbe_info;

static uint32_t fb_size_bytes;

uint32_t vbe_palette[256];

uint32_t vbe_colors[16] = {
    0x000000, // BLACK
    0x0000AA, // BLUE
    0x00AA00, // GREEN
    0x00AAAA, // CYAN
    0xAA0000, // RED
    0xAA00AA, // MAGENTA
    0xAA5500, // BROWN / YELLOW
    0xAAAAAA, // LIGHT GRAY
    0x555555, // DARK GRAY
    0x5555FF, // LIGHT BLUE
    0x55FF55, // LIGHT GREEN
    0x55FFFF, // LIGHT CYAN
    0xFF5555, // LIGHT RED
    0xFF55FF, // LIGHT MAGENTA
    0xFFFF55, // YELLOW / LIGHT YELLOW
    0xFFFFFF  // WHITE
};

// Must be called *after* paging_init((uintptr_t)mbi->framebuffer_addr) has run.
void vbe_init(multiboot_info_t *mbi) {
    // 1) Grab the physical‐address fields from multiboot
    uint32_t phys_fb = (uint32_t)(mbi->framebuffer_addr);
    uint32_t pitch   = (uint32_t)(mbi->framebuffer_pitch);
    uint32_t width   = (uint32_t)(mbi->framebuffer_width);
    uint32_t height  = (uint32_t)(mbi->framebuffer_height);
    uint32_t bpp     = (uint32_t)(mbi->framebuffer_bpp);

    // 2) Convert frontbuffer to a virtual pointer:
    uint32_t *virt_fb = (uint32_t *)phys_to_virt(phys_fb);
    if (!virt_fb) {
        kernel_panic("vbe_init: phys_to_virt failed");
    }

    // 3) Fill in the vbe_info struct:
    vbe_info.framebuffer = virt_fb;
    vbe_info.width       = width;
    vbe_info.height      = height;
    vbe_info.pitch       = pitch;
    vbe_info.bpp         = bpp;

    // 4) Allocate a backbuffer of exactly (height * pitch) bytes
    fb_size_bytes = (uint32_t)height * pitch;
    // Make sure to PAGE‐ALIGN if size ≥ PAGE_SIZE. kernel_malloc already does this internally.
    vbe_info.backbuffer = (uint32_t *)kernel_malloc(fb_size_bytes);
    if (!vbe_info.backbuffer) {
        kernel_panic("vbe_init: could not allocate backbuffer");
    }

    // OPTIONAL sanity checks (you can remove these after you verify they work):
    {
        uintptr_t back_start = (uintptr_t)vbe_info.backbuffer;
        uintptr_t back_end   = back_start + fb_size_bytes;
        // Check that backbuffer lies entirely within the heap’s virtual range:
        if (back_start < KERNEL_HEAP_VMA || back_end > (KERNEL_HEAP_VMA + KERNEL_HEAP_SIZE)) {
            kernel_panic("vbe_init: backbuffer out of heap bounds");
        }
    }
}

// A “safe” way to write a 32-bit pixel even when pitch is not a multiple of 4
void vbe_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= vbe_info.width || y >= vbe_info.height) return;

    // (1) Compute the byte-address of the start of scanline y in the backbuffer:
    uint8_t *row_start = (uint8_t *)vbe_info.backbuffer + (y * vbe_info.pitch);
    // (2) Advance x pixels (4 bytes each):
    uint32_t *dest = (uint32_t *)(row_start + (x * 4));
    *dest = color;

    terminal_dirty = 1;
}

// Fill a rectangle by calling putpixel for each pixel in it
void vbe_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t dy = 0; dy < h; dy++) {
        for (uint32_t dx = 0; dx < w; dx++) {
            vbe_putpixel(x + dx, y + dy, color);
        }
    }

    terminal_dirty = 1;
}

// Copy the entire backbuffer into the frontbuffer
void vbe_flip(void) {
    if (!terminal_dirty) return; 
    // frontbuffer is at vbe_info.framebuffer
    // backbuffer is at vbe_info.backbuffer
    // total bytes = fb_size_bytes
    memcpy(vbe_info.framebuffer, vbe_info.backbuffer, fb_size_bytes);

    terminal_dirty = 0;
}

void vbe_drawglyph(FontGlyph *glyph, uint32_t x, uint32_t y, uint32_t color) {
    if (!glyph) return;

    uint8_t *row_base;
    uint32_t *pixel_ptr;

    for (uint32_t row = 0; row < VBE_FONT_HEIGHT; row++) {
        uint8_t row_data = glyph->data[row];

        // Get pointer to start of this scanline in backbuffer:
        row_base = (uint8_t *)vbe_info.backbuffer + (y + row) * vbe_info.pitch;
        pixel_ptr = (uint32_t *)(row_base + x * 4);

        // Unroll the 8 bits manually:
        if (row_data & 0x80) pixel_ptr[0] = color;
        if (row_data & 0x40) pixel_ptr[1] = color;
        if (row_data & 0x20) pixel_ptr[2] = color;
        if (row_data & 0x10) pixel_ptr[3] = color;
        if (row_data & 0x08) pixel_ptr[4] = color;
        if (row_data & 0x04) pixel_ptr[5] = color;
        if (row_data & 0x02) pixel_ptr[6] = color;
        if (row_data & 0x01) pixel_ptr[7] = color;
    }

    terminal_dirty = 1;
}

void vbe_puts(const char *str, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t orig_x = x;

    while (*str) {
        if (*str == '\n') {
            // Newline -> move to next line
            y += 32;
            x = orig_x;
        } else {
            // Draw one glyph
            FontGlyph *glyph = find_glyph((uint8_t)(*str));
            if (glyph) {
                vbe_drawglyph(glyph, x, y, color);
            }
            // Advance to next char (8 pixels per glyph)
            x += 8;
        }
        str++;
    }

    terminal_dirty = 1;
}

static uint32_t term_max_cols(void) {
    return vbe_info.width / VBE_FONT_WIDTH;
}

static uint32_t term_max_rows(void) {
    return vbe_info.height / VBE_FONT_HEIGHT;
}

void vbe_terminal_putchar(char c) {
    if (c == '\n') {
        // Newline → next row
        term_cursor_col = 0;
        term_cursor_row++;
    } else if (c == '\r') {
        // Carriage return → reset column
        term_cursor_col = 0;
    } else {
        // Draw one character
        FontGlyph *glyph = find_glyph((uint8_t)c);
        if (glyph) {
            uint32_t px = term_cursor_col * VBE_FONT_WIDTH;
            uint32_t py = term_cursor_row * VBE_FONT_HEIGHT;
            vbe_fillrect(px, py, VBE_FONT_WIDTH, VBE_FONT_HEIGHT, term_bg_color);
            vbe_drawglyph(glyph, px, py, term_fg_color);
        }
        // Advance to next column
        term_cursor_col++;
    }

    // Wrap column
    if (term_cursor_col >= term_max_cols()) {
        term_cursor_col = 0;
        term_cursor_row++;
    }

    // Scroll if needed
    if (term_cursor_row >= term_max_rows()) {
        // Simple scroll: move all lines up by one row (VBE_FONT_HEIGHT pixels)

        // Move framebuffer up
        uint32_t bytes_per_row = vbe_info.pitch * VBE_FONT_HEIGHT;
        uint32_t visible_rows = vbe_info.height - VBE_FONT_HEIGHT;

        // Use memmove for overlap-safe copy
        memmove(vbe_info.backbuffer,
                (uint8_t *)vbe_info.backbuffer + bytes_per_row,
                visible_rows * vbe_info.pitch);

        // Clear last row
        vbe_fillrect(0, visible_rows, vbe_info.width, VBE_FONT_HEIGHT, term_bg_color);

        term_cursor_row = term_max_rows() - 1;
    }

    terminal_dirty = 1;
}

void vbe_terminal_puts(const char *str) {
    while (*str) {
        vbe_terminal_putchar(*str++);
    }
}

void vbe_terminal_back(void) {
    if (term_cursor_col == 0 && term_cursor_row == 0) {
        // Already at top-left → nothing to do
        return;
    }

    if (term_cursor_col == 0) {
        // Move up one row
        term_cursor_row--;
        term_cursor_col = term_max_cols() - 1;
    } else {
        // Move left
        term_cursor_col--;
    }

    // Erase the glyph (fill with black)
    uint32_t px = term_cursor_col * VBE_FONT_WIDTH;
    uint32_t py = term_cursor_row * VBE_FONT_HEIGHT;
    vbe_fillrect(px, py, VBE_FONT_WIDTH, VBE_FONT_HEIGHT, 0x000000);

    // Mark terminal dirty so vbe_flip knows
    terminal_dirty = 1;
}

void vbe_setcolor_fg(uint8_t color) {
    term_fg_color = color;
}

void vbe_setcolor_bg(uint8_t color) {
    term_bg_color = color;
}

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
