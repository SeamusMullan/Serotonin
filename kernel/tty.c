#include <stdint.h>
#include <stddef.h>
#include <kernel/string.h>
#include <kernel/tty.h>
#include <kernel/io/io.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  0xB8000 

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer = (uint16_t*)VGA_MEMORY;

/**
 * @brief Get the color value for a VGA entry.
 * 
 * @param fg The foreground color.
 * @param bg The background color.
 * @return uint8_t The combined color value.
 */
inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) 
{
	return fg | bg << 4;
}

/**
 * @brief Create a VGA entry with a character and color.
 * 
 * @param uc The character to display.
 * @param color The color of the character.
 * @return uint16_t The VGA entry combining character and color.
 */
inline uint16_t vga_entry(unsigned char uc, uint8_t color) 
{
	return (uint16_t) uc | (uint16_t) color << 8;
}

/**
 * @brief Initialize the terminal with default settings.
 * 
 * This function sets the initial cursor position, color, and fills the
 * terminal buffer with blank spaces.
 */
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

/**
 * @brief Set the color for the terminal.
 * 
 * @param color The new color to set.
 */
void tty_setcolor(uint8_t color) 
{
	terminal_color = color;
}

/**
 * @brief Set the color for the terminal using foreground and background colors.
 * 
 * @param fg The foreground color.
 * @param bg The background color.
 */
void tty_putentryat(char c, uint8_t color, size_t x, size_t y) 
{
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

/**
 * @brief Output a character to the terminal.
 * 
 * This function handles newline characters and updates the cursor position.
 * If the end of the line is reached, it wraps to the next line.
 * 
 * @param c The character to output.
 */
void tty_putchar(char c) 
{
    if (c == '\n') {
        terminal_row++;
        terminal_column = 0;
        tty_set_cursor(terminal_column, terminal_row);
        return;
    }
	tty_putentryat(c, terminal_color, terminal_column, terminal_row);
	if (++terminal_column == VGA_WIDTH) {
		terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) 
		    terminal_row = 0;
    }
    tty_set_cursor(terminal_column, terminal_row);
}

/**
 * @brief Write a string to the terminal.
 * 
 * This function writes a specified number of characters to the terminal.
 * 
 * @param data The string to write.
 * @param size The number of characters to write.
 */
void tty_write(const char* data, size_t size) 
{
	for (size_t i = 0; i < size; i++)
		tty_putchar(data[i]);
}

/**
 * @brief Scroll the terminal content up by one line.
 * 
 * This function shifts all lines up by one, effectively removing the top line
 * and making space for new content at the bottom.
 */
void tty_scroll(void)
{
    const size_t start_index = 1 * VGA_WIDTH;
    for (size_t i = start_index; i < VGA_HEIGHT*VGA_WIDTH; i++) {
		terminal_buffer[i-start_index] = terminal_buffer[i];
	}
    terminal_column = 0;
    tty_set_cursor(terminal_column, terminal_row);
}

/**
 * @brief Write a string to the terminal, scrolling if necessary.
 * 
 * This function writes a string to the terminal and scrolls the content
 * if the end of the terminal is reached.
 * 
 * @param data The string to write.
 */
void tty_writestring(const char* data) 
{
    if (terminal_row+1 == VGA_HEIGHT) {
        tty_scroll();
        terminal_row--;
    }
	tty_write(data, strlen(data));
}

/**
 * @brief Move the cursor back by one character.
 */
void tty_back(void)
{
    if (terminal_column == 0 && terminal_row == 0) {
        return;
    }

    if (terminal_column == 0) {
        terminal_row--;
        terminal_column = VGA_WIDTH - 1;
    } else {
        terminal_column--;
    }

    tty_putentryat(' ', terminal_color, terminal_column, terminal_row);
    tty_set_cursor(terminal_column, terminal_row);
}

/**
 * @brief Set the cursor position on the terminal.
 * @param column The column to set the cursor to.
 * @param row The row to set the cursor to.
 */
void tty_set_cursor(int column, int row)
{
    uint16_t position = row * VGA_WIDTH + column;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}