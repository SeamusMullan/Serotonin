#ifndef _KERNEL_STDLIB
#define _KERNEL_STDLIB

#include <stdint.h>
#include <stddef.h>

/* Constants */
#define RAND_MAX 32767
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* Number conversion functions */
void kitoa(int value, char* str);
int map_range(int input, int in_min, int in_max, int out_min, int out_max);
void kutoa(uint32_t value, char* str);
void ltoa(long value, char* buffer);
void ultoa(unsigned long value, char* buffer);
void ultoa_hex(unsigned long value, char* buffer);
void lltoa(long long value, char* buffer);
void ulltoa(unsigned long long value, char* buffer);
void ulltoa_hex(unsigned long long value, char* buffer);
int atoi(const char* str);
long atol(const char* str);
long long atoll(const char* str);
void ftoa(double n, char *res, int precision);

/* Memory management functions */
void* memmove(void* dstptr, const void* srcptr, size_t size);
int memcmp(const void* aptr, const void* bptr, size_t size);
void* memset(void* bufptr, int value, size_t size);
void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size);
void* memcpy_nt(void* restrict dstptr, const void* restrict srcptr, size_t size);
void* memchr(const void* ptr, int value, size_t num);

/* Mathematical functions */
int abs(int n);
long labs(long n);
long long llabs(long long n);
float powf(float x, float y);

/* Pseudo-random number generation */
int rand(void);
void srand(unsigned int seed);

/* Character classification and conversion */
int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int iscntrl(int c);
int isxdigit(int c);
int toupper(int c);
int tolower(int c);

/* String conversion */
char* itoa_base(int value, char* str, int base);
char* utoa_base(unsigned int value, char* str, int base);
void utoa_hex(uint32_t value, char* str);

/* Additional string utility functions */
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
int strcmp(const char* str1, const char* str2);
int strncmp(const char* str1, const char* str2, size_t n);
char* strchr(const char* str, int c);
char* strrchr(const char* str, int c);
char* strstr(const char* haystack, const char* needle);
size_t strspn(const char* str1, const char* str2);
size_t strcspn(const char* str1, const char* str2);
char* strpbrk(const char* str1, const char* str2);
char* strtok(char* str, const char* delim);
int strcasecmp(const char *s1, const char *s2);

/* Utility functions */
void qsort(void* base, size_t num, size_t size, int (*compare)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t num, size_t size, int (*compare)(const void*, const void*));
int system(const char* command);

/* Process control */
__attribute__((__noreturn__))
void abort();
__attribute__((__noreturn__))
void exit(int status);

#endif