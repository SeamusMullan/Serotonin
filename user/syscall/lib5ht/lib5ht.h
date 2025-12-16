#ifndef _LIB5HT
#define _LIB5HT

#include <unistd.h>
#include <errno.h>

typedef struct proc_5ht {
    int pid;
    char name[32];
    int priority;
    int priv;
} proc_5ht_t;

/**
 * @brief Execute a system call via interrupt 0x80
 * 
 * Low-level function that performs the actual system call by triggering
 * interrupt 0x80 with the appropriate register values.
 * 
 * @param num System call number
 * @param arg1 First argument
 * @param arg2 Second argument
 * @param arg3 Third argument
 * @return System call return value, or sets errno and returns error code on failure
 */
static inline int do_syscall(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    register uint32_t eax asm("eax") = num;
    register uint32_t ebx asm("ebx") = arg1;
    register uint32_t ecx asm("ecx") = arg2;
    register uint32_t edx asm("edx") = arg3;

    asm volatile("int $0x80"
                 : "+a"(eax)
                 : "b"(ebx), "c"(ecx), "d"(edx)
                 : "memory");

    if ((int)eax < 0) {
        errno = -(int)eax;
        return errno;
    }
    return eax;
}

int sys_5ht_list_processes(proc_5ht_t *buf, size_t max);

#endif