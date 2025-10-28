/**
 * @file test.c
 * @brief Test program for Serotonin OS libc functionality
 * 
 * This program tests various libc functions including stdio, malloc/free,
 * fork/exec, and file I/O operations in the Serotonin OS environment.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern int waitpid(pid_t pid, int *status);

int main(int argc, char **argv, char **envp) {
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Hello libc world!\n");
    char *p = malloc(42);
    printf("malloc gave me %p\n", p);

    scanf("%41s", p);
    printf("scanf:%s\n", p);

    free(p);

    for (int i = 0; i < argc; i++) {
        printf("argv[%d]=%s\n",i,argv[i]);
    }

    int i = 0;
    for (char **e=envp; *e; e++) {
        printf("envp[%d]=%s\n",i,*e);
        i++;
    }

    int pid = fork();
    if (pid == 0) {
        printf("child\n");
        asm volatile ("hlt"); // <-- #GP
    } else {
        printf("parent\n");
        int status = 0;
        waitpid(pid, &status);
        printf("child exited with status: %d\n",status);
    }

    FILE* fptr;
    fptr = fopen("/home/troll.txt", "w+");
    printf("fptr:%d\n",fptr);
    fprintf(fptr, "%s", "testing");
    fclose(fptr);

    execve("/bin/shell", 0, 0);

    return 0;
}
