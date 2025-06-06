#include "../stdlib/stdlib.h"
#include "../tty.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>

void printf_internal(const char* p, void** arg_ptr) 
{
    char buffer[32];
    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            char* str = buffer;

            switch (*p) {
                case 'd':
                    itoa((int)(intptr_t)*arg_ptr++, buffer);
                    tty_writestring(str);
                    break;
                case 'u':
                    utoa((uint32_t)(uintptr_t)*arg_ptr++, buffer);
                    tty_writestring(str);
                    break;
                case 'x':
                    utoa_hex((uint32_t)(uintptr_t)*arg_ptr++, buffer);
                    tty_writestring(str);
                    break;
                case 's':
                    tty_writestring((char*)*arg_ptr++);
                    break;
                case 'c':
                    buffer[0] = (char)(intptr_t)*arg_ptr++;
                    buffer[1] = '\0';
                    tty_writestring(buffer);
                    break;
                default:
                    tty_writestring("%");
                    tty_writestring((char[]){*p, '\0'});
                    break;
            }
        } else {
            buffer[0] = *p;
            buffer[1] = '\0';
            tty_writestring(buffer);
        }
        p++;
    }
}

void printf(const char* fmt, ...) 
{
    const char* p = fmt;

    void** arg_ptr = (void**)(&fmt);
    arg_ptr++; // skip format string itself

    printf_internal(p, arg_ptr);
}

void printfs_write_status(enum print_status_types status_type) {
    tty_writestring("[");
    switch (status_type) {
        case PRINT_STATUS_DEBUG:
            tty_setcolor(vga_entry_color(VGA_COLOR_WHITE,VGA_COLOR_LIGHT_BLUE));
            tty_writestring("DDD");
            break;
        case PRINT_STATUS_INFO:
            tty_setcolor(vga_entry_color(VGA_COLOR_WHITE,VGA_COLOR_BLUE));
            tty_writestring("III");
            break;
        case PRINT_STATUS_WARNING:
            tty_setcolor(vga_entry_color(VGA_COLOR_WHITE,VGA_COLOR_LIGHT_RED));
            tty_writestring("WWW");
            break;
        case PRINT_STATUS_ERROR:
            tty_setcolor(vga_entry_color(VGA_COLOR_WHITE,VGA_COLOR_RED));
            tty_writestring("EEE");
            break;
        case PRINT_STATUS_FATAL:
            tty_setcolor(vga_entry_color(VGA_COLOR_WHITE,VGA_COLOR_RED));
            tty_writestring("!!!");
            break;
    }
    tty_setcolor(vga_entry_color(VGA_COLOR_WHITE,VGA_COLOR_BLACK));
    tty_writestring("] ");
}

// Print with status
void printfs(enum print_status_types status_type, const char* fmt, ...) 
{
    const char* p = fmt;

    void** arg_ptr = (void**)(&fmt);
    arg_ptr++; // skip format string itself

    printfs_write_status(status_type);
    printf_internal(p, arg_ptr);
}