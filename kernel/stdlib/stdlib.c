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
    asm volatile("cli; hlt");
    while (1) { }
    __builtin_unreachable();
}

/**
 * @brief Convert string to integer.
 * 
 * @param str The string to convert.
 * @return int The converted integer value.
 */
int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    // Handle sign
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // Convert digits
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

/**
 * @brief Convert string to long integer.
 * 
 * @param str The string to convert.
 * @return long The converted long integer value.
 */
long atol(const char* str) {
    long result = 0;
    int sign = 1;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    // Handle sign
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // Convert digits
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

/**
 * @brief Convert string to long long integer.
 * 
 * @param str The string to convert.
 * @return long long The converted long long integer value.
 */
long long atoll(const char* str) {
    long long result = 0;
    int sign = 1;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    // Handle sign
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // Convert digits
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

/**
 * @brief Find first occurrence of character in memory block.
 * 
 * @param ptr Pointer to the memory block.
 * @param value Character to search for.
 * @param num Number of bytes to search.
 * @return void* Pointer to the first occurrence, or NULL if not found.
 */
void* memchr(const void* ptr, int value, size_t num) {
    const unsigned char* p = (const unsigned char*)ptr;
    unsigned char c = (unsigned char)value;
    
    for (size_t i = 0; i < num; i++) {
        if (p[i] == c) {
            return (void*)(p + i);
        }
    }
    
    return NULL;
}

/**
 * @brief Compute absolute value of integer.
 * 
 * @param n The integer value.
 * @return int The absolute value.
 */
int abs(int n) {
    return (n < 0) ? -n : n;
}

/**
 * @brief Compute absolute value of long integer.
 * 
 * @param n The long integer value.
 * @return long The absolute value.
 */
long labs(long n) {
    return (n < 0) ? -n : n;
}

/**
 * @brief Compute absolute value of long long integer.
 * 
 * @param n The long long integer value.
 * @return long long The absolute value.
 */
long long llabs(long long n) {
    return (n < 0) ? -n : n;
}

/**
 * @brief Divide two integers and return quotient and remainder.
 * 
 * @param numer The numerator.
 * @param denom The denominator.
 * @return div_t Structure containing quotient and remainder.
 */
div_t div(int numer, int denom) {
    div_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

/**
 * @brief Divide two long integers and return quotient and remainder.
 * 
 * @param numer The numerator.
 * @param denom The denominator.
 * @return ldiv_t Structure containing quotient and remainder.
 */
ldiv_t ldiv(long numer, long denom) {
    ldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}


// ========== This currently breaks the build so we got comments for that ==========
/**
 * @brief Divide two long long integers and return quotient and remainder.
 * 
 * @param numer The numerator.
 * @param denom The denominator.
 * @return lldiv_t Structure containing quotient and remainder.
 */
// lldiv_t lldiv(long long numer, long long denom) {
//     lldiv_t result;
//     result.quot = numer / denom;
//     result.rem = numer % denom;
//     return result;
// }
// ========== End of the breaking code ==========

/* Random number generator state */
static unsigned long next = 1;

/**
 * @brief Generate pseudo-random number.
 * 
 * @return int Random number between 0 and RAND_MAX.
 */
int rand(void) {
    next = next * 1103515245 + 12345;
    return (unsigned int)(next / 65536) % 32768;
}

/**
 * @brief Seed the random number generator.
 * 
 * @param seed The seed value.
 */
void srand(unsigned int seed) {
    next = seed;
}

/**
 * @brief Check if character is alphabetic.
 * 
 * @param c The character to check.
 * @return int Non-zero if alphabetic, zero otherwise.
 */
int isalpha(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/**
 * @brief Check if character is a digit.
 * 
 * @param c The character to check.
 * @return int Non-zero if digit, zero otherwise.
 */
int isdigit(int c) {
    return c >= '0' && c <= '9';
}

/**
 * @brief Check if character is alphanumeric.
 * 
 * @param c The character to check.
 * @return int Non-zero if alphanumeric, zero otherwise.
 */
int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

/**
 * @brief Check if character is whitespace.
 * 
 * @param c The character to check.
 * @return int Non-zero if whitespace, zero otherwise.
 */
int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/**
 * @brief Check if character is uppercase.
 * 
 * @param c The character to check.
 * @return int Non-zero if uppercase, zero otherwise.
 */
int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

/**
 * @brief Check if character is lowercase.
 * 
 * @param c The character to check.
 * @return int Non-zero if lowercase, zero otherwise.
 */
int islower(int c) {
    return c >= 'a' && c <= 'z';
}

/**
 * @brief Check if character is printable.
 * 
 * @param c The character to check.
 * @return int Non-zero if printable, zero otherwise.
 */
int isprint(int c) {
    return c >= 32 && c <= 126;
}

/**
 * @brief Check if character is punctuation.
 * 
 * @param c The character to check.
 * @return int Non-zero if punctuation, zero otherwise.
 */
int ispunct(int c) {
    return isprint(c) && !isalnum(c) && !isspace(c);
}

/**
 * @brief Check if character is control character.
 * 
 * @param c The character to check.
 * @return int Non-zero if control character, zero otherwise.
 */
int iscntrl(int c) {
    return (c >= 0 && c <= 31) || c == 127;
}

/**
 * @brief Check if character is hexadecimal digit.
 * 
 * @param c The character to check.
 * @return int Non-zero if hexadecimal digit, zero otherwise.
 */
int isxdigit(int c) {
    return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/**
 * @brief Convert character to uppercase.
 * 
 * @param c The character to convert.
 * @return int The uppercase character.
 */
int toupper(int c) {
    if (islower(c)) {
        return c - 'a' + 'A';
    }
    return c;
}

/**
 * @brief Convert character to lowercase.
 * 
 * @param c The character to convert.
 * @return int The lowercase character.
 */
int tolower(int c) {
    if (isupper(c)) {
        return c - 'A' + 'a';
    }
    return c;
}

/**
 * @brief Convert integer to string with specified base.
 * 
 * @param value The integer value to convert.
 * @param str The output string buffer.
 * @param base The base for conversion (2-36).
 * @return char* Pointer to the result string.
 */
char* itoa_base(int value, char* str, int base) {
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    char buffer[65];
    int i = 0;
    int is_negative = 0;
    
    if (value < 0 && base == 10) {
        is_negative = 1;
        value = -value;
    }
    
    if (value == 0) {
        buffer[i++] = '0';
    } else {
        while (value > 0) {
            buffer[i++] = digits[value % base];
            value /= base;
        }
    }
    
    if (is_negative) {
        buffer[i++] = '-';
    }
    
    int j;
    for (j = 0; j < i; j++) {
        str[j] = buffer[i - 1 - j];
    }
    str[j] = '\0';
    
    return str;
}

/**
 * @brief Convert unsigned integer to string with specified base.
 * 
 * @param value The unsigned integer value to convert.
 * @param str The output string buffer.
 * @param base The base for conversion (2-36).
 * @return char* Pointer to the result string.
 */
char* utoa_base(unsigned int value, char* str, int base) {
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    char buffer[65];
    int i = 0;
    
    if (value == 0) {
        buffer[i++] = '0';
    } else {
        while (value > 0) {
            buffer[i++] = digits[value % base];
            value /= base;
        }
    }
    
    int j;
    for (j = 0; j < i; j++) {
        str[j] = buffer[i - 1 - j];
    }
    str[j] = '\0';
    
    return str;
}

/**
 * @brief Quick sort implementation.
 * 
 * @param base Pointer to the array to sort.
 * @param num Number of elements in the array.
 * @param size Size of each element in bytes.
 * @param compare Comparison function.
 */
void qsort(void* base, size_t num, size_t size, int (*compare)(const void*, const void*)) {
    if (num < 2) return;
    
    char* arr = (char*)base;
    char* pivot = arr + (num / 2) * size;
    char temp[256]; // Use fixed-size buffer instead of alloca
    
    if (size > sizeof(temp)) return; // Safety check
    
    size_t left = 0, right = num - 1;
    
    while (left <= right) {
        while (compare(arr + left * size, pivot) < 0) left++;
        while (compare(arr + right * size, pivot) > 0) right--;
        
        if (left <= right) {
            memcpy(temp, arr + left * size, size);
            memcpy(arr + left * size, arr + right * size, size);
            memcpy(arr + right * size, temp, size);
            left++;
            right--;
        }
    }
    
    if (right > 0) qsort(arr, right + 1, size, compare);
    if (left < num) qsort(arr + left * size, num - left, size, compare);
}

/**
 * @brief Binary search implementation.
 * 
 * @param key Pointer to the key to search for.
 * @param base Pointer to the sorted array.
 * @param num Number of elements in the array.
 * @param size Size of each element in bytes.
 * @param compare Comparison function.
 * @return void* Pointer to the found element, or NULL if not found.
 */
void* bsearch(const void* key, const void* base, size_t num, size_t size, int (*compare)(const void*, const void*)) {
    const char* arr = (const char*)base;
    size_t left = 0, right = num;
    
    while (left < right) {
        size_t mid = (left + right) / 2;
        int cmp = compare(key, arr + mid * size);
        
        if (cmp == 0) {
            return (void*)(arr + mid * size);
        } else if (cmp < 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }
    
    return NULL;
}

/**
 * @brief Execute system command (stub implementation).
 * 
 * @param command The command to execute.
 * @return int Always returns -1 (not implemented in kernel).
 */
int system(const char* command) {
    (void)command; // Suppress unused parameter warning
    return -1; // Not implemented in kernel space
}

/**
 * @brief Exit program with status code.
 * 
 * @param status The exit status code.
 */
__attribute__((__noreturn__))
void exit(int status) {
    printf("exit() called with status %d - system halted\n", status);
    asm volatile("cli; hlt");
    while (1) { }
    __builtin_unreachable();
}

/**
 * @brief Copy string.
 * 
 * @param dest Destination string buffer.
 * @param src Source string.
 * @return char* Pointer to destination string.
 */
char* strcpy(char* dest, const char* src) {
    char* original_dest = dest;
    while ((*dest++ = *src++));
    return original_dest;
}

/**
 * @brief Copy at most n characters from string.
 * 
 * @param dest Destination string buffer.
 * @param src Source string.
 * @param n Maximum number of characters to copy.
 * @return char* Pointer to destination string.
 */
char* strncpy(char* dest, const char* src, size_t n) {
    char* original_dest = dest;
    while (n-- && (*dest++ = *src++));
    while (n-- > 0) *dest++ = '\0';
    return original_dest;
}

/**
 * @brief Concatenate strings.
 * 
 * @param dest Destination string buffer.
 * @param src Source string to append.
 * @return char* Pointer to destination string.
 */
char* strcat(char* dest, const char* src) {
    char* original_dest = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return original_dest;
}

/**
 * @brief Concatenate at most n characters from string.
 * 
 * @param dest Destination string buffer.
 * @param src Source string to append.
 * @param n Maximum number of characters to append.
 * @return char* Pointer to destination string.
 */
char* strncat(char* dest, const char* src, size_t n) {
    char* original_dest = dest;
    while (*dest) dest++;
    while (n-- && (*dest++ = *src++));
    if (n == SIZE_MAX) *dest = '\0';
    return original_dest;
}

/**
 * @brief Compare two strings.
 * 
 * @param str1 First string.
 * @param str2 Second string.
 * @return int Negative if str1 < str2, positive if str1 > str2, zero if equal.
 */
int strcmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

/**
 * @brief Compare at most n characters of two strings.
 * 
 * @param str1 First string.
 * @param str2 Second string.
 * @param n Maximum number of characters to compare.
 * @return int Negative if str1 < str2, positive if str1 > str2, zero if equal.
 */
int strncmp(const char* str1, const char* str2, size_t n) {
    while (n && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

/**
 * @brief Find first occurrence of character in string.
 * 
 * @param str The string to search.
 * @param c The character to find.
 * @return char* Pointer to first occurrence, or NULL if not found.
 */
char* strchr(const char* str, int c) {
    while (*str) {
        if (*str == c) return (char*)str;
        str++;
    }
    return (c == '\0') ? (char*)str : NULL;
}

/**
 * @brief Find last occurrence of character in string.
 * 
 * @param str The string to search.
 * @param c The character to find.
 * @return char* Pointer to last occurrence, or NULL if not found.
 */
char* strrchr(const char* str, int c) {
    char* last = NULL;
    while (*str) {
        if (*str == c) last = (char*)str;
        str++;
    }
    return (c == '\0') ? (char*)str : last;
}

/**
 * @brief Find substring in string.
 * 
 * @param haystack The string to search in.
 * @param needle The substring to find.
 * @return char* Pointer to first occurrence, or NULL if not found.
 */
char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        
        if (!*n) return (char*)haystack;
        haystack++;
    }
    
    return NULL;
}

/**
 * @brief Get length of prefix matching characters in set.
 * 
 * @param str1 The string to check.
 * @param str2 The set of characters.
 * @return size_t Length of the prefix.
 */
size_t strspn(const char* str1, const char* str2) {
    const char* p;
    const char* a;
    size_t count = 0;
    
    for (p = str1; *p; p++) {
        for (a = str2; *a; a++) {
            if (*p == *a) break;
        }
        if (*a == '\0') return count;
        count++;
    }
    
    return count;
}

/**
 * @brief Get length of prefix not matching characters in set.
 * 
 * @param str1 The string to check.
 * @param str2 The set of characters.
 * @return size_t Length of the prefix.
 */
size_t strcspn(const char* str1, const char* str2) {
    const char* p;
    const char* a;
    size_t count = 0;
    
    for (p = str1; *p; p++) {
        for (a = str2; *a; a++) {
            if (*p == *a) return count;
        }
        count++;
    }
    
    return count;
}

/**
 * @brief Find first character in string that matches any character in set.
 * 
 * @param str1 The string to search.
 * @param str2 The set of characters.
 * @return char* Pointer to first matching character, or NULL if not found.
 */
char* strpbrk(const char* str1, const char* str2) {
    while (*str1) {
        const char* p = str2;
        while (*p) {
            if (*str1 == *p) return (char*)str1;
            p++;
        }
        str1++;
    }
    return NULL;
}

/* Static variable for strtok */
static char* strtok_last = NULL;

/**
 * @brief Split string into tokens.
 * 
 * @param str String to tokenize (NULL to continue with previous string).
 * @param delim Delimiter characters.
 * @return char* Pointer to next token, or NULL if no more tokens.
 */
char* strtok(char* str, const char* delim) {
    char* token_start;
    
    if (str != NULL) {
        strtok_last = str;
    } else if (strtok_last == NULL) {
        return NULL;
    }
    
    // Skip leading delimiters
    strtok_last += strspn(strtok_last, delim);
    
    if (*strtok_last == '\0') {
        strtok_last = NULL;
        return NULL;
    }
    
    token_start = strtok_last;
    
    // Find end of token
    strtok_last = strpbrk(token_start, delim);
    
    if (strtok_last != NULL) {
        *strtok_last = '\0';
        strtok_last++;
    }
    
    return token_start;
}