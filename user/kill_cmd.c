/**
 * @file kill_cmd.c
 * @brief Send signals to processes for Serotonin OS
 *
 * Usage: kill [-SIGNAL] PID...
 * Default signal is 15 (SIGTERM).
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: kill [-SIGNAL] PID...\n");
        return 1;
    }

    int sig = 15; /* SIGTERM */
    int pid_start = 1;

    /* Check for -SIGNAL argument */
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] >= '0' && argv[1][1] <= '9') {
        sig = atoi(argv[1] + 1);
        if (sig <= 0) {
            printf("kill: invalid signal: %s\n", argv[1]);
            return 1;
        }
        pid_start = 2;
    }

    if (pid_start >= argc) {
        printf("usage: kill [-SIGNAL] PID...\n");
        return 1;
    }

    int status = 0;

    for (int i = pid_start; i < argc; i++) {
        char *endptr;
        long pid = strtol(argv[i], &endptr, 10);
        if (*endptr != '\0') {
            printf("kill: invalid pid: %s\n", argv[i]);
            status = 1;
            continue;
        }

        if (kill((int)pid, sig) < 0) {
            printf("kill: %ld: cannot send signal %d (errno=%d)\n",
                   pid, sig, errno);
            status = 1;
        }
    }

    return status;
}
