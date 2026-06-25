/**
 * @file rmdir_cmd.c
 * @brief Remove empty directories for Serotonin OS
 *
 * Removes each directory specified on the command line.
 * Directories must be empty.
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: rmdir DIR...\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            printf("rmdir: %s: failed to remove (errno=%d)\n", argv[i], errno);
            status = 1;
        }
    }

    return status;
}
