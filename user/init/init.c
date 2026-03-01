/**
 * @file init.c
 * @brief System initialization program for Serotonin OS
 *
 * This is the first user-space program executed by the kernel.
 * It displays a welcome message and launches the system shell.
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

/**
 * @brief Init process entry point
 *
 * Prints welcome messages and executes the system shell.
 * If shell execution fails, reports an error.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (passed to shell)
 * @param envp Environment variables (passed to shell)
 * @return 1 on error (shell failed to load)
 */
int main(int argc, char **argv, char **envp) {
    printf("Welcome to \033[1m\033[38;2;122;152;255mSerotonin\033[0m!\n");

    printf("Loading shell\n");

    execve("/bin/sh", argv, envp);

    printf("Failed to load shell!\n");

    return 1;
}
