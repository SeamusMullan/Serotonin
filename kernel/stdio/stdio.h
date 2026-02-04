#ifndef _KERNEL_STDIO
#define _KERNEL_STDIO

#include <stdint.h>
#include <stddef.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef struct stdio_lck {
    void* stdin_ptr;
    uint32_t  stdin_buf_size;
} stdio_lck_t;

enum print_status_types {
    PRINT_STATUS_DEBUG = 0,
    PRINT_STATUS_INFO = 1,
    PRINT_STATUS_WARNING = 2,
    PRINT_STATUS_ERROR = 3,
    PRINT_STATUS_FATAL = 4,
    PRINT_STATUS_SUCCESS = 5
};

void printf(const char* fmt, ...);
void printfs(enum print_status_types status_type, const char* fmt, ...);
void printfs_set_mask(uint32_t mask);

int sprintf(char* str, const char* fmt, ...);
int snprintf(char* str, size_t size, const char* fmt, ...);

#endif
