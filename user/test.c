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

    int pid2 = fork();
    if (pid2 == 0) {
        printf("child2\n");
    } else {
        printf("parent2\n");
        int status = 0;
        waitpid(pid2, &status);
        printf("child2 exited with status: %d\n",status);
    }
    

    return 0;
}
