/**
 * @file receiver.c
 * @brief Signal and shared memory receiver test program
 *
 * Demonstrates inter-process communication in Serotonin OS using
 * signals and shared memory. This program creates a shared memory
 * segment, writes data to it, and waits for signals from other processes.
 *
 * @see sender.c for the corresponding sender program
 */

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

/**
 * @brief Create a new shared memory segment
 * @param size Size of the shared memory segment in bytes
 * @return Shared memory ID on success, -1 on error
 */
extern int shm_create(uint32_t size);

/**
 * @brief Map a shared memory segment into the process address space
 * @param id Shared memory ID
 * @return Pointer to mapped memory on success, NULL on error
 */
extern void* shm_map(int id);

/**
 * @brief Signal handler for incoming signals
 *
 * Called when the process receives a signal. Prints the signal number.
 *
 * @param sig Signal number received
 */
void handler(int sig) {
    printf("[receiver] got signal %d!\n", sig);
}

/**
 * @brief Main entry point for receiver program
 *
 * Creates a shared memory segment, writes initial data, registers
 * a signal handler, and waits indefinitely for signals.
 *
 * @return Never returns (loops forever with pause())
 */
int main() {
    printf("[receiver] PID = %d, handler:%p\n", getpid(), handler);
    signal(1, handler);

    int id = shm_create(4096);
    char* buf = (char*)shm_map(id);
    strcpy(buf, "hello from A");
    printf("[receiver] id=%d shm says: %s (%p)\n", id, buf, buf);

    while (1) {
        pause();
    }
}