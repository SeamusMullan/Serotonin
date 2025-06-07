#include "stdlib.h"
#include <stddef.h>

/**
 * @brief Move memory area.
 * 
 * @param dstptr Destination pointer.
 * @param srcptr Source pointer.
 * @param size Size of the memory area to move.
 * @return void* Pointer to the destination.
 */
void* memmove(void* dstptr, const void* srcptr, size_t size) {
	unsigned char* dst = (unsigned char*) dstptr;
	const unsigned char* src = (const unsigned char*) srcptr;
	if (dst < src) {
		for (size_t i = 0; i < size; i++)
			dst[i] = src[i];
	} else {
		for (size_t i = size; i != 0; i--)
			dst[i-1] = src[i-1];
	}
	return dstptr;
}

/**
 * @brief Compare two memory areas.
 * 
 * @param aptr Pointer to the first memory area.
 * @param bptr Pointer to the second memory area.
 * @param size Size of the memory areas to compare.
 * @return int Negative if a < b, positive if a > b, zero if equal.
 */
int memcmp(const void* aptr, const void* bptr, size_t size) {
	const unsigned char* a = (const unsigned char*) aptr;
	const unsigned char* b = (const unsigned char*) bptr;
	for (size_t i = 0; i < size; i++) {
		if (a[i] < b[i])
			return -1;
		else if (b[i] < a[i])
			return 1;
	}
	return 0;
}

/**
 * @brief Set memory area to a specific value.
 * 
 * @param bufptr Pointer to the memory area.
 * @param value Value to set.
 * @param size Size of the memory area.
 * @return void* Pointer to the memory area.
 */
void* memset(void* bufptr, int value, size_t size) {
	unsigned char* buf = (unsigned char*) bufptr;
	for (size_t i = 0; i < size; i++)
		buf[i] = (unsigned char) value;
	return bufptr;
}

/**
 * @brief Copy memory area.
 * 
 * @param dstptr Destination pointer.
 * @param srcptr Source pointer.
 * @param size Size of the memory area to copy.
 * @return void* Pointer to the destination.
 */
void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size) {
    unsigned char* dst = (unsigned char*) dstptr;
    const unsigned char* src = (const unsigned char*) srcptr;

    // Align to 4 bytes (optional)
    while (size > 0 && ((uintptr_t)dst & 3)) {
        *dst++ = *src++;
        size--;
    }

    // Copy 4 bytes at a time
    uint32_t* dst32 = (uint32_t*)dst;
    const uint32_t* src32 = (const uint32_t*)src;
    while (size >= 4) {
        *dst32++ = *src32++;
        size -= 4;
    }

    // Copy any remaining bytes
    dst = (unsigned char*)dst32;
    src = (const unsigned char*)src32;
    while (size > 0) {
        *dst++ = *src++;
        size--;
    }

    return dstptr;
}
