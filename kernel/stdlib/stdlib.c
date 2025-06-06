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

// Halt system
__attribute__((__noreturn__))
void abort() {
    printf("abort() called - system halted\n");
    __asm__ volatile("cli; hlt");
    while (1) { }
    __builtin_unreachable();
}