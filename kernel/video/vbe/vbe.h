#ifndef _VBE_H
#define _VBE_H

#include <stdint.h>
#include "../../multiboot.h"
#include "../font.h"

#define VBE_FONT_WIDTH  8
#define VBE_FONT_HEIGHT 20

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

static uint32_t term_cursor_col = 0;
static uint32_t term_cursor_row = 0;

extern uint32_t vbe_palette[256];

typedef enum {
    VBE_COLOR_BLACK = 0,
    VBE_COLOR_BLUE,
    VBE_COLOR_GREEN,
    VBE_COLOR_CYAN,
    VBE_COLOR_RED,
    VBE_COLOR_MAGENTA,
    VBE_COLOR_BROWN,
    VBE_COLOR_LIGHT_GRAY,
    VBE_COLOR_DARK_GRAY,
    VBE_COLOR_LIGHT_BLUE,
    VBE_COLOR_LIGHT_GREEN,
    VBE_COLOR_LIGHT_CYAN,
    VBE_COLOR_LIGHT_RED,
    VBE_COLOR_LIGHT_MAGENTA,
    VBE_COLOR_YELLOW,
    VBE_COLOR_WHITE
} vbe_color_t;

extern uint32_t vbe_colors[16];

static uint32_t term_color = 0xFFFFFF;

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

void vbe_init(multiboot_info_t *mbi);
void vbe_putpixel(uint32_t x, uint32_t y, uint32_t color);
void vbe_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void vbe_flip(void);
void vbe_drawglyph(FontGlyph *glyph, uint32_t x, uint32_t y, uint32_t color);
void vbe_puts(const char *str, uint32_t x, uint32_t y, uint32_t color);
static uint32_t term_max_cols(void);
static uint32_t term_max_rows(void);
void vbe_terminal_putchar(char c);
void vbe_terminal_puts(const char *str);
void vbe_terminal_back(void);
void vbe_setcolor_fg(uint8_t color);
void vbe_setcolor_bg(uint8_t color);
void vbe_palette_init(void);

#endif
