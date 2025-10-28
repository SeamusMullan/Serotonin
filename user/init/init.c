#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv, char **envp) {
    printf("Welcome to Serotonin from init!\n");
    printf("Loading shell\n");

    execve("/bin/sh", argv, envp);
    
    printf("Failed to load shell!\n");

    return 1;
}
