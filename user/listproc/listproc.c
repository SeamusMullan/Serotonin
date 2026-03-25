/**
 * @file listproc.c
 * @brief Process listing utility for Serotonin OS
 *
 * Displays a formatted table of all running processes including
 * their PID, name, priority, and privilege level (kernel/user mode).
 * Uses ANSI color codes to distinguish kernel and user processes.
 */

#include <stdio.h>
#include <lib5ht.h>

/**
 * @brief Main entry point for process listing utility
 *
 * Retrieves the list of running processes from the kernel and
 * displays them in a formatted table with color-coded privilege levels.
 *
 * @return 0 on success
 */
int main(void) {

    proc_5ht_t procs[128];
    sys_5ht_list_processes(procs, 128);

    printf("%-5s | %-20s | %-8s | %-12s\n", "PID", "NAME", "PRIORITY", "PRIVILEGE");
    printf("-----------------------------------------------------------\n");

    for (int i = 0; i < 128; i++) {
        if (procs[i].name[0] == '\0')
            break;
        printf("%s%-5d | %-20s | %-8d | %-12s\033[39m\033[49m\n",(procs[i].priv == 0) ? "\033[38;2;255;200;140m" : "\033[38;2;170;210;255m", procs[i].pid, procs[i].name, procs[i].priority, (procs[i].priv == 0) ? "kernel mode" : "user mode");
    }

    return 0;
}