#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

extern int shm_map(int id);

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: sender <pid>\n");
        return 1;
    }

    int pid = atoi(argv[1]);
    printf("[sender] sending SIG1 to pid %d\n", pid);
    kill(pid, 1);

    char* buf = (char*)shm_map(0);
    printf("buf: %p\n", buf);
    printf("B sees: %s\n", buf);

    return 0;
}
