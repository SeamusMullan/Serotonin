#include "stdlib.h"
#include <stdint.h>
#include "../stdio/stdio.h"

/**
 * @brief Convert an integer to a string (base 10).
 * 
 * @param value The integer value to convert.
 * @param str The output string buffer.
 */
void itoa(int value, char* str) {
    char buffer[12];
    int i = 0, is_negative = 0;

    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    do {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);

    if (is_negative) {
        buffer[i++] = '-';
    }

    for (int j = i - 1, k = 0; j >= 0; j--, k++) {
        str[k] = buffer[j];
    }
    str[i] = '\0';
}

/**
 * @brief Convert an unsigned integer to a hexadecimal string.
 *
 * @param value The unsigned integer value to convert.
 * @param str The output string buffer.
 */
void utoa_hex(uint32_t value, char* str) {
    const char* hex = "0123456789abcdef";
    char buffer[9];
    int i = 0;

    do {
        buffer[i++] = hex[value % 16];
        value /= 16;
    } while (value > 0);

    for (int j = i - 1, k = 0; j >= 0; j--, k++) {
        str[k] = buffer[j];
    }
    str[i] = '\0';
}

/**
 * @brief Convert an unsigned integer to a string (base 10).
 *
 * @param value The unsigned integer value to convert.
 * @param str The output string buffer.
 */
void utoa(uint32_t value, char* str) {
    char buffer[11];
    int i = 0;

    do {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);

    for (int j = i - 1, k = 0; j >= 0; j--, k++) {
        str[k] = buffer[j];
    }
    str[i] = '\0';
}

/**
 * @brief Convert a long integer to a string (base 10).
 *
 * @param value The long integer value to convert.
 * @param str The output string buffer.
 */
void ltoa(long value, char* str) {
    if (value < 0) {
        *str++ = '-';
        value = -value;
    }

    char buf[20];
    int i = 0;
    do {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    } while (value);

    while (i--) {
        *str++ = buf[i];
    }
    *str = '\0';
}

/**
 * @brief Convert an unsigned long integer to a string (base 10).
 *
 * @param value The unsigned long integer value to convert.
 * @param str The output string buffer.
 */
void ultoa(unsigned long value, char* str) {
    char buf[20];
    int i = 0;
    do {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    } while (value);

    while (i--) {
        *str++ = buf[i];
    }
    *str = '\0';
}

/**
 * @brief Convert an unsigned long integer to a hexadecimal string.
 *
 * @param value The unsigned long integer value to convert.
 * @param str The output string buffer.
 */
void ultoa_hex(unsigned long value, char* str) {
    const char* hex_digits = "0123456789abcdef";
    char buf[16];
    int i = 0;

    do {
        buf[i++] = hex_digits[value & 0xF];
        value >>= 4;
    } while (value);

    while (i--) {
        *str++ = buf[i];
    }
    *str = '\0';
}

/**
 * @brief Convert a long long integer to a string (base 10).
 *
 * @param value The long long integer value to convert.
 * @param str The output string buffer.
 */
void lltoa(long long value, char* str) {
    if (value < 0) {
        *str++ = '-';
        value = -value;
    }

    char buf[32];
    int i = 0;
    do {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    } while (value);

    while (i--) {
        *str++ = buf[i];
    }
    *str = '\0';
}

/**
 * @brief Convert an unsigned long long integer to a string (base 10).
 *
 * @param value The unsigned long long integer value to convert.
 * @param str The output string buffer.
 */
void ulltoa(unsigned long long value, char* str) {
    char buf[32];
    int i = 0;
    do {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    } while (value);

    while (i--) {
        *str++ = buf[i];
    }
    *str = '\0';
}

/**
 * @brief Convert an unsigned long long integer to a hexadecimal string.
 *
 * @param value The unsigned long long integer value to convert.
 * @param str The output string buffer.
 */
void ulltoa_hex(unsigned long long value, char* str) {
    const char* hex_digits = "0123456789abcdef";
    char buf[16];
    int i = 0;

    do {
        buf[i++] = hex_digits[value & 0xF];
        value >>= 4;
    } while (value);

    while (i--) {
        *str++ = buf[i];
    }
    *str = '\0';
}

/**
 * @brief Halt System.
 * 
 * This function is called when a critical error occurs, such as a kernel panic.
 * It prints an abort message and halts the system.
 */
__attribute__((__noreturn__))
void abort() {
    printf("abort() called - system halted\n");
    __asm__ volatile("cli; hlt");
    while (1) { }
    __builtin_unreachable();
}