#include <kernel/stdlib/stdlib.h>
#include <kernel/tty.h>
#include <kernel/stdio/stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/string.h>
#include <kernel/video/vbe/vbe.h>
#include <kernel/schedule/schedule.h>
#include <kernel/vmm/vmm.h>
#include <kernel/vmm/paging_init.h>
#include <kernel/io/io.h>

static uint32_t printfs_status_mask = 0xFFFFFFFF;

static int copy_user_string(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0 || !src || !current_task || !current_task->address_space) {
        return -1;
    }
    if ((uintptr_t)src < USER_SPACE_START || (uintptr_t)src > USER_SPACE_END) {
        return -1;
    }

    size_t off = 0;
    while (off + 1 < dst_size) {
        uint32_t va = (uint32_t)(uintptr_t)(src + off);
        uint32_t phys = get_mapping(current_task->address_space, va);
        if (!phys) {
            return -1;
        }

        clear_interrupts();
        // cppcheck-suppress constVariablePointer
        uint8_t *mapped = (uint8_t *)kmap(phys);
        uint8_t c = mapped[va & (PAGE_SIZE - 1)];
        kunmap();
        enable_interrupts();

        dst[off++] = (char)c;
        if (c == '\0') {
            return 0;
        }
    }

    dst[dst_size - 1] = '\0';
    return 0;
}

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
            // cppcheck-suppress unreadVariable
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

            // cppcheck-suppress constVariablePointer
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
                    kitoa(val, buffer);
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
                    kutoa(val, buffer);
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
                    char* str_arg = (char*)*arg_ptr++;
                    if (current_task && current_task->priv == CPU_USER_MODE &&
                        (uintptr_t)str_arg <= USER_SPACE_END) {
                        char tmp[256];
                        if (copy_user_string(tmp, sizeof(tmp), str_arg) != 0) {
                            const char *bad = "<badptr>";
                            vbe_terminal_puts(bad, 0);
                        } else {
                            vbe_terminal_puts(tmp, 0);
                        }
                    } else {
                        vbe_terminal_puts(str_arg, 0);
                    }
                    break;

                case 'c':
                    vbe_terminal_putchar((char)(intptr_t)*arg_ptr++);
                    break;

                case 'p': {
                    void* ptr = *arg_ptr++;
                    uintptr_t addr = (uintptr_t)ptr;
                    vbe_terminal_puts("0x", 0);

                    utoa_hex(addr, buffer);

                    int len = strlen(buffer);
                    while (len < width) {
                        vbe_terminal_putchar(pad_char);
                        width--;
                    }

                    vbe_terminal_puts(buffer, 0);
                    break;
                }

                case 'f': {
                    double val = *(double*)arg_ptr;
                    arg_ptr++;

                    ftoa(val, buffer, 6); // 6 decimal places
                    vbe_terminal_puts(buffer, 0);
                    break;
                }

                default:
                    vbe_terminal_putchar('%');
                    vbe_terminal_putchar(*p);
                    break;
            }

            if (*p == 'x' || *p == 'u' || *p == 'd') {
                vbe_terminal_puts(buffer, 0);
            }

        } else {
            char ps[2] = {*p, 0};
            vbe_terminal_puts(ps, 0);
        }
        p++;
    }
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
    vbe_terminal_puts("[", 0);
    switch (status_type) {
        case PRINT_STATUS_DEBUG:
            vbe_setcolor_bg_palette(VBE_COLOR_LIGHT_BLUE);
            vbe_terminal_puts("DDD", 0);
            break;
        case PRINT_STATUS_INFO:
            vbe_setcolor_bg_palette(VBE_COLOR_BLUE);
            vbe_terminal_puts("III", 0);
            break;
        case PRINT_STATUS_WARNING:
            vbe_setcolor_bg_palette(VBE_COLOR_BROWN);
            vbe_terminal_puts("WWW", 0);
            break;
        case PRINT_STATUS_ERROR:
            vbe_setcolor_bg_palette(VBE_COLOR_RED);
            vbe_terminal_puts("EEE", 0);
            break;
        case PRINT_STATUS_FATAL:
            vbe_setcolor_bg_palette(VBE_COLOR_RED);
            vbe_terminal_puts("!!!", 0);
            break;
        case PRINT_STATUS_SUCCESS:
            vbe_setcolor_bg_palette(VBE_COLOR_GREEN);
            vbe_setcolor_fg_palette(VBE_COLOR_BLACK);
            vbe_terminal_puts("SSS", 0);
            break;
    }
    vbe_setcolor_bg_palette(VBE_COLOR_BLACK);
    vbe_setcolor_fg_palette(VBE_COLOR_WHITE);
    vbe_terminal_puts("] ", 0);
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

/**
 * @brief Write a character to buffer if space permits.
 *
 * @param buf Pointer to current buffer position.
 * @param end Pointer to one past the last writable position.
 * @param c Character to write.
 * @return New buffer position.
 */
// cppcheck-suppress constParameterPointer
static char* buf_putchar(char* buf, char* end, char c) {
    if (buf < end) {
        *buf = c;
    }
    return buf + 1;
}

/**
 * @brief Write a string to buffer if space permits.
 *
 * @param buf Pointer to current buffer position.
 * @param end Pointer to one past the last writable position.
 * @param s String to write.
 * @return New buffer position.
 */
static char* buf_puts(char* buf, char* end, const char* s) {
    while (*s) {
        buf = buf_putchar(buf, end, *s++);
    }
    return buf;
}

