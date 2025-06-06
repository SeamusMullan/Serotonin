#ifndef _KERNEL_STDLIB
#define _KERNEL_STDLIB

#include <stdint.h>
#include <stddef.h>

void itoa(int value, char* str);
void utoa_hex(uint32_t value, char* str);
void utoa(uint32_t value, char* str);
void ltoa(long value, char* buffer);
void ultoa(unsigned long value, char* buffer);
void ultoa_hex(unsigned long value, char* buffer);
void lltoa(long long value, char* buffer);
void ulltoa(unsigned long long value, char* buffer);
void ulltoa_hex(unsigned long long value, char* buffer);
void* memmove(void* dstptr, const void* srcptr, size_t size);
int memcmp(const void* aptr, const void* bptr, size_t size);
void* memset(void* bufptr, int value, size_t size);
void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size);

__attribute__((__noreturn__))
void abort();

#endif