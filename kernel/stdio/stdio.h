#ifndef _KERNEL_STDIO
#define _KERNEL_STDIO
#include <stddef.h>

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

#endif