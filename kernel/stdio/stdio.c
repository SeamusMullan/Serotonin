#include "../stdlib/stdlib.h"
#include "../tty.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>
#include "../string.h"
#include "../video/vbe/vbe.h"
#include "../schedule/schedule.h"
#include "../io/serial.h"

static uint32_t printfs_status_mask = 0xFFFFFFFF; 

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
                        serial_putchar(COM1_BASE, pad_char);
                        width--;
                    }
                    break;
                }

                case 's':
                    char* str_arg = (char*)*arg_ptr++;
                    serial_puts(COM1_BASE, str_arg);
                    vbe_terminal_puts(str_arg);
                    break;

                case 'c':
                    vbe_terminal_putchar((char)(intptr_t)*arg_ptr++);
                    serial_putchar(COM1_BASE, (char)(intptr_t)*arg_ptr++);
                    break;

                case 'p': {
                    void* ptr = *arg_ptr++;
                    uintptr_t addr = (uintptr_t)ptr;
                    vbe_terminal_puts("0x");
                    serial_puts(COM1_BASE, "0x");

                    utoa_hex(addr, buffer);

                    int len = strlen(buffer);
                    while (len < width) {
                        vbe_terminal_putchar(pad_char);
                        serial_putchar(COM1_BASE, pad_char);
                        width--;
                    }

                    vbe_terminal_puts(buffer);
                    serial_puts(COM1_BASE, buffer);
                    break;
                }

                case 'f': {
                    double val = *(double*)arg_ptr;
                    arg_ptr++;

                    ftoa(val, buffer, 6); // 6 decimal places
                    vbe_terminal_puts(buffer);
                    serial_puts(COM1_BASE, buffer);
                    break;
                }

                default:
                    vbe_terminal_putchar('%');
                    vbe_terminal_putchar(*p);
                    serial_putchar(COM1_BASE, '%');
                    serial_putchar(COM1_BASE, *p);
                    break;
            }

            if (*p == 'x' || *p == 'u' || *p == 'd') {
                vbe_terminal_puts(buffer);
                serial_puts(COM1_BASE, buffer);
            }

        } else {
            // Output normal character fast
            vbe_terminal_putchar(*p);
            serial_putchar(COM1_BASE, *p);
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

void printfs_set_mask(uint32_t mask) {
    printfs_status_mask = mask;
}

int printfs_masked(enum print_status_types status_type) {
    return (printfs_status_mask & (1 << status_type)) != 0;
}

/**
 * @brief Write the status to the TTY.
 *
 * @param status_type The status type to write.
 */
void printfs_write_status(enum print_status_types status_type) {
    vbe_terminal_puts("[");
    serial_puts(COM1_BASE, "[");
    switch (status_type) {
        case PRINT_STATUS_DEBUG:
            vbe_setcolor_bg_palette(VBE_COLOR_LIGHT_BLUE);
            vbe_terminal_puts("DDD");
            serial_puts(COM1_BASE, "DDD");
            break;
        case PRINT_STATUS_INFO:
            vbe_setcolor_bg_palette(VBE_COLOR_BLUE);
            vbe_terminal_puts("III");
            serial_puts(COM1_BASE, "III");
            break;
        case PRINT_STATUS_WARNING:
            vbe_setcolor_bg_palette(VBE_COLOR_BROWN);
            vbe_terminal_puts("WWW");
            serial_puts(COM1_BASE, "WWW");
            break;
        case PRINT_STATUS_ERROR:
            vbe_setcolor_bg_palette(VBE_COLOR_RED);
            vbe_terminal_puts("EEE");
            serial_puts(COM1_BASE, "EEE");
            break;
        case PRINT_STATUS_FATAL:
            vbe_setcolor_bg_palette(VBE_COLOR_RED);
            vbe_terminal_puts("!!!");
            serial_puts(COM1_BASE, "!!!");
            break;
        case PRINT_STATUS_SUCCESS:
            vbe_setcolor_bg_palette(VBE_COLOR_GREEN);
            vbe_setcolor_fg_palette(VBE_COLOR_BLACK);
            vbe_terminal_puts("SSS");
            serial_puts(COM1_BASE, "SSS");
            break;
    }
    vbe_setcolor_bg_palette(VBE_COLOR_BLACK);
    vbe_setcolor_fg_palette(VBE_COLOR_WHITE);
    vbe_terminal_puts("] ");
    serial_puts(COM1_BASE, "] ");
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
    if (!printfs_masked(status_type))
        return;

    const char* p = fmt;

    void** arg_ptr = (void**)(&fmt);
    arg_ptr++; // skip format string itself

    printfs_write_status(status_type);
    printf_internal(p, arg_ptr);
}