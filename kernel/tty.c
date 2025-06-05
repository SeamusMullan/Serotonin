#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "tty.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  0xB8000 

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer = (uint16_t*)VGA_MEMORY;

inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) 
{
	return fg | bg << 4;
}

inline uint16_t vga_entry(unsigned char uc, uint8_t color) 
{
	return (uint16_t) uc | (uint16_t) color << 8;
}

void tty_initialize(void) 
{
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
	
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_color);
		}
	}
}

void tty_setcolor(uint8_t color) 
{
	terminal_color = color;
}

void tty_putentryat(char c, uint8_t color, size_t x, size_t y) 
{
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

void tty_putchar(char c) 
{
    if (c == '\n') {
        terminal_row++;
        terminal_column = 0;
        return;
    }
	tty_putentryat(c, terminal_color, terminal_column, terminal_row);
	if (++terminal_column == VGA_WIDTH) {
		terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) 
		    terminal_row = 0;
    }
}

void tty_write(const char* data, size_t size) 
{
	for (size_t i = 0; i < size; i++)
		tty_putchar(data[i]);
}


void tty_scroll(void)
{
    const size_t start_index = 1 * VGA_WIDTH;
    for (size_t i = start_index; i < VGA_HEIGHT*VGA_WIDTH; i++) {
		terminal_buffer[i-start_index] = terminal_buffer[i];
	}
}

void tty_writestring(const char* data) 
{
    if (terminal_row+1 == VGA_HEIGHT) {
        tty_scroll();
        terminal_row--;
    }
	tty_write(data, strlen(data));
}