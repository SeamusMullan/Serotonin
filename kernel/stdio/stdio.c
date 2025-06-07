#include "../stdlib/stdlib.h"
#include "../tty.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>
#include "../string.h"
#include "../video/vbe/vbe.h"

/**
 * @brief Internal printf function.
 * 
 * @param p Format string.
 * @param arg_ptr Pointer to the argument list.
 */
void printf_internal(const char* p, void** arg_ptr) {
    char buffer[32];

    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            const char* fmt_start = p;

            char pad_char = ' ';
            if (*p == '0') {
                pad_char = '0';
                p++;
            }

            int width = 0;
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }

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

                    int len = strlen(buffer);
                    while (len < width) {
                        vbe_terminal_putchar(pad_char);
                        width--;
                    }
                    break;
                }

                case 's':
                    vbe_terminal_puts((char*)*arg_ptr++);
                    break;

                case 'c':
                    vbe_terminal_putchar((char)(intptr_t)*arg_ptr++);
                    break;

                case 'p': {
                    void* ptr = *arg_ptr++;
                    uintptr_t addr = (uintptr_t)ptr;
                    vbe_terminal_puts("0x");

                    utoa_hex(addr, buffer);

                    int len = strlen(buffer);
                    while (len < width) {
                        vbe_terminal_putchar(pad_char);
                        width--;
                    }

                    vbe_terminal_puts(buffer);
                    break;
                }

                case 'f': {
                    double val = *(double*)arg_ptr;
                    arg_ptr++;

                    ftoa(val, buffer, 6); // 6 decimal places
                    vbe_terminal_puts(buffer);
                    break;
                }

                default:
                    vbe_terminal_putchar('%');
                    vbe_terminal_putchar(*p);
                    break;
            }

            if (*p == 'x' || *p == 'u' || *p == 'd') {
                vbe_terminal_puts(buffer);
            }

        } else {
            // Output normal character fast
            vbe_terminal_putchar(*p);
        }
        p++;
    }

    vbe_flip();
}

/**
 * @brief Print formatted output.
 *
 * @param fmt Format string.
 * @param ... Variable arguments.
 */
void printf(const char* fmt, ...) 
{
    const char* p = fmt;

    void** arg_ptr = (void**)(&fmt);
    arg_ptr++; // skip format string itself

    printf_internal(p, arg_ptr);
}

/**
 * @brief Write the status to the TTY.
 *
 * @param status_type The status type to write.
 */
void printfs_write_status(enum print_status_types status_type) {
    vbe_terminal_puts("[");
    switch (status_type) {
        case PRINT_STATUS_DEBUG:
            vbe_setcolor_bg_palette(VBE_COLOR_LIGHT_BLUE);
            vbe_terminal_puts("DDD");
            break;
        case PRINT_STATUS_INFO:
            vbe_setcolor_bg_palette(VBE_COLOR_BLUE);
            vbe_terminal_puts("III");
            break;
        case PRINT_STATUS_WARNING:
            vbe_setcolor_bg_palette(VBE_COLOR_BROWN);
            vbe_terminal_puts("WWW");
            break;
        case PRINT_STATUS_ERROR:
            vbe_setcolor_bg_palette(VBE_COLOR_RED);
            vbe_terminal_puts("EEE");
            break;
        case PRINT_STATUS_FATAL:
            vbe_setcolor_bg_palette(VBE_COLOR_RED);
            vbe_terminal_puts("!!!");
            break;
        case PRINT_STATUS_SUCCESS:
            vbe_setcolor_bg_palette(VBE_COLOR_GREEN);
            vbe_terminal_puts("SSS");
            break;
    }
    vbe_setcolor_bg_palette(VBE_COLOR_BLACK);
    vbe_terminal_puts("] ");
}

/**
 * @brief Print formatted string with status.
 *
 * @param status_type The status type to print.
 * @param fmt Format string.
 * @param ... Variable arguments.
 */
void printfs(enum print_status_types status_type, const char* fmt, ...) 
{
    const char* p = fmt;

    void** arg_ptr = (void**)(&fmt);
    arg_ptr++; // skip format string itself

    printfs_write_status(status_type);
    printf_internal(p, arg_ptr);
}