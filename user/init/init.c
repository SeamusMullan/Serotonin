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
void main(int argc, char **argv, char **envp) {
    char newline[] = "\n";
    system_call(1,0,(uint32_t)argc,0);
    for (int i = 0; i < argc; i++) {
        system_call(1,0,(uint32_t)argv[i],0);
        system_call(1,0,(uint32_t)newline,0);
    }

    for (char **e=envp; *e; e++) {
        system_call(1,0,(uint32_t)*e,0);
        system_call(1,0,(uint32_t)newline,0);
    }

    char buf[64];
    char lol1[] = "Child!\n";
    char lol2[] = "Parent!\n";
    char lol3[] = "Forking now!\n";
    char lol4[] = "/bin/test";
    uint32_t pid = system_call(4,0,0,0);
    if (pid == 0) {
        system_call(1,0,(uint32_t)lol1,0);
        system_call(3,(uint32_t)lol4,0,0);
    } else {
        system_call(1,0,(uint32_t)lol2,0);
    }

    return;
}