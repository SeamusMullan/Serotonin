#include "stdlib.h"
#include <stdint.h>
#include "../stdio/stdio.h"

// Integer to string (base 10)
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


// Unsigned int to hex string
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

// Unsigned int to string
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

// Long to string
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

// Unsigned long to string
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

// Unsigned long to hex string
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

// Long long to string
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

// Unsigned long long to string
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

// Unsigned long long to hex string
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


// Halt system
__attribute__((__noreturn__))
void abort() {
    printf("abort() called - system halted\n");
    __asm__ volatile("cli; hlt");
    while (1) { }
    __builtin_unreachable();
}