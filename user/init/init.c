#include <stdint.h>

void system_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    asm volatile (
        "int $0x80"
        :
        : "a" (arg1),  // eax
          "b" (arg2),  // ebx
          "c" (arg3),  // ecx
          "d" (arg4)   // edx
        : "memory"
    );
}

void _start() {
    system_call(0,15,0,0);
}