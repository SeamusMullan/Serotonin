#ifndef _KERNEL
#define _KERNEL

#include <stdint.h>

void kernel_panic(char* str);
void *kernel_malloc(uint32_t size);
void kernel_free(void *ptr);

#endif