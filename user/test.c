/**
 * @file test.c
 * @brief Test program for Serotonin OS libc functionality
 *
 * This program tests various libc functions including stdio, malloc/free,
 * fork/exec, and file I/O operations in the Serotonin OS environment.
 */

#include <unistd.h>

extern int waitpid(pid_t pid, int *status);

int main(int argc, char **argv, char **envp) {
    //asm volatile ("hlt");
    volatile int* ptr = (int*)0xDEADBEEF;
    *ptr = 2;
    return 0;
}
