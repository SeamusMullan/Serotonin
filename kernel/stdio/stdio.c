#include "../stdlib/stdlib.h"
#include "../tty.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>
#include "../string.h"

void printf_internal(const char* p, void** arg_ptr) {
    char buffer[32];

    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            const char* fmt_start = p;

            // --- Parse flags ---
            char pad_char = ' ';
            if (*p == '0') {
                pad_char = '0';
                p++;
            }

            // --- Parse field width ---
            int width = 0;
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }

            // --- Parse length modifier ---
            enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL } length = LEN_NONE;
            if (*p == 'h') {
                if (*(p + 1) == 'h') {
                    length = LEN_HH;
                    p += 2;
                } else {
                    length = LEN_H;
                    p++;
                }
            } else if (*p == 'l') {
                if (*(p + 1) == 'l') {
                    length = LEN_LL;
                    p += 2;
                } else {
                    length = LEN_L;
                    p++;
                }
            }

            // Prepare buffer
            char* str = buffer;

            switch (*p) {
                case 'd': {
                    int val;
                    switch (length) {
                        case LEN_HH: val = (char)(intptr_t)*arg_ptr++; break;
                        case LEN_H:  val = (short)(intptr_t)*arg_ptr++; break;
                        case LEN_L:  val = (long)(intptr_t)*arg_ptr++; break;
                        case LEN_LL: val = (long long)(intptr_t)*arg_ptr++; break;
                        default:     val = (int)(intptr_t)*arg_ptr++; break;
                    }
                    itoa(val, buffer);
                    break;
                }

                case 'u': {
                    unsigned int val;
                    switch (length) {
                        case LEN_HH: val = (unsigned char)(uintptr_t)*arg_ptr++; break;
                        case LEN_H:  val = (unsigned short)(uintptr_t)*arg_ptr++; break;
                        case LEN_L:  val = (unsigned long)(uintptr_t)*arg_ptr++; break;
                        case LEN_LL: val = (unsigned long long)(uintptr_t)*arg_ptr++; break;
                        default:     val = (unsigned int)(uintptr_t)*arg_ptr++; break;
                    }
                    utoa(val, buffer);
                    break;
                }

                case 'x': {
                    unsigned int val;
                    switch (length) {
                        case LEN_HH: val = (unsigned char)(uintptr_t)*arg_ptr++; break;
                        case LEN_H:  val = (unsigned short)(uintptr_t)*arg_ptr++; break;
                        case LEN_L:  val = (unsigned long)(uintptr_t)*arg_ptr++; break;
                        case LEN_LL: val = (unsigned long long)(uintptr_t)*arg_ptr++; break;
                        default:     val = (unsigned int)(uintptr_t)*arg_ptr++; break;
                    }
                    utoa_hex(val, buffer);

                    // Apply zero-padding if necessary
                    int len = strlen(buffer);
                    while (len < width) {
                        tty_putchar(pad_char);
                        width--;
                    }
                    break;
                }

                case 's':
                    tty_writestring((char*)*arg_ptr++);
                    break;

                case 'c':
                    buffer[0] = (char)(intptr_t)*arg_ptr++;
                    buffer[1] = '\0';
                    tty_writestring(buffer);
                    break;

                case 'p': {
                    void* ptr = *arg_ptr++;
                    uintptr_t addr = (uintptr_t)ptr;
                    tty_writestring("0x");

                    utoa_hex(addr, buffer);

                    int len = strlen(buffer);
                    while (len < width) {
                        tty_putchar(pad_char);
                        width--;
                    }

                    tty_writestring(buffer);
                    break;
                }


                default:
                    tty_putchar('%');
                    tty_putchar(*p);
                    break;
            }

            // Output formatted string
            if (*p == 'x' || *p == 'u' || *p == 'd') {
                tty_writestring(buffer);
            }

        } else {
            // Just a normal character
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