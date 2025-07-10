#include <stdint.h>

static inline uint32_t system_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
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

void _start() {
    system_call(1,15,12,4363);
    system_call(0,15,0,0);
}