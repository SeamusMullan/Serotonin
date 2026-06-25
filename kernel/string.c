#include <stdint.h>
#include <stddef.h>
#include <kernel/string.h>

/**
 * @brief Get the length of a string.
 * 
 * This function calculates the length of a null-terminated string.
 * @param str The string to measure.
 * @return size_t The length of the string.
 */
size_t strlen(const char* str) 
{
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}