/**
 * @brief Internal sprintf function that writes to a buffer.
 *
 * @param buf Output buffer (can be NULL for counting only).
 * @param size Buffer size (0 for unlimited/sprintf behavior).
 * @param p Format string.
 * @param arg_ptr Pointer to the argument list.
 * @return Number of characters that would have been written (excluding null terminator).
 */
static int sprintf_internal(char* buf, size_t size, const char* p, void** arg_ptr) {
    char tmpbuf[32];
    char* out = buf;
    char* end = (size > 0) ? (buf + size - 1) : (char*)(uintptr_t)-1;

    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;

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

            // cppcheck-suppress constVariablePointer
            char* str = tmpbuf;

            switch (*p) {
                case 'd': {
                    long long val;
                    switch (length) {
                        case LEN_HH: val = (char)(intptr_t)*arg_ptr++; break;
                        case LEN_H:  val = (short)(intptr_t)*arg_ptr++; break;
                        case LEN_L:  val = (long)(intptr_t)*arg_ptr++; break;
                        case LEN_LL: val = (long long)(intptr_t)*arg_ptr++; break;
                        default:     val = (int)(intptr_t)*arg_ptr++; break;
                    }
                    lltoa(val, tmpbuf);

                    int len = strlen(tmpbuf);
                    while (len < width) {
                        out = buf_putchar(out, end, pad_char);
                        width--;
                    }
                    out = buf_puts(out, end, tmpbuf);
                    break;
                }

                case 'u': {
                    unsigned long long val;
                    switch (length) {
                        case LEN_HH: val = (unsigned char)(uintptr_t)*arg_ptr++; break;
                        case LEN_H:  val = (unsigned short)(uintptr_t)*arg_ptr++; break;
                        case LEN_L:  val = (unsigned long)(uintptr_t)*arg_ptr++; break;
                        case LEN_LL: val = (unsigned long long)(uintptr_t)*arg_ptr++; break;
                        default:     val = (unsigned int)(uintptr_t)*arg_ptr++; break;
                    }
                    ulltoa(val, tmpbuf);

                    int len = strlen(tmpbuf);
                    while (len < width) {
                        out = buf_putchar(out, end, pad_char);
                        width--;
                    }
                    out = buf_puts(out, end, tmpbuf);
                    break;
                }

                case 'x': {
                    unsigned long long val;
                    switch (length) {
                        case LEN_HH: val = (unsigned char)(uintptr_t)*arg_ptr++; break;
                        case LEN_H:  val = (unsigned short)(uintptr_t)*arg_ptr++; break;
                        case LEN_L:  val = (unsigned long)(uintptr_t)*arg_ptr++; break;
                        case LEN_LL: val = (unsigned long long)(uintptr_t)*arg_ptr++; break;
                        default:     val = (unsigned int)(uintptr_t)*arg_ptr++; break;
                    }
                    ulltoa_hex(val, tmpbuf);

                    int len = strlen(tmpbuf);
                    while (len < width) {
                        out = buf_putchar(out, end, pad_char);
                        width--;
                    }
                    out = buf_puts(out, end, tmpbuf);
                    break;
                }

                case 's': {
                    // cppcheck-suppress constVariablePointer
                    char* str_arg = (char*)*arg_ptr++;
                    if (!str_arg) {
                        str_arg = "(null)";
                    }
                    out = buf_puts(out, end, str_arg);
                    break;
                }

                case 'c':
                    out = buf_putchar(out, end, (char)(intptr_t)*arg_ptr++);
                    break;

                case 'p': {
                    void* ptr = *arg_ptr++;
                    uintptr_t addr = (uintptr_t)ptr;
                    out = buf_puts(out, end, "0x");
                    ulltoa_hex(addr, tmpbuf);

                    int len = strlen(tmpbuf);
                    while (len < width) {
                        out = buf_putchar(out, end, pad_char);
                        width--;
                    }
                    out = buf_puts(out, end, tmpbuf);
                    break;
                }

                case 'f': {
                    double val = *(double*)arg_ptr;
                    arg_ptr++;

                    ftoa(val, tmpbuf, 6);
                    out = buf_puts(out, end, tmpbuf);
                    break;
                }

                case '%':
                    out = buf_putchar(out, end, '%');
                    break;

                default:
                    out = buf_putchar(out, end, '%');
                    out = buf_putchar(out, end, *p);
                    break;
            }
        } else {
            out = buf_putchar(out, end, *p);
        }
        p++;
    }

    // Null terminate
    if (size > 0) {
        if (out <= end) {
            *out = '\0';
        } else {
            *end = '\0';
        }
    } else if (buf) {
        *out = '\0';
    }

    return (int)(out - buf);
}

/**
 * @brief Write formatted output to a string.
 *
 * @param str Output buffer.
 * @param fmt Format string.
 * @param ... Variable arguments.
 * @return Number of characters written (excluding null terminator).
 */
int sprintf(char* str, const char* fmt, ...) {
    void** arg_ptr = (void**)(&fmt);
    arg_ptr++;

    return sprintf_internal(str, 0, fmt, arg_ptr);
}

/**
 * @brief Write formatted output to a sized buffer.
 *
 * @param str Output buffer.
 * @param size Buffer size.
 * @param fmt Format string.
 * @param ... Variable arguments.
 * @return Number of characters that would have been written (excluding null terminator),
 *         or a negative value on error.
 */
int snprintf(char* str, size_t size, const char* fmt, ...) {
    if (!str || size == 0) {
        return 0;
    }

    void** arg_ptr = (void**)(&fmt);
    arg_ptr++;

    return sprintf_internal(str, size, fmt, arg_ptr);
}
