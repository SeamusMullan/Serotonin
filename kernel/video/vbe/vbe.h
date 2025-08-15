#ifndef _VBE_H
#define _VBE_H

#include <stdint.h>
#include "../../multiboot.h"
#include "../font.h"

#define VBE_FONT_WIDTH  8
#define VBE_FONT_HEIGHT 20

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 800

static uint32_t term_cursor_col = 0;
static uint32_t term_cursor_row = 0;

extern uint32_t vbe_palette[256];

/**
 * @brief Enumeration of VBE colors.
 *
 * Used to define the colours supported by the VBE.
 *
 */
typedef enum {
    VBE_COLOR_BLACK = 0,
    VBE_COLOR_BLUE = 1,
    VBE_COLOR_GREEN = 2,
    VBE_COLOR_CYAN = 3,
    VBE_COLOR_RED = 4,
    VBE_COLOR_MAGENTA = 5,
    VBE_COLOR_BROWN = 6,
    VBE_COLOR_LIGHT_GRAY = 7,
    VBE_COLOR_DARK_GRAY = 8,
    VBE_COLOR_LIGHT_BLUE = 9,
    VBE_COLOR_LIGHT_GREEN = 10,
    VBE_COLOR_LIGHT_CYAN = 11,
    VBE_COLOR_LIGHT_RED = 12,
    VBE_COLOR_LIGHT_MAGENTA = 13,
    VBE_COLOR_YELLOW = 14,
    VBE_COLOR_WHITE = 15
} vbe_color_t;

extern uint32_t vbe_colors[16];

static uint32_t term_color = 0xFFFFFF;

static int vbe_any_dirty;

// We only need width, height, pitch, bpp, and backbuffer pointer here:
typedef struct {
    uint32_t *framebuffer;   // virtual address of frontbuffer
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;         // bytes per scanline
    uint32_t  bpp;           // bits per pixel
    uint32_t *backbuffer;    // virtual address of our malloc’d backbuffer
} vbe_mode_info_t;

extern vbe_mode_info_t vbe_info;
extern uint32_t fb_size_bytes;

void vbe_init(multiboot_info_t *mbi);
void vbe_putpixel(uint32_t x, uint32_t y, uint32_t color);
void vbe_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void vbe_flip(void);
void vbe_flip_all(void);
void vbe_drawglyph(FontGlyph *glyph, uint32_t x, uint32_t y, uint32_t color);
void vbe_puts(const char *str, uint32_t x, uint32_t y, uint32_t color);
static uint32_t term_max_cols(void);
static uint32_t term_max_rows(void);
void vbe_terminal_putchar(char c);
void vbe_terminal_puts(const char *str);
void vbe_terminal_back(void);
void vbe_setcolor_fg(uint32_t color);
void vbe_setcolor_bg(uint32_t color);
void vbe_palette_init(void);
void vbe_setcolor_fg_palette(vbe_color_t color);
void vbe_setcolor_bg_palette(vbe_color_t color);
void vbe_fast_draw_hline(uint32_t *buf, uint32_t pitch, uint32_t x, uint32_t y, uint32_t w, uint32_t color);
void vbe_fast_putpixel(uint32_t x, uint32_t y, uint32_t color);
void vbe_fast_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void vbe_set_cursor(uint32_t col, uint32_t row);
void vbe_clear_screen(uint32_t color);

#define VBE_NUM_Z_LAYERS 8

void vbe_z_putpixel(uint32_t z, uint32_t x, uint32_t y, uint32_t color);
void vbe_z_fillrect(uint32_t z, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void vbe_clear_z_layer(uint32_t z, uint32_t color);
void vbe_clear_all_z_layers(void);
// Copy source z-layer (src_z) to destination (dst_z) fading alpha by fade_amount (0-255).
// If resulting alpha <= 0 it becomes fully transparent (pixel value 0).
void vbe_z_copy_and_fade(uint32_t src_z, uint32_t dst_z, uint8_t fade_amount);


#endif
