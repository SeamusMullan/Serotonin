/**
 * @file sleep_cmd.c
 * @brief Sleep utility for Serotonin OS
 *
 * Suspends execution for a specified number of seconds using
 * alarm() and pause() syscalls.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static void alarm_handler(int sig) {
    (void)sig;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: sleep SECONDS\n");
        return 1;
    }

    int seconds = atoi(argv[1]);
    if (seconds <= 0) {
        printf("sleep: invalid number of seconds '%s'\n", argv[1]);
        return 1;
    }

    signal(SIGALRM, alarm_handler);
    alarm((unsigned int)seconds);
    pause();

    return 0;
}
