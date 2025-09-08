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
    char* strdbg = "user: testing argc, argv, envp\n";
    system_call(1,0,(uint32_t)strdbg,0);

    char newline[] = "\n";
    for (int i = 0; i < argc; i++) {
        system_call(1,0,(uint32_t)argv[i],0);
        system_call(1,0,(uint32_t)newline,0);
    }

    for (char **e=envp; *e; e++) {
        system_call(1,0,(uint32_t)*e,0);
        system_call(1,0,(uint32_t)newline,0);
    }

    char buf[64];
    strdbg = "user: testing user input\n";
    system_call(1,0,(uint32_t)strdbg,0);
    system_call(2,0,(uint32_t)buf,64);
    system_call(1,0,(uint32_t)buf,0);

    strdbg = "user: testing sbrk\n";
    system_call(1,0,(uint32_t)strdbg,0);

    uint32_t* somemem = (uint32_t*)system_call(11,1,0,0);
    *somemem = 0xDEADBEEF;
    if (*somemem == 0xDEADBEEF) {
        strdbg = "user: sbrk write OK\n";
        system_call(1,0,(uint32_t)strdbg,0);
    }

    strdbg = "user: testing fork\n";
    system_call(1,0,(uint32_t)strdbg,0);

    char lol1[] = "Child!\n";
    char lol2[] = "Parent!\n";
    char lol3[] = "back from waitpid!\n";
    char lol4[] = "/bin/test";
    uint32_t pid = system_call(4,0,0,0);
    if (pid == 0) {
        system_call(1,0,(uint32_t)lol1,0);
        strdbg = "user: testing execve in child\n";
        system_call(1,0,(uint32_t)strdbg,0);
        char *argv[] = {"/bin/test", "arg1", "arg2", NULL};
        char *envp[] = {"PATH=/", NULL};
        system_call(3,(uint32_t)lol4,(uint32_t)argv,(uint32_t)envp);
    } else {
        int troll = 0;
        system_call(1,0,(uint32_t)lol2,0);
        system_call(9,pid,(uint32_t)&troll,0);
        system_call(1,0,(uint32_t)lol3,0);
    }

    return 0;
}