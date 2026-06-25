/**
 * @file pwd.c
 * @brief Print working directory utility for Serotonin OS
 *
 * Prints the absolute pathname of the current working directory.
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/**
 * @brief Main entry point for pwd command
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return 0 on success, 1 on error
 */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char buf[1024];
    if (getcwd(buf, sizeof(buf)) == NULL) {
        printf("pwd: unable to get current directory (errno=%d)\n", errno);
        return 1;
    }
    puts(buf);
    return 0;
}
