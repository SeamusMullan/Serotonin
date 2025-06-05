#ifndef _KERNEL_STDLIB
#define _KERNEL_STDLIB

#include <stdint.h>

void itoa(int value, char* str);
void utoa_hex(uint32_t value, char* str);
void utoa(uint32_t value, char* str);

#endif