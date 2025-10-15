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

    // Now launch the physics demo
    printf("\n=== Launching Physics Demo ===\n");
    int physics_pid = fork();
    if (physics_pid == 0) {
        // Child process - execute physics demo
        char *physics_argv[] = {"/bin/physics_demo", NULL};
        char *physics_envp[] = {"PATH=/", NULL};
        execve("/bin/physics_demo", physics_argv, physics_envp);
        // If execve fails, print error
        printf("Failed to execute physics demo\n");
        return 1;
    } else {
        // Parent process - wait for physics demo to complete
        printf("Waiting for physics demo to complete...\n");
        int physics_status = 0;
        waitpid(physics_pid, &physics_status);
        printf("Physics demo exited with status: %d\n", physics_status);
    }

    return 0;
}
