#include <stdint.h>
#include <stddef.h>

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
int main(int argc, char **argv, char **envp) {
    char* strdbg = "Serotonin usermode init, loading unit test (/bin/test)\n";
    system_call(1,0,(uint32_t)strdbg,0);

    char lol4[] = "/bin/test";
    uint32_t pid = system_call(4,0,0,0);
    if (pid == 0) {
        char *argv[] = {"/bin/test", "arg1", "arg2", NULL};
        char *envp[] = {"PATH=/", NULL};
        system_call(3,(uint32_t)lol4,(uint32_t)argv,(uint32_t)envp);
    } else {
        int troll = 0;
        system_call(9,pid,(uint32_t)&troll,0);
    }

    return 0;
}