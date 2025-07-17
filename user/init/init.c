#include <stdint.h>

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

void _start() {
    char buf[64];
    char lol1[] = "Child!\n";
    char lol2[] = "Parent!\n";
    char lol3[] = "Fokring now!\n";
    uint32_t pid = system_call(4,0,0,0);
    if (pid == 0) {
        system_call(1,0,(uint32_t)lol1,0);
    } else {
        system_call(1,0,(uint32_t)lol2,0);
    }

    system_call(0,15,0,0);
}