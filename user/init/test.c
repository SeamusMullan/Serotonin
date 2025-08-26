#include <stdint.h>

/**
 * @brief Makes a system call.
 *
 * @param arg1 The first argument.
 * @param arg2 The second argument.
 * @param arg3 The third argument.
 * @param arg4 The fourth argument.
 * @return uint32_t The return value of the system call.
 */
uint32_t system_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    uint32_t ret;
    asm volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (arg1),  // eax
          "b" (arg2),  // ebx
          "c" (arg3),  // ecx
          "d" (arg4)   // edx
        : "memory"
    );
    return ret;
}

/**
 * @brief The entry point of the user program.
 */
void _start() {
    char lol1[] = "execve!\n";
    system_call(1,0,(uint32_t)lol1,0);
    system_call(0,15,0,0);
}