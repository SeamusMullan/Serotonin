/**
 * @file sender.c
 * @brief Signal sender and shared memory reader test program
 *
 * Demonstrates inter-process communication by sending signals to
 * another process and reading from shared memory. Works in conjunction
 * with receiver.c to test IPC mechanisms.
 *
 * @see receiver.c for the corresponding receiver program
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

/**
 * @brief Map an existing shared memory segment
 * @param id Shared memory ID to map
 * @return Pointer to mapped memory on success, NULL on error
 */
extern int shm_map(int id);

/**
 * @brief Main entry point for sender program
 *
 * Sends a signal to the specified process and reads from shared memory
 * segment ID 0 to display the message written by the receiver.
 *
 * @param argc Argument count (expects 2)
 * @param argv Argument vector (argv[1] = target PID)
 * @return 0 on success, 1 on usage error
 */
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
